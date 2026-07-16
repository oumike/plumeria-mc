#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <esp_vfs_fat.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lvgl.h>
#include <stdarg.h>
#include <string.h>

#ifndef PLUMERIA_OTA_ENABLED
#define PLUMERIA_OTA_ENABLED 1
#endif

#include "config/features.h"
#include "hal/device_board.h"
#include "hal/device_lvgl.h"
#include "mesh/mesh_adapter.h"
#if PLUMERIA_OTA_ENABLED
#include "ota/ota_boot_mode.h"
#include "ota/ota_update.h"
#endif
#include "ui/standalone_ui.h"
#include "web/web_config.h"

namespace {

constexpr uint32_t kLvglTickMs = 5;
constexpr int kUiChannelCapacity = 40;
constexpr char kPublicChannelName[] = "Public";
constexpr char kCfgSdPath[] = "/plumeria/plumeria-config.yaml";
constexpr char kCfgSdPathFallback[] = "/plumeria-config.yaml";
constexpr uint32_t kCfgSdClockHz = 800000UL;
constexpr uint32_t kOtaBootWifiTimeoutMs = 20000;
constexpr uint32_t kMeshBeginTimeoutMs = 12000;
constexpr uint32_t kMeshRetryIntervalMs = 15000;
constexpr uint32_t kMeshRetryCreateFailBackoffMs = 30000;

#if defined(DEVICE_TLORA_PAGER_TFT)
constexpr bool kPagerBootDiag = false;
#else
constexpr bool kPagerBootDiag = false;
#endif

constexpr bool kOtaBootEnableVisuals = true;

#if defined(DEVICE_HELTEC_V4_EXPANSION)
constexpr const char* kBuildTargetName = "heltec-v4-expansion";
#elif defined(DEVICE_TDECK)
constexpr const char* kBuildTargetName = "tdeck";
#elif defined(DEVICE_TLORA_PAGER_TFT)
constexpr const char* kBuildTargetName = "tlora-pager-tft";
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
constexpr const char* kBuildTargetName = "cardputer-cap";
#else
constexpr const char* kBuildTargetName = "unknown";
#endif

plumeria::hal::DeviceBoard g_board;
plumeria::hal::DeviceLvgl g_display;
plumeria::mesh::MeshAdapter g_mesh;
plumeria::ui::StandaloneUi g_ui;
plumeria::web::WebSettings g_web_settings{};
plumeria::hal::RadioConfig g_radio_cfg{};
bool g_mesh_ready = false;
uint32_t g_next_mesh_retry_ms = 0;
char g_channel_names_buf[kUiChannelCapacity][32]{};

void applyWebSettingsToMeshRuntime();
void publishMeshChannelsToUi();

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

#if defined(DEVICE_HELTEC_V4_EXPANSION)
struct MeshBeginTaskCtx {
  plumeria::mesh::MeshAdapter* adapter;
  plumeria::hal::RadioConfig radio_cfg;
  volatile bool finished;
  bool result;
};

struct MeshBeginRetryState {
  MeshBeginTaskCtx ctx;
  TaskHandle_t task_handle;
  uint32_t started_ms;
  bool active;
};

MeshBeginRetryState g_mesh_retry_state{};

void meshBeginTask(void* arg) {
  auto* ctx = static_cast<MeshBeginTaskCtx*>(arg);
  if (!ctx || !ctx->adapter) {
    vTaskDelete(nullptr);
    return;
  }

  ctx->result = ctx->adapter->begin(ctx->radio_cfg);
  ctx->finished = true;
  vTaskDelete(nullptr);
}

bool startMeshBeginTask(MeshBeginRetryState* state,
                       plumeria::mesh::MeshAdapter* adapter,
                       const plumeria::hal::RadioConfig& radio_cfg) {
  if (!state || !adapter || state->active) {
    return false;
  }

  memset(state, 0, sizeof(*state));
  state->ctx.adapter = adapter;
  state->ctx.radio_cfg = radio_cfg;

  const BaseType_t created = xTaskCreatePinnedToCore(meshBeginTask,
                                                      "mesh_begin",
                                                      12288,
                                                      &state->ctx,
                                                      1,
                                                      &state->task_handle,
                                                      tskNO_AFFINITY);
  if (created != pdPASS) {
    state->task_handle = nullptr;
    return false;
  }

  state->started_ms = millis();
  state->active = true;
  return true;
}

bool beginMeshWithTimeout(plumeria::mesh::MeshAdapter* adapter,
                          const plumeria::hal::RadioConfig& radio_cfg,
                          uint32_t timeout_ms,
                          bool* out_timed_out) {
  if (!adapter) {
    if (out_timed_out) {
      *out_timed_out = false;
    }
    return false;
  }

  MeshBeginRetryState state{};
  if (!startMeshBeginTask(&state, adapter, radio_cfg)) {
    if (out_timed_out) {
      *out_timed_out = false;
    }
    Serial.println("[BOOT] mesh task create failed");
    adapter->resetRuntime();
    return false;
  }

  const uint32_t start_ms = millis();
  while (!state.ctx.finished && (millis() - start_ms) < timeout_ms) {
    delay(10);
  }

  if (state.ctx.finished) {
    if (out_timed_out) {
      *out_timed_out = false;
    }
    state.active = false;
    state.task_handle = nullptr;
    return state.ctx.result;
  }

  if (out_timed_out) {
    *out_timed_out = true;
  }
  Serial.printf("[BOOT] mesh begin timeout after %lu ms\n", static_cast<unsigned long>(timeout_ms));
  if (state.task_handle) {
    vTaskDelete(state.task_handle);
    state.task_handle = nullptr;
  }
  state.active = false;
  adapter->resetRuntime();
  return false;
}

void serviceMeshRetry() {
  if (g_mesh.isReady()) {
    g_mesh_ready = true;
    return;
  }

  const uint32_t now = millis();
  if (g_mesh_retry_state.active) {
    if (g_mesh_retry_state.ctx.finished) {
      const bool retry_ok = g_mesh_retry_state.ctx.result;
      g_mesh_retry_state.active = false;
      g_mesh_retry_state.task_handle = nullptr;

      Serial.printf("[BOOT] mesh retry done=%d timeout=0\n", retry_ok ? 1 : 0);
      g_mesh_ready = retry_ok;
      if (retry_ok) {
        applyWebSettingsToMeshRuntime();
        publishMeshChannelsToUi();
        g_ui.setMeshReady(true);
      }
      g_next_mesh_retry_ms = now + kMeshRetryIntervalMs;
      return;
    }

    if ((now - g_mesh_retry_state.started_ms) >= kMeshBeginTimeoutMs) {
      Serial.printf("[BOOT] mesh begin timeout after %lu ms\n", static_cast<unsigned long>(kMeshBeginTimeoutMs));
      if (g_mesh_retry_state.task_handle) {
        vTaskDelete(g_mesh_retry_state.task_handle);
        g_mesh_retry_state.task_handle = nullptr;
      }
      g_mesh_retry_state.active = false;
      g_mesh.resetRuntime();
      g_mesh_ready = false;
      Serial.println("[BOOT] mesh retry done=0 timeout=1");
      g_next_mesh_retry_ms = now + kMeshRetryIntervalMs;
    }
    return;
  }

  if (static_cast<int32_t>(now - g_next_mesh_retry_ms) < 0) {
    return;
  }

  Serial.println("[BOOT] mesh retry start");
  if (!startMeshBeginTask(&g_mesh_retry_state, &g_mesh, g_radio_cfg)) {
    Serial.println("[BOOT] mesh task create failed; backing off");
    g_mesh.resetRuntime();
    g_mesh_ready = false;
    g_next_mesh_retry_ms = now + kMeshRetryCreateFailBackoffMs;
  }
}
#endif

void applyWebSettingsToMeshRuntime() {
  if (!g_mesh.isReady()) {
    return;
  }

  if (g_web_settings.node_name[0] != '\0') {
    g_mesh.setNodeName(g_web_settings.node_name);
  }
  g_mesh.setAdvertLocation(g_web_settings.send_location_in_advert,
                           g_web_settings.node_latitude,
                           g_web_settings.node_longitude);
  g_mesh.setGpsEnabled(!g_web_settings.send_location_in_advert);
  g_mesh.setAutoAdvertIntervalMinutes(g_web_settings.advert_interval_minutes);
  g_mesh.setPathHashMode(g_web_settings.path_hash_mode);
  g_mesh.setMultiAck(g_web_settings.multi_ack);
  g_mesh.setRepeaterMode(g_web_settings.repeater_mode);
  g_mesh.setMeshRegion(g_web_settings.mesh_region);
}

void publishMeshChannelsToUi() {
  memset(g_channel_names_buf, 0, sizeof(g_channel_names_buf));
  const int channel_count = g_mesh.exportChannels(g_channel_names_buf, kUiChannelCapacity);
  if (channel_count > 0) {
    g_ui.setChannels(g_channel_names_buf, static_cast<size_t>(channel_count));
    return;
  }

  strncpy(g_channel_names_buf[0], kPublicChannelName, sizeof(g_channel_names_buf[0]) - 1);
  g_channel_names_buf[0][sizeof(g_channel_names_buf[0]) - 1] = '\0';
  g_ui.setChannels(g_channel_names_buf, 1);
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
  // Arduino SD defaults to "/sd" in this core, but some builds may use "/sdcard".
  // Ignore return values: unregistering a non-registered path is a no-op.
  (void)esp_vfs_fat_unregister_path("/sd");
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

#if defined(DEVICE_TLORA_PAGER_TFT)
  SD.end();
  clearSdVfsRegistration();
  SPI.end();
  delay(6);
  SPI.begin(sd_sck, sd_miso, sd_mosi);

  static const char* kMountpoints[] = {"/sd", "/sdcard"};
  static const int kProfiles[] = {3, 2, 0, 1, 4};
  static const uint32_t kSpeeds[] = {4000000UL, 1000000UL, 400000UL};
  // FATFS context allocs ~ sizeof(vfs_fat_ctx_t) + max_files*sizeof(FIL).
  // With BOARD_HAS_PSRAM enabled in platformio.ini, FATFS allocates from PSRAM
  // (CONFIG_FATFS_ALLOC_PREFER_EXTRAM); keeping max_files small still helps
  // when PSRAM is missing or saturated.
  constexpr uint8_t kSdMaxFiles = 2;
  Serial.printf("[SD][boot] enter free_heap=%lu\n",
                static_cast<unsigned long>(ESP.getFreeHeap()));
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
      Serial.printf("[SD][boot] try profile=%d speed=%lu free_heap=%lu\n",
                    kProfiles[pi],
                    static_cast<unsigned long>(kSpeeds[si]),
                    static_cast<unsigned long>(ESP.getFreeHeap()));

      bool mounted = false;
      for (size_t mi = 0; mi < (sizeof(kMountpoints) / sizeof(kMountpoints[0])); mi++) {
        Serial.printf("[SD][boot] try mountpoint=%s\n", kMountpoints[mi]);
        if (SD.begin(sd_cs, SPI, kSpeeds[si], kMountpoints[mi], kSdMaxFiles)) {
          mounted = true;
          break;
        }
        SD.end();
        clearSdVfsRegistration();
        delay(2);
      }

      if (!mounted) {
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
      Serial.printf("[SD][boot] mounted profile=%d speed=%lu\n", kProfiles[pi],
                    static_cast<unsigned long>(kSpeeds[si]));
      setErrText(out_err, out_err_size, "");
      return true;
    }
    delay(8);
  }
  SD.end();
  clearSdVfsRegistration();
  SPI.end();
  delay(6);
  Serial.printf("[SD][boot] all profiles failed free_heap=%lu\n",
                static_cast<unsigned long>(ESP.getFreeHeap()));
  setErrText(out_err, out_err_size, "SD mount failed");
  return false;
#else
  // Cardputer / T-Deck path. Mirror camillia-mt's simple mount sequence:
  // no SPI.end()/SD.end()/VFS unregister churn, no mountpoint override, no
  // max_files override. On boards without PSRAM (e.g. Cardputer) the FATFS
  // context allocates from fragmented internal DRAM, so the fewer
  // allocations we trigger before the mount, the more likely it succeeds.
#if defined(DEVICE_CARDPUTER_LORA_HAT)
  // Cardputer path: explicitly set SPI pins before SD.begin; Arduino SD can
  // otherwise probe using default bus pins and fail with cmd 0x00.
  SPI.begin(sd_sck, sd_miso, sd_mosi);
  delay(2);

