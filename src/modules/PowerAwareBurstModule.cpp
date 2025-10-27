#include "PowerAwareBurstModule.h"
#include "mesh/NodeDB.h"
#include "modules/AdminModule.h"
#include <Arduino.h>
#include <cstdlib>
#include "PowerStatus.h"
extern PowerStatus powerStatus;

void PowerAwareBurstModule::pollPower(bool& hasUsb, bool& isCharging) {
  hasUsb     = powerStatus.getHasUSB();
  isCharging = powerStatus.getIsCharging(); // If this symbol is missing in 2.6.10 for your board, tell me and I’ll give the alternative.
}
``

PowerAwareBurstModule::PowerAwareBurstModule()
  : ProtobufModule<meshtastic_Position>(PortNum_PositionApp, "PowerAwareBurst")
{}

void PowerAwareBurstModule::setup() {
  pollPower(lastUsb, lastCharging);
}

void PowerAwareBurstModule::loop() {
  bool hasUsb=false, isCharging=false;
  pollPower(hasUsb, isCharging);
  uint32_t now = millis() / 1000; //seconds since boot

  // Debounce transitions
  if (hasUsb) {
    usbOffSince = 0;
    if (!lastUsb && usbOnSince == 0) usbOnSince = now;
    if (usbOnSince && (now - usbOnSince) >= USB_ON_DEBOUNCE_SEC) {
      applyNormalProfile();   // revert on power present
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

  // Burst scheduler
  if (remainingBurstSends > 0) {
    if (nextBurstSendAt == 0 || now >= nextBurstSendAt) {
      sendPositionNow();
      remainingBurstSends--;
      if (remainingBurstSends == 0) {
        applyLowPowerProfile(); // after last send, go low-power
      } else {
        uint32_t jitter = std::rand() % 15; // reduce collisions
        nextBurstSendAt = now + BURST_MIN_SPACING_SEC + jitter;
      }
    }
  }

  lastUsb = hasUsb;
  lastCharging = isCharging;
}

// NOTE: implement using the existing power subsystem used by Telemetry/Power for your board.
// Examples in-tree expose similar helpers to read "USB present" and "charging" from the PMIC.
void PowerAwareBurstModule::pollPower(bool& hasUsb, bool& isCharging) {
  // hasUsb = Power::instance().hasUSB();
  // isCharging = Power::instance().isCharging();
  hasUsb = /* TODO: hook to PMIC-backed flag */;
  isCharging = /* TODO: hook to PMIC-backed flag */;
}

bool PowerAwareBurstModule::buildPosition(meshtastic_Position& pos) {
  auto& ndb = NodeDB::instance();
  auto fix = ndb.getOurNodePosition();   // obtain last valid fix
  if (!fix.valid) return false;

  pos.set_latitude_i(fix.latitude_i);
  pos.set_longitude_i(fix.longitude_i);
  pos.set_precision_bits(32);            // full precision; tune via config if desired
  return true;
}

void PowerAwareBurstModule::sendPositionNow() {
  meshtastic_Position p{};
  if (!buildPosition(p)) return;
  this->send(p);
}

void PowerAwareBurstModule::applyLowPowerProfile() {
  AdminMessage am;
  auto cfg = am.mutable_set_config();
  auto* pos = cfg->mutable_position();
  pos->set_smart_broadcast(true);
  pos->set_broadcast_interval(LOW_BCAST_INTERVAL_S);
  pos->set_gps_update_interval(LOW_GPS_INTERVAL_S);

  auto* pwr = cfg->mutable_power();
  pwr->set_power_saving(true);

  AdminModule::send(am);
}

void PowerAwareBurstModule::applyNormalProfile() {
  AdminMessage am;
  auto cfg = am.mutable_set_config();
  auto* pos = cfg->mutable_position();
  pos->set_smart_broadcast(true);
  pos->set_broadcast_interval(NORMAL_BCAST_INTERVAL_S);
  pos->set_gps_update_interval(NORMAL_GPS_INTERVAL_S);

  auto* pwr = cfg->mutable_power();
  pwr->set_power_saving(false);

  AdminModule::send(am);
}
