#include <Arduino.h>
#include "PowerAwareBurstModule.h"

// Use the same providers the firmware uses elsewhere
#include "PowerStatus.h"                             // power status type
#include "mesh/MeshService.h"                        // service* pointer
#include "mesh/generated/meshtastic/admin.pb.h"      // AdminMessage (nanopb C)
#include "mesh/generated/meshtastic/config.pb.h"     // Config (nanopb C)
#include "mesh/generated/meshtastic/portnums.pb.h"   // Port numbers enum

// Declared by the firmware; DO NOT re-declare with a different type
extern MeshService *service;       // pointer (see MeshService.h)  [2](https://github.com/meshtastic/python/blob/master/meshtastic/mesh_interface.py)
extern PowerStatus *powerStatus;   // pointer (see PowerStatus.h)

// Send Admin set_config messages to temporarily change cadence/power profile
static void sendPositionPowerConfig(uint32_t bcast_interval_s,
                                    uint32_t gps_interval_s,
                                    bool power_saving)
{
  // 1) Position config (declaration-time init for nanopb)
  meshtastic_AdminMessage admin1 = meshtastic_AdminMessage_init_zero;
  admin1.which_payload_variant = meshtastic_AdminMessage_set_config_tag;

  // NOTE: In 2.6.10, the oneof union is named 'payload'
  meshtastic_Config cfg1 = meshtastic_Config_init_zero;
  cfg1.which_payload_variant = meshtastic_Config_position_tag;
  cfg1.payload.position = meshtastic_Config_PositionConfig_init_zero;
  cfg1.payload.position.smart_broadcast = true;   // present in v2.6.x position config
  cfg1.payload.position.gps_update_interval = gps_interval_s;
  // Some older trees don’t have broadcast_interval; using 1s gps + smart_broadcast
  // is sufficient to force an immediate/near-immediate send.

  admin1.payload.set_config = cfg1;
  service->sendAdmin(admin1);  // MeshService is a pointer in this tree  [2](https://github.com/meshtastic/python/blob/master/meshtastic/mesh_interface.py)

  // 2) Power config
  meshtastic_AdminMessage admin2 = meshtastic_AdminMessage_init_zero;
  admin2.which_payload_variant = meshtastic_AdminMessage_set_config_tag;

  meshtastic_Config cfg2 = meshtastic_Config_init_zero;
  cfg2.which_payload_variant = meshtastic_Config_power_tag;
  cfg2.payload.power = meshtastic_Config_PowerConfig_init_zero;
  cfg2.payload.power.is_power_saving = power_saving;

  admin2.payload.set_config = cfg2;
  service->sendAdmin(admin2);
}

// ---------------- Module ----------------

PowerAwareBurstModule::PowerAwareBurstModule()
  : ProtobufModule<meshtastic_Position>(
        /*port (unused by us)*/ meshtastic_PortNum_PRIVATE_APP,  // safe placeholder
        "PowerAwareBurst") {}

void PowerAwareBurstModule::setup() {
  bool u=false, c=false;
  pollPower(u, c);
  lastUsb = u;
  lastCharging = c;
}

static inline uint32_t now_secs() { return millis() / 1000; }

void PowerAwareBurstModule::scheduleNextBurst(uint32_t now) {
  uint32_t jitter = (uint32_t)(now % 15); // tiny deterministic jitter
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
      applyNormalProfile();       // revert to normal on power return
      usbOnSince = 0;
    }
  } else {
    usbOnSince = 0;
    if (lastUsb && usbOffSince == 0) usbOffSince = now;
    if (usbOffSince && (now - usbOffSince) >= USB_OFF_DEBOUNCE_SEC) {
      if (now >= rearmNotBefore && remainingBurstSends == 0) {
        remainingBurstSends = BURST_COUNT;
        nextBurstSendAt = 0;      // immediate
        rearmNotBefore = now + REARM_COOLDOWN_SEC;
      }
    }
  }

  // Burst scheduler: speed up cadence momentarily to elicit a send
  if (remainingBurstSends > 0) {
    if (nextBurstSendAt == 0 || now >= nextBurstSendAt) {
      // 1s/1s + smart_broadcast triggers a near-immediate Position send
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
    // getIsCharging() can be unreliable across boards; not required for our logic
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