  // Keep FATFS allocation small and retry once after clearing stale VFS.
  bool mounted = SD.begin(sd_cs, SPI, 1000000UL, "/sd", 1);
  if (!mounted) {
    SD.end();
    clearSdVfsRegistration();
    SPI.begin(sd_sck, sd_miso, sd_mosi);
    delay(2);
    mounted = SD.begin(sd_cs, SPI, 400000UL, "/sd", 1);
  }
#else
  SPI.begin(sd_sck, sd_miso, sd_mosi);
  delay(8);
  bool mounted = SD.begin(sd_cs, SPI, 4000000UL);
  if (!mounted) {
    mounted = SD.begin(sd_cs, SPI, 1000000UL);
  }
#endif
  if (!mounted) {
    setErrText(out_err, out_err_size, "SD mount failed");
    return false;
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

  if (!g_mesh.isReady()) {
    return;
  }

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

#if PLUMERIA_OTA_ENABLED
void otaBootProgress(size_t written_bytes, size_t total_bytes) {
  static size_t last_written_bytes = 0;
  static uint32_t last_advance_ms = 0;
  static uint32_t last_draw_ms = 0;
  static uint32_t last_log_ms = 0;

  const uint32_t now = millis();
  if (last_advance_ms == 0) {
    last_advance_ms = now;
  }

  if (written_bytes != last_written_bytes) {
    last_written_bytes = written_bytes;
    last_advance_ms = now;
  }

  const bool done = (total_bytes > 0 && written_bytes >= total_bytes);
  const bool stalled = !done && (now - last_advance_ms >= 4000UL);

  if (kOtaBootEnableVisuals && ((now - last_draw_ms) >= 250UL || done)) {
    g_display.drawBootProgress("OTABOOT: Installing",
                               stalled ? "Download stalled, waiting..." : "Downloading firmware...",
                               written_bytes,
                               total_bytes,
                               stalled);
    last_draw_ms = now;
  }

  if (now - last_log_ms < 1000 && !(total_bytes > 0 && written_bytes >= total_bytes)) {
    return;
  }
  last_log_ms = now;

  if (total_bytes > 0) {
    const unsigned pct = static_cast<unsigned>((written_bytes * 100U) / total_bytes);
    Serial.printf("[OTABOOT] download %u%% (%u/%u)\n",
                  pct,
                  static_cast<unsigned>(written_bytes),
                  static_cast<unsigned>(total_bytes));
  } else {
    Serial.printf("[OTABOOT] download %u bytes\n", static_cast<unsigned>(written_bytes));
  }
}

bool ensureOtaBootWifiConnected(const plumeria::web::WebSettings& web_settings,
                                char* out_err,
                                size_t out_err_size) {
  setErrText(out_err, out_err_size, "");
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }
  if (web_settings.wifi_ssid[0] == '\0') {
    setErrText(out_err, out_err_size, "WiFi SSID not configured");
    return false;
  }

#if defined(DEVICE_TLORA_PAGER_TFT) || defined(DEVICE_TDECK)
  WiFi.useStaticBuffers(false);
#endif

  WiFi.mode(WIFI_MODE_NULL);
  delay(10);
  WiFi.mode(WIFI_STA);
  WiFi.begin(web_settings.wifi_ssid, web_settings.wifi_pass);

  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if ((millis() - start) >= kOtaBootWifiTimeoutMs) {
      setErrText(out_err, out_err_size, "WiFi connect timeout");
      return false;
    }
    delay(100);
  }
  return true;
}

