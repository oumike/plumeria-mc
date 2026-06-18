#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <esp_vfs_fat.h>
#include <esp_system.h>
#include <lvgl.h>
#include <stdarg.h>
#include <string.h>

#include "config/features.h"
#include "hal/device_board.h"
#include "hal/device_lvgl.h"
#include "mesh/mesh_adapter.h"
#include "ui/standalone_ui.h"
#include "web/web_config.h"

namespace {

constexpr uint32_t kLvglTickMs = 5;
constexpr int kUiChannelCapacity = 40;
constexpr char kCfgSdPath[] = "/plumeria/plumeria-config.yaml";
constexpr char kCfgSdPathFallback[] = "/plumeria-config.yaml";
constexpr uint32_t kCfgSdClockHz = 800000UL;

#if defined(DEVICE_TLORA_PAGER_TFT)
constexpr bool kPagerBootDiag = true;
#else
constexpr bool kPagerBootDiag = false;
#endif

plumeria::hal::DeviceBoard g_board;
plumeria::hal::DeviceLvgl g_display;
plumeria::mesh::MeshAdapter g_mesh;
plumeria::ui::StandaloneUi g_ui;
plumeria::web::WebSettings g_web_settings{};
char g_channel_names_buf[kUiChannelCapacity][32]{};

void pagerDiagLog(const char* fmt, ...) {
  if (!kPagerBootDiag || !fmt) {
    return;
  }

  va_list args;
  va_start(args, fmt);
  char msg[192] = {};
  vsnprintf(msg, sizeof(msg), fmt, args);
  Serial.print("[BOOT] ");
  Serial.println(msg);
  va_end(args);
}

void setErrText(char* out_err, size_t out_err_size, const char* text) {
  if (!out_err || out_err_size == 0) {
    return;
  }
  if (!text) {
    out_err[0] = '\0';
    return;
  }
  strncpy(out_err, text, out_err_size - 1);
  out_err[out_err_size - 1] = '\0';
}

void clearSdVfsRegistration() {
  // Ignore return value: if path is not registered this is a no-op.
  (void)esp_vfs_fat_unregister_path("/sdcard");
}

#if defined(DEVICE_TLORA_PAGER_TFT)
constexpr uint8_t kPagerXl9555RegOut0 = 0x02;
constexpr uint8_t kPagerXl9555RegOut1 = 0x03;
constexpr uint8_t kPagerXl9555RegCfg0 = 0x06;
constexpr uint8_t kPagerXl9555RegCfg1 = 0x07;
constexpr uint8_t kPagerExpSdDet = 10;
constexpr uint8_t kPagerExpSdPullen = 11;
constexpr uint8_t kPagerExpSdEn = 12;

enum class PagerSdPullenMode : uint8_t {
  kInput = 0,
  kLow = 1,
  kHigh = 2,
};

struct PagerSdProfile {
  bool invert_dir_sense;
  bool sd_en_high;
  PagerSdPullenMode pullen_mode;
};

bool pagerXl9555WriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool pagerXl9555ReadReg(uint8_t addr, uint8_t reg, uint8_t* val) {
  if (!val) {
    return false;
  }
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(static_cast<int>(addr), 1) != 1) {
    return false;
  }
  *val = static_cast<uint8_t>(Wire.read());
  return true;
}

void pagerXl9555SetOutput(uint8_t pin, bool level, bool invert_dir_sense,
                          uint8_t* out0, uint8_t* out1,
                          uint8_t* cfg0, uint8_t* cfg1) {
  if (!out0 || !out1 || !cfg0 || !cfg1) {
    return;
  }
  const uint8_t bit = static_cast<uint8_t>(1U << (pin & 0x07));
  if (pin < 8) {
    if (invert_dir_sense) {
      *cfg0 |= bit;
    } else {
      *cfg0 &= static_cast<uint8_t>(~bit);
    }
    if (level) {
      *out0 |= bit;
    } else {
      *out0 &= static_cast<uint8_t>(~bit);
    }
  } else {
    if (invert_dir_sense) {
      *cfg1 |= bit;
    } else {
      *cfg1 &= static_cast<uint8_t>(~bit);
    }
    if (level) {
      *out1 |= bit;
    } else {
      *out1 &= static_cast<uint8_t>(~bit);
    }
  }
}

