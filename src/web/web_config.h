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
  int16_t timezone_offset_minutes;
  char region[24];
  float lora_freq_mhz;
  float lora_bw_khz;
  uint8_t lora_sf;
  uint8_t lora_cr;
  int8_t lora_tx_power_dbm;
  uint8_t path_hash_mode;
  bool multi_ack;
  uint16_t screen_timeout_seconds;
  char mesh_region[32];
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
uint16_t screenTimeoutSeconds();

}  // namespace web
}  // namespace plumeria