bool isOtaBootTlsLowMemError(const char* err) {
  return err && strstr(err, "TLS init failed (low memory)") != nullptr;
}

bool recycleOtaBootWifi(const plumeria::web::WebSettings& web_settings,
                        char* out_err,
                        size_t out_err_size) {
  setErrText(out_err, out_err_size, "");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_MODE_NULL);
  delay(160);
  return ensureOtaBootWifiConnected(web_settings, out_err, out_err_size);
}

void runOtaBootModeIfRequested() {
  const bool requested = plumeria::ota::consumeBootModeRequest();
#if defined(DEVICE_TDECK)
  Serial.printf("[OTABOOT] boot request=%d\n", requested ? 1 : 0);
#endif
  if (!requested) {
    return;
  }

  Serial.println("[OTABOOT] entering minimal OTA mode");
  if (kOtaBootEnableVisuals) {
    g_display.beginBootDisplay();
    g_display.drawBootStatus("OTABOOT mode", "Preparing updater...");
  }

  Serial.println("[OTABOOT] loading settings");
  plumeria::web::WebSettings web_settings{};
  plumeria::web::loadSettings(&web_settings);

  char err[160] = {};
  Serial.println("[OTABOOT] connecting WiFi");
  if (kOtaBootEnableVisuals) {
    g_display.drawBootStatus("OTABOOT mode", "Connecting WiFi...");
  }
  if (!ensureOtaBootWifiConnected(web_settings, err, sizeof(err))) {
    Serial.printf("[OTABOOT] WiFi unavailable: %s\n", err[0] ? err : "unknown");
    if (kOtaBootEnableVisuals) {
      g_display.drawBootStatus("OTA failed", err[0] ? err : "WiFi unavailable");
    }
    delay(2200);
    Serial.println("[OTABOOT] continuing normal boot");
    return;
  }

  if (kOtaBootEnableVisuals) {
    char wifi_line[64] = {};
    const IPAddress ip = WiFi.localIP();
    snprintf(wifi_line,
             sizeof(wifi_line),
             "WiFi connected: %u.%u.%u.%u",
             static_cast<unsigned>(ip[0]),
             static_cast<unsigned>(ip[1]),
             static_cast<unsigned>(ip[2]),
             static_cast<unsigned>(ip[3]));
    g_display.drawBootStatus("OTABOOT mode", wifi_line);
    delay(350);
  }

  if (kOtaBootEnableVisuals) {
    g_display.drawBootStatus("OTABOOT mode", "Checking latest release...");
  }
  Serial.println("[OTABOOT] checking latest release");
  plumeria::ota::OtaCheckResult check{};
  bool check_ok = plumeria::ota::checkLatestRelease(check) && check.ok;
  if (!check_ok && isOtaBootTlsLowMemError(check.error)) {
    Serial.println("[OTABOOT] low-mem TLS during check; retrying after WiFi recycle");
    if (recycleOtaBootWifi(web_settings, err, sizeof(err))) {
      memset(&check, 0, sizeof(check));
      check_ok = plumeria::ota::checkLatestRelease(check) && check.ok;
    } else if (err[0]) {
      Serial.printf("[OTABOOT] WiFi recycle failed: %s\n", err);
    }
  }

  if (!check_ok) {
    Serial.printf("[OTABOOT] check failed: %s\n", check.error[0] ? check.error : "unknown");
    if (kOtaBootEnableVisuals) {
      g_display.drawBootStatus("OTA failed", check.error[0] ? check.error : "Release check failed");
    }
    delay(2200);
    Serial.println("[OTABOOT] continuing normal boot");
    return;
  }

  if (!check.update_available) {
    Serial.printf("[OTABOOT] already up to date (%s)\n", APP_VERSION);
    if (kOtaBootEnableVisuals) {
      g_display.drawBootStatus("Already up to date", APP_VERSION);
    }
    delay(1400);
    Serial.println("[OTABOOT] continuing normal boot");
    return;
  }

  err[0] = '\0';
  if (kOtaBootEnableVisuals) {
    g_display.drawBootProgress("OTABOOT: Installing", check.latest_tag, 0, 0, false);
  }
  Serial.printf("[OTABOOT] installing %s\n", check.latest_tag);
  bool install_ok = plumeria::ota::installLatestRelease(check.latest_tag, err, sizeof(err), otaBootProgress);
  if (!install_ok && isOtaBootTlsLowMemError(err)) {
    Serial.println("[OTABOOT] low-mem TLS during install; retrying after WiFi recycle");
    if (recycleOtaBootWifi(web_settings, err, sizeof(err))) {
      err[0] = '\0';
      install_ok = plumeria::ota::installLatestRelease(check.latest_tag, err, sizeof(err), otaBootProgress);
    } else if (err[0]) {
      Serial.printf("[OTABOOT] WiFi recycle failed: %s\n", err);
    }
  }

  if (!install_ok) {
    Serial.printf("[OTABOOT] install failed: %s\n", err[0] ? err : "unknown");
    if (kOtaBootEnableVisuals) {
      g_display.drawBootStatus("OTA failed", err[0] ? err : "Install failed");
    }
    delay(2600);
    return;
  }

  Serial.println("[OTABOOT] install complete, rebooting");
  if (kOtaBootEnableVisuals) {
    g_display.drawBootStatus("Update complete", "Rebooting...");
  }
  delay(150);
  ESP.restart();
}
#else
void runOtaBootModeIfRequested() {
}
#endif

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.printf("[BOOT] target=%s app=%s\n", kBuildTargetName, APP_VERSION);