void pagerXl9555SetInput(uint8_t pin, bool invert_dir_sense, uint8_t* cfg0, uint8_t* cfg1) {
  if (!cfg0 || !cfg1) {
    return;
  }
  const uint8_t bit = static_cast<uint8_t>(1U << (pin & 0x07));
  if (pin < 8) {
    if (invert_dir_sense) {
      *cfg0 &= static_cast<uint8_t>(~bit);
    } else {
      *cfg0 |= bit;
    }
  } else {
    if (invert_dir_sense) {
      *cfg1 &= static_cast<uint8_t>(~bit);
    } else {
      *cfg1 |= bit;
    }
  }
}

bool pagerGetSdProfile(int profile, PagerSdProfile* out_profile) {
  if (!out_profile) {
    return false;
  }
  switch (profile) {
    case 0:
      *out_profile = {false, true, PagerSdPullenMode::kInput};
      return true;
    case 1:
      *out_profile = {false, false, PagerSdPullenMode::kInput};
      return true;
    case 2:
      *out_profile = {false, true, PagerSdPullenMode::kLow};
      return true;
    case 3:
      *out_profile = {true, true, PagerSdPullenMode::kInput};
      return true;
    case 4:
      *out_profile = {true, false, PagerSdPullenMode::kInput};
      return true;
    default:
      return false;
  }
}

bool pagerPrepareSdPower(int profile, char* out_err, size_t out_err_size) {
  PagerSdProfile cfg{};
  if (!pagerGetSdProfile(profile, &cfg)) {
    setErrText(out_err, out_err_size, "Invalid SD profile");
    return false;
  }

  Wire.begin(3, 2, 100000UL);

  uint8_t exp_addr = 0xFF;
  for (uint8_t a = 0x20; a <= 0x27; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      exp_addr = a;
      break;
    }
  }

  if (exp_addr == 0xFF) {
    setErrText(out_err, out_err_size, "SD expander not found");
    return false;
  }

  uint8_t out0 = 0xFF;
  uint8_t out1 = 0xFF;
  uint8_t cfg0 = 0xFF;
  uint8_t cfg1 = 0xFF;
  (void)pagerXl9555ReadReg(exp_addr, kPagerXl9555RegOut0, &out0);
  (void)pagerXl9555ReadReg(exp_addr, kPagerXl9555RegOut1, &out1);
  (void)pagerXl9555ReadReg(exp_addr, kPagerXl9555RegCfg0, &cfg0);
  (void)pagerXl9555ReadReg(exp_addr, kPagerXl9555RegCfg1, &cfg1);

  // Keep Pager SD probing scoped to SD-specific expander lines only.
  pagerXl9555SetOutput(kPagerExpSdEn, cfg.sd_en_high, cfg.invert_dir_sense, &out0, &out1, &cfg0, &cfg1);
  pagerXl9555SetInput(kPagerExpSdDet, cfg.invert_dir_sense, &cfg0, &cfg1);
  if (cfg.pullen_mode == PagerSdPullenMode::kInput) {
    pagerXl9555SetInput(kPagerExpSdPullen, cfg.invert_dir_sense, &cfg0, &cfg1);
  } else {
    pagerXl9555SetOutput(kPagerExpSdPullen, cfg.pullen_mode == PagerSdPullenMode::kHigh,
                         cfg.invert_dir_sense, &out0, &out1, &cfg0, &cfg1);
  }

  const bool ok = pagerXl9555WriteReg(exp_addr, kPagerXl9555RegOut0, out0) &&
                  pagerXl9555WriteReg(exp_addr, kPagerXl9555RegOut1, out1) &&
                  pagerXl9555WriteReg(exp_addr, kPagerXl9555RegCfg0, cfg0) &&
                  pagerXl9555WriteReg(exp_addr, kPagerXl9555RegCfg1, cfg1);
  if (!ok) {
    setErrText(out_err, out_err_size, "SD expander write failed");
    return false;
  }

  setErrText(out_err, out_err_size, "");
  return true;
}
#endif

