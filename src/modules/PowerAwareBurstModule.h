#pragma once
#include "mesh/MeshService.h"
#include "mesh/ProtobufModule.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

class PowerAwareBurstModule : public ProtobufModule<meshtastic_Position> {
public:
  PowerAwareBurstModule();
  void setup();
  void loop();

private:
  // State
  bool     lastUsb = false;
  bool     lastCharging = false;
  uint32_t nextBurstSendAt = 0;
  uint8_t  remainingBurstSends = 0;
  uint32_t rearmNotBefore = 0;
  uint32_t usbOffSince = 0;
  uint32_t usbOnSince  = 0;

  // Tunables
  static constexpr uint32_t BURST_COUNT           = 5;
  static constexpr uint32_t BURST_MIN_SPACING_SEC = 120;
  static constexpr uint32_t BURST_WINDOW_SEC      = 600;
  static constexpr uint32_t USB_OFF_DEBOUNCE_SEC  = 10;
  static constexpr uint32_t USB_ON_DEBOUNCE_SEC   = 10;
  static constexpr uint32_t REARM_COOLDOWN_SEC    = 1800;

  // Profiles
  static constexpr uint32_t NORMAL_GPS_INTERVAL_S   = 60;
  static constexpr uint32_t NORMAL_BCAST_INTERVAL_S = 120;
  static constexpr uint32_t LOW_GPS_INTERVAL_S      = 600;
  static constexpr uint32_t LOW_BCAST_INTERVAL_S    = 1800;

  // Helpers
  void pollPower(bool& hasUsb, bool& isCharging);
  void scheduleNextBurst(uint32_t now);
  void applyLowPowerProfile();
  void applyNormalProfile();
};