#if defined(DEVICE_HELTEC_V4_EXPANSION)
  // Let USB CDC settle on S3 boards so early boot logs are visible during debug.
  delay(250);
  const uint32_t usb_wait_start = millis();
  while (!Serial && (millis() - usb_wait_start) < 2500) {
    delay(10);
  }
  Serial.printf("[BOOT] setup enter reset_reason=%d\n", static_cast<int>(esp_reset_reason()));
  Serial.flush();
#endif

  if (kPagerBootDiag) {
    const uint32_t start_wait = millis();
    while (!Serial && (millis() - start_wait) < 2500) {
      delay(10);
    }
    delay(80);
    pagerDiagLog("setup enter");
  }

  runOtaBootModeIfRequested();

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

  if (display_ready) {
    g_ui.showSplash(g_web_settings.node_name, 2200);
  }

  g_display.setScreenTimeoutSeconds(g_web_settings.screen_timeout_seconds);
  g_radio_cfg = g_board.defaultRadioConfig();
  plumeria::web::applyRadioProfile(&g_radio_cfg, g_web_settings);

  if (board_ready && display_ready) {
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    Serial.println("[BOOT] mesh begin start");
    bool mesh_timed_out = false;
    mesh_ready = beginMeshWithTimeout(&g_mesh, g_radio_cfg, kMeshBeginTimeoutMs, &mesh_timed_out);
    Serial.printf("[BOOT] mesh begin done=%d timeout=%d\n", mesh_ready ? 1 : 0, mesh_timed_out ? 1 : 0);
#else
    mesh_ready = g_mesh.begin(g_radio_cfg);
#endif
  }
  pagerDiagLog("mesh ready=%d", mesh_ready ? 1 : 0);
  g_mesh_ready = mesh_ready;
  g_next_mesh_retry_ms = mesh_ready ? (millis() + kMeshRetryIntervalMs) : millis();

  if (mesh_ready) {
    applyWebSettingsToMeshRuntime();
    publishMeshChannelsToUi();

    char pub_hex[65] = {};
    if (g_mesh.getPublicKeyHex(pub_hex, sizeof(pub_hex))) {
      if (false) Serial.printf("[BOOT][ID] pubkey=%s\n", pub_hex);
    } else {
      if (false) Serial.println("[BOOT][ID] pubkey=unavailable");
    }
  } else {
    memset(g_channel_names_buf, 0, sizeof(g_channel_names_buf));
    strncpy(g_channel_names_buf[0], kPublicChannelName, sizeof(g_channel_names_buf[0]) - 1);
    g_channel_names_buf[0][sizeof(g_channel_names_buf[0]) - 1] = '\0';
    g_ui.setChannels(g_channel_names_buf, 1);
  }

  #if defined(DEVICE_HELTEC_V4_EXPANSION)
  Serial.println("[BOOT] web begin start");
  #endif
  const bool web_ready = plumeria::web::begin(&g_mesh, g_web_settings);
  #if defined(DEVICE_HELTEC_V4_EXPANSION)
  Serial.printf("[BOOT] web begin done=%d\n", web_ready ? 1 : 0);
  #endif
  pagerDiagLog("web begin=%d mode=%s ip=%s", web_ready ? 1 : 0,
               plumeria::web::mode(), plumeria::web::ip());

  bool first_install_identity_prompt = false;
  bool first_install_import_available = false;
  if (mesh_ready && !g_mesh.identityLoadedFromNvs()) {
#if !defined(DEVICE_CARDPUTER_LORA_HAT)
    // Fresh install: rather than silently importing an SD config, note that one
    // is available so the onboarding flow can offer it as a prompt.
    String cfg_text;
    if (loadConfigTextFromSd(&cfg_text) && configHasIdentityKeys(cfg_text)) {
      first_install_import_available = true;
    }
#endif
    first_install_identity_prompt = true;
  }

  g_ui.attachMeshAdapter(&g_mesh);
  g_ui.setFirstInstallImportAvailable(first_install_import_available);
  g_ui.setFirstInstallIdentityPrompt(first_install_identity_prompt);
  #if defined(DEVICE_HELTEC_V4_EXPANSION)
  Serial.println("[BOOT] ui begin start");
  #endif
  const bool ui_ready = g_ui.begin();
  #if defined(DEVICE_HELTEC_V4_EXPANSION)
  Serial.printf("[BOOT] ui begin done=%d\n", ui_ready ? 1 : 0);
  #endif
  pagerDiagLog("ui begin=%d", ui_ready ? 1 : 0);
  if (false) Serial.printf("[BOOT] ui_ready=%d\n", ui_ready ? 1 : 0);
  g_ui.setMeshReady(g_mesh_ready);
  sync_ui_wifi_state();
  pagerDiagLog("setup complete");
}

void loop() {
  static bool boot_reason_reported_late = false;
  if (!boot_reason_reported_late && millis() > 5000) {
    boot_reason_reported_late = true;
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    Serial.printf("[BOOT] late reset_reason=%d\n", static_cast<int>(esp_reset_reason()));
#endif
  }

  g_board.loop();
  g_display.setScreenTimeoutSeconds(plumeria::web::screenTimeoutSeconds());
  g_display.loop();

#if defined(DEVICE_HELTEC_V4_EXPANSION)
  serviceMeshRetry();
#endif

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
