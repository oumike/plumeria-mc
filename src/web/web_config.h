#pragma once

#include <Arduino.h>

#include "hal/device_board.h"
#include "mesh/mesh_adapter.h"

namespace plumeria {
namespace web {

struct WebSettings {
  char node_name[32];
  double node_latitude;
  double node_longitude;
  bool send_location_in_advert;
  uint16_t advert_interval_minutes;
  char wifi_ssid[64];
  char wifi_pass[64];
  char timezone[64];
  char timezone_posix[24];
  int16_t timezone_offset_minutes;
  char region[24];
  float lora_freq_mhz;
  float lora_bw_khz;
  uint8_t lora_sf;
  uint8_t lora_cr;
  int8_t lora_tx_power_dbm;
  uint8_t path_hash_mode;
  bool multi_ack;
  bool repeater_mode;
  bool notifications_enabled;
  uint16_t screen_timeout_seconds;
  char mesh_region[32];
  uint8_t ui_theme;  // ui::UiThemeFamily
  uint8_t ui_mode;   // ui::UiThemeMode (0=dark, 1=light)
  uint8_t chat_style;      // 0=Classic, 1=Bubble, 2=Outline
  uint8_t chat_font_size;  // 0=Small, 1=Medium, 2=Large, 3=X-Large
  bool chat_style_colors;  // Bubble/Outline tint toggle
};

void loadSettings(WebSettings* out_settings);
void applyRadioProfile(hal::RadioConfig* radio_config, const WebSettings& settings);

bool begin(mesh::MeshAdapter* mesh_adapter, const WebSettings& initial_settings);
void loop();
void end();

bool running();
const char* mode();
const char* ip();

bool exportConfigText(String* out_text);
bool importConfigText(const char* text, bool queue_reboot, char* err, size_t err_size);
bool setNodeName(const char* node_name, char* err, size_t err_size);
bool setSendLocationInAdvert(bool enabled, char* err, size_t err_size);
bool setMeshRegion(const char* region_name, char* err, size_t err_size);
bool setPathHashMode(uint8_t mode, char* err, size_t err_size);
bool setMultiAck(bool enabled, char* err, size_t err_size);
bool setRepeaterMode(bool enabled, char* err, size_t err_size);
bool setNotificationsEnabled(bool enabled, char* err, size_t err_size);
bool setUiTheme(uint8_t theme, uint8_t mode, char* err, size_t err_size);
bool setChatStyle(uint8_t style, char* err, size_t err_size);
bool setChatFontSize(uint8_t size, char* err, size_t err_size);
bool setChatStyleColors(bool enabled, char* err, size_t err_size);
// True once after the theme was changed from the web UI or a config import, so
// the on-device UI knows to repaint itself.
bool consumeUiThemeChanged();

// Region radio presets (used by first-install onboarding).
int regionPresetCount();
const char* regionPresetId(int index);
const char* defaultRegionId();
bool setRegionPreset(const char* region_id, char* err, size_t err_size);
bool setWifiCredentials(const char* ssid, const char* pass, char* err, size_t err_size);
uint16_t screenTimeoutSeconds();
bool notificationsEnabled();

}  // namespace web
}  // namespace plumeria