bool sdBeginForCurrentBoard(char* out_err, size_t out_err_size) {
#if defined(DEVICE_HELTEC_V4_EXPANSION)
  setErrText(out_err, out_err_size, "SD unsupported on Heltec");
  return false;
#else
  int sd_cs = -1;
  int sd_sck = -1;
  int sd_miso = -1;
  int sd_mosi = -1;
  int lora_cs = -1;
  int tft_cs = -1;

#if defined(DEVICE_TDECK)
  sd_cs = 39;
  sd_sck = 40;
  sd_miso = 38;
  sd_mosi = 41;
  lora_cs = 9;
  tft_cs = 12;
#elif defined(DEVICE_TLORA_PAGER_TFT)
  sd_cs = 21;
  sd_sck = 35;
  sd_miso = 33;
  sd_mosi = 34;
  lora_cs = 36;
  tft_cs = 38;
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
  sd_cs = 12;
  sd_sck = 40;
  sd_miso = 39;
  sd_mosi = 14;
  lora_cs = 5;
  tft_cs = 37;
#else
  setErrText(out_err, out_err_size, "SD profile not configured for this board");
  return false;
#endif

  if (sd_cs >= 0) {
    pinMode(sd_cs, OUTPUT);
    digitalWrite(sd_cs, HIGH);
  }
  if (lora_cs >= 0) {
    pinMode(lora_cs, OUTPUT);
    digitalWrite(lora_cs, HIGH);
  }
  if (tft_cs >= 0) {
    pinMode(tft_cs, OUTPUT);
    digitalWrite(tft_cs, HIGH);
  }

  // Avoid duplicate VFS registration when another path has already mounted SD.
  if (SD.cardType() != CARD_NONE) {
    setErrText(out_err, out_err_size, "");
    return true;
  }

  SD.end();
  clearSdVfsRegistration();
  SPI.end();
  delay(6);
  SPI.begin(sd_sck, sd_miso, sd_mosi);

#if defined(DEVICE_TLORA_PAGER_TFT)
  static const int kProfiles[] = {3, 2, 0, 1, 4};
  static const uint32_t kSpeeds[] = {4000000UL, 1000000UL, 400000UL};
  for (size_t pi = 0; pi < (sizeof(kProfiles) / sizeof(kProfiles[0])); pi++) {
    if (!pagerPrepareSdPower(kProfiles[pi], out_err, out_err_size)) {
      continue;
    }
    delay(12);
    for (size_t si = 0; si < (sizeof(kSpeeds) / sizeof(kSpeeds[0])); si++) {
      SD.end();
      clearSdVfsRegistration();
      SPI.end();
      delay(6);
      SPI.begin(sd_sck, sd_miso, sd_mosi);
      if (kPagerBootDiag) {
        pagerDiagLog("sd try profile=%d speed=%lu", kProfiles[pi],
                     static_cast<unsigned long>(kSpeeds[si]));
      }
      if (!SD.begin(sd_cs, SPI, kSpeeds[si])) {
        SD.end();
        clearSdVfsRegistration();
        SPI.end();
        delay(6);
        continue;
      }
      if (SD.cardType() == CARD_NONE) {
        SD.end();
        clearSdVfsRegistration();
        SPI.end();
        delay(6);
        continue;
      }
      if (kPagerBootDiag) {
        pagerDiagLog("sd mounted profile=%d speed=%lu", kProfiles[pi],
                     static_cast<unsigned long>(kSpeeds[si]));
      }
      setErrText(out_err, out_err_size, "");
      return true;
    }
    delay(8);
  }
  SD.end();
  clearSdVfsRegistration();
  SPI.end();
  delay(6);
  setErrText(out_err, out_err_size, "SD mount failed");
  return false;
#else
  if (!SD.begin(sd_cs, SPI, kCfgSdClockHz)) {
    // Recover from stale mount state by tearing down and retrying once.
    SD.end();
    clearSdVfsRegistration();
    SPI.end();
    delay(6);
    SPI.begin(sd_sck, sd_miso, sd_mosi);
    if (!SD.begin(sd_cs, SPI, kCfgSdClockHz)) {
      SD.end();
      clearSdVfsRegistration();
      SPI.end();
      delay(6);
      setErrText(out_err, out_err_size, "SD mount failed");
      return false;
    }
  }
#endif

  if (SD.cardType() == CARD_NONE) {
    setErrText(out_err, out_err_size, "No SD card");
    return false;
  }

  setErrText(out_err, out_err_size, "");
  return true;
#endif
}

