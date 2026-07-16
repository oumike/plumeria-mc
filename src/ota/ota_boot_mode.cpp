#include "ota/ota_boot_mode.h"

#include <Preferences.h>
#include <esp_attr.h>
#include <stdint.h>

namespace plumeria {
namespace ota {
namespace {

constexpr char kOtaBootNs[] = "ota_boot";
constexpr char kOtaBootReqKey[] = "request";
constexpr uint32_t kOtaBootRtcMagic = 0x4F544231UL;  // "OTB1"
RTC_DATA_ATTR static uint32_t s_ota_boot_rtc_flag = 0;

}  // namespace

bool requestBootMode() {
  // RTC survives software reboot and provides a low-friction fallback path.
  s_ota_boot_rtc_flag = kOtaBootRtcMagic;

  Preferences prefs;
  if (!prefs.begin(kOtaBootNs, false)) {
    return true;
  }
  const size_t wrote = prefs.putUChar(kOtaBootReqKey, 1);
  prefs.end();

  if (wrote == sizeof(uint8_t)) {
    Preferences verify;
    if (verify.begin(kOtaBootNs, true)) {
      const bool nvs_requested = verify.getUChar(kOtaBootReqKey, 0) != 0;
      verify.end();
      if (nvs_requested) {
        return true;
      }
    }
  }

  return s_ota_boot_rtc_flag == kOtaBootRtcMagic;
}

bool consumeBootModeRequest() {
  const bool rtc_requested = (s_ota_boot_rtc_flag == kOtaBootRtcMagic);
  if (rtc_requested) {
    s_ota_boot_rtc_flag = 0;
  }

  Preferences prefs;
  if (!prefs.begin(kOtaBootNs, false)) {
    return rtc_requested;
  }

  const bool nvs_requested = prefs.getUChar(kOtaBootReqKey, 0) != 0;
  if (nvs_requested) {
    (void)prefs.putUChar(kOtaBootReqKey, 0);
  }
  prefs.end();
  return rtc_requested || nvs_requested;
}

}  // namespace ota
}  // namespace plumeria
