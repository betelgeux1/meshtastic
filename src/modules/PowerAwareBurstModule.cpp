#include <Arduino.h>
#include "PowerAwareBurstModule.h"

// Firmware power status provider used elsewhere (FSM, etc.).  [2](https://github.com/meshtastic/python/blob/master/meshtastic/mesh_interface.py)
#include "PowerStatus.h"
extern PowerStatus* powerStatus;  // pointer in this tree

// Admin/config nanopb protos
#include "mesh/generated/meshtastic/admin.pb.h"
#include "mesh/generated/meshtastic/config.pb.h"
#include "mesh/generated/meshtastic/portnums.pb.h"

// Mesh service to send Admin messages
#include "mesh/MeshService.h"
extern MeshService service;

// ----- Helper: send Admin set_config to adjust Position cadence and Power flag -----
static void sendPositionPowerConfig(uint32_t bcast_interval_s,
                                    uint32_t gps_interval_s,
                                    bool power_saving)
{
  // First Admin message: set Position config
  meshtastic_AdminMessage adminMsg = meshtastic_AdminMessage_init_zero;
  adminMsg.which_payload_variant = meshtastic_AdminMessage_set_config_tag;

  meshtastic_Config* cfg = &adminMsg.payload_variant.set_config;
  *cfg = meshtastic_Config_init_zero;
  cfg->which_payload_variant = meshtastic_Config_position_tag;
  cfg->payload_variant.position = meshtastic_Config_PositionConfig_init_zero;
  cfg->payload_variant.position.smart_broadcast = true;
  cfg->payload_variant.position.broadcast_interval = bcast_interval_s;
  cfg->payload_variant.position.gps_update_interval = gps_interval_s;

  service.sendAdmin(adminMsg);

  // Second Admin message: set Power config
  meshtastic_AdminMessage adminMsg2 = meshtastic_AdminMessage_init_zero;
  adminMsg2.which_payload_variant = meshtastic_AdminMessage_set_config_tag;

  meshtastic_Config* cfg2 = &adminMsg2.payload_variant.set_config;
  *cfg2 = meshtastic_Config_init_zero;
  cfg2->which_payload_variant = meshtastic_Config_power_tag;
  cfg2->payload_variant.power = meshtastic_Config_PowerConfig_init_zero;
  cfg2->payload_variant.power.is_power_saving = power_saving;

  service.sendAdmin(adminMsg2);
}

// ---------------- Module ----------------

PowerAwareBurstModule::PowerAwareBurstModule()
  : ProtobufModule<meshtastic_Position>(meshtastic_PortNum_PositionApp, "PowerAwareBurst")
{}

void PowerAwareBurstModule::setup() {
  bool u=false, c=false;
  pollPower(u, c);
  lastUsb = u;
  lastCharging = c;
}

static inline uint32_t now_secs() { return millis() / 1000; }

void PowerAwareBurstModule::scheduleNextBurst(uint32_t now) {
  uint32_t jitter = (uint32_t)(now % 15); // small deterministic jitter
  nextBurstSendAt = now + BURST_MIN_SPACING_SEC + jitter;
}

void PowerAwareBurstModule::loop() {
  const uint32_t now = now_secs();

  bool hasUsb=false, isCharging=false;
  pollPower(hasUsb, isCharging);

  // Debounce transitions
  if (hasUsb) {
    usbOffSince = 0;
    if (!lastUsb && usbOnSince == 0) usbOnSince = now;
    if (usbOnSince && (now - usbOnSince) >= USB_ON_DEBOUNCE_SEC) {
      applyNormalProfile();
      usbOnSince = 0;
    }
  } else {
    usbOnSince = 0;
    if (lastUsb && usbOffSince == 0) usbOffSince = now;
    if (usbOffSince && (now - usbOffSince) >= USB_OFF_DEBOUNCE_SEC) {
      if (now >= rearmNotBefore && remainingBurstSends == 0) {
        remainingBurstSends = BURST_COUNT;
        nextBurstSendAt = 0; // immediate
        rearmNotBefore = now + REARM_COOLDOWN_SEC;
      }
    }
  }

  // Burst scheduler: briefly accelerate Position cadence to get a send
  if (remainingBurstSends > 0) {
    if (nextBurstSendAt == 0 || now >= nextBurstSendAt) {
      // 1s/1s to force near-immediate Position send by the stock PositionModule  [4](https://meshtastic.org/docs/development/linux/)
      sendPositionPowerConfig(/*bcast*/1, /*gps*/1, /*power_saving*/false);

      remainingBurstSends--;
      if (remainingBurstSends == 0) {
        applyLowPowerProfile();
      } else {
        scheduleNextBurst(now);
      }
    }
  }

  lastUsb = hasUsb;
  lastCharging = isCharging;
}

void PowerAwareBurstModule::pollPower(bool& hasUsb, bool& isCharging) {
  if (powerStatus) {
    hasUsb     = powerStatus->getHasUSB();
    // Some variants don’t expose a reliable isCharging; default to false for our logic.
    isCharging = false;
  } else {
    hasUsb = false;
    isCharging = false;
  }
}

void PowerAwareBurstModule::applyLowPowerProfile() {
  sendPositionPowerConfig(
    /*bcast*/ LOW_BCAST_INTERVAL_S,
    /*gps*/   LOW_GPS_INTERVAL_S,
    /*power_saving*/ true
  );
}

void PowerAwareBurstModule::applyNormalProfile() {
  sendPositionPowerConfig(
    /*bcast*/ NORMAL_BCAST_INTERVAL_S,
    /*gps*/   NORMAL_GPS_INTERVAL_S,
    /*power_saving*/ false
  );
}