bool loadConfigTextFromSd(String* out_text) {
  if (!out_text) {
    return false;
  }

  char sd_err[64] = {};
  if (!sdBeginForCurrentBoard(sd_err, sizeof(sd_err))) {
    return false;
  }

  File file = SD.open(kCfgSdPath, FILE_READ);
  if (!file) {
    file = SD.open(kCfgSdPathFallback, FILE_READ);
  }
  if (!file) {
    return false;
  }

  out_text->clear();
  out_text->reserve(4096);
  while (file.available()) {
    *out_text += static_cast<char>(file.read());
  }
  file.close();
  return out_text->length() > 0;
}

bool configHasIdentityKeys(const String& text) {
  return text.indexOf("identity_public_key:") >= 0 && text.indexOf("identity_private_key:") >= 0;
}

void initialize_lvgl() {
  lv_init();
  if (false) Serial.println("[LVGL] Initialized");
}

void sync_ui_channels_from_mesh() {
  static uint32_t last_sync_ms = 0;
  static uint8_t cached_count = 0;
  static char cached_names[kUiChannelCapacity][32]{};

  const uint32_t now = millis();
  if (now - last_sync_ms < 1000) {
    return;
  }
  last_sync_ms = now;

  memset(g_channel_names_buf, 0, sizeof(g_channel_names_buf));
  const int exported = g_mesh.exportChannels(g_channel_names_buf, kUiChannelCapacity);
  const uint8_t count = exported > 0 ? static_cast<uint8_t>(exported) : static_cast<uint8_t>(0);

  bool changed = count != cached_count;
  if (!changed) {
    for (uint8_t i = 0; i < count; i++) {
      if (strcmp(cached_names[i], g_channel_names_buf[i]) != 0) {
        changed = true;
        break;
      }
    }
  }

  if (!changed) {
    return;
  }

  g_ui.setChannels(g_channel_names_buf, count);

  cached_count = count;
  memset(cached_names, 0, sizeof(cached_names));
  for (uint8_t i = 0; i < count; i++) {
    strncpy(cached_names[i], g_channel_names_buf[i], sizeof(cached_names[i]) - 1);
    cached_names[i][sizeof(cached_names[i]) - 1] = '\0';
  }
}

void sync_ui_wifi_state() {
  static uint32_t last_sync_ms = 0;
  static bool cached_config_server_on = false;
  static bool cached_sta_connected = false;
  static bool cached_ap_mode = false;

  const uint32_t now = millis();
  if (now - last_sync_ms < 500) {
    return;
  }
  last_sync_ms = now;

  const bool config_server_on = plumeria::web::running();
  const char* net_mode = plumeria::web::mode();
  const bool sta_connected = config_server_on && net_mode && strcmp(net_mode, "sta") == 0;
  const bool ap_mode = config_server_on && net_mode && strcmp(net_mode, "ap") == 0;

  if (config_server_on == cached_config_server_on && sta_connected == cached_sta_connected &&
      ap_mode == cached_ap_mode) {
    return;
  }

  cached_config_server_on = config_server_on;
  cached_sta_connected = sta_connected;
  cached_ap_mode = ap_mode;
  g_ui.setWifiState(config_server_on, sta_connected, ap_mode);
}

}  // namespace

void setup() {
  Serial.begin(115200);

#if defined(DEVICE_HELTEC_V4_EXPANSION)
  // Let USB CDC settle on S3 boards so early boot logs are visible during debug.
  delay(250);
  Serial.printf("[BOOT] setup enter reset_reason=%d\n", static_cast<int>(esp_reset_reason()));
#endif

  if (kPagerBootDiag) {
    const uint32_t start_wait = millis();
    while (!Serial && (millis() - start_wait) < 2500) {
      delay(10);
    }
    delay(80);
    pagerDiagLog("setup enter");
  }

  if (plumeria::config::kCompanionEnabled) {
    if (false) Serial.println("[BOOT] Companion mode requested but disabled in this firmware.");
  }

  #if defined(DEVICE_HELTEC_V4_EXPANSION)
  Serial.println("[BOOT] lvgl init start");
  #endif
  initialize_lvgl();
  #if defined(DEVICE_HELTEC_V4_EXPANSION)
  Serial.println("[BOOT] lvgl init done");
  #endif
  pagerDiagLog("lvgl init done");

  #if defined(DEVICE_HELTEC_V4_EXPANSION)
  Serial.println("[BOOT] board begin start");
  #endif
  bool board_ready = g_board.begin();
  #if defined(DEVICE_HELTEC_V4_EXPANSION)
  Serial.printf("[BOOT] board begin done=%d\n", board_ready ? 1 : 0);
  Serial.println("[BOOT] display begin start");
  #endif
  bool display_ready = g_display.begin();
  #if defined(DEVICE_HELTEC_V4_EXPANSION)
  Serial.printf("[BOOT] display begin done=%d\n", display_ready ? 1 : 0);
  #endif
  pagerDiagLog("hal ready board=%d display=%d", board_ready ? 1 : 0, display_ready ? 1 : 0);
  if (false) Serial.printf("[BOOT] board_ready=%d display_ready=%d\n", board_ready ? 1 : 0, display_ready ? 1 : 0);
  bool mesh_ready = false;

  plumeria::web::loadSettings(&g_web_settings);
  plumeria::hal::RadioConfig radio_cfg = g_board.defaultRadioConfig();
  plumeria::web::applyRadioProfile(&radio_cfg, g_web_settings);

  if (board_ready && display_ready) {
    mesh_ready = g_mesh.begin(radio_cfg);
  }
  pagerDiagLog("mesh ready=%d", mesh_ready ? 1 : 0);

  if (mesh_ready) {
    memset(g_channel_names_buf, 0, sizeof(g_channel_names_buf));
    const int channel_count = g_mesh.exportChannels(g_channel_names_buf, kUiChannelCapacity);
    g_ui.setChannels(g_channel_names_buf, channel_count > 0 ? static_cast<size_t>(channel_count) : 0);

    char pub_hex[65] = {};
    if (g_mesh.getPublicKeyHex(pub_hex, sizeof(pub_hex))) {
      if (false) Serial.printf("[BOOT][ID] pubkey=%s\n", pub_hex);
    } else {
      if (false) Serial.println("[BOOT][ID] pubkey=unavailable");
    }
  }

  const bool web_ready = plumeria::web::begin(&g_mesh, g_web_settings);
  pagerDiagLog("web begin=%d mode=%s ip=%s", web_ready ? 1 : 0,
               plumeria::web::mode(), plumeria::web::ip());

  bool first_install_identity_prompt = false;
  if (mesh_ready && !g_mesh.identityLoadedFromNvs()) {
    String cfg_text;
    if (loadConfigTextFromSd(&cfg_text) && configHasIdentityKeys(cfg_text)) {
      char err[96] = {};
      if (plumeria::web::importConfigText(cfg_text.c_str(), false, err, sizeof(err))) {
        delay(120);
        ESP.restart();
      }
    }

    if (!g_mesh.identityLoadedFromNvs()) {
      first_install_identity_prompt = true;
    }
  }

  g_ui.attachMeshAdapter(&g_mesh);
  g_ui.setFirstInstallIdentityPrompt(first_install_identity_prompt);
  const bool ui_ready = g_ui.begin();
  pagerDiagLog("ui begin=%d", ui_ready ? 1 : 0);
  if (false) Serial.printf("[BOOT] ui_ready=%d\n", ui_ready ? 1 : 0);
  g_ui.setMeshReady(mesh_ready);
  sync_ui_wifi_state();
  pagerDiagLog("setup complete");
}

void loop() {
  g_board.loop();
  g_display.loop();
  g_mesh.loop();
  sync_ui_channels_from_mesh();
  plumeria::web::loop();
  sync_ui_wifi_state();

  plumeria::mesh::MeshEvent events[16];
  while (true) {
    size_t event_count = g_mesh.drainEvents(events, 16);
    if (event_count == 0) {
      break;
    }
    for (size_t i = 0; i < event_count; i++) {
      g_ui.applyEvent(events[i]);
    }
    if (event_count < 16) {
      break;
    }
  }

  g_ui.loop();

  lv_tick_inc(kLvglTickMs);
  lv_timer_handler();
  delay(kLvglTickMs);
}
