#pragma once

#include "hal/tlora_pager_board.h"
#include "mesh/mesh_adapter.h"

namespace plumeria {
namespace web {

struct WebSettings {
  char wifi_ssid[64];
  char wifi_pass[64];
  char region[24];
  float lora_freq_mhz;
  float lora_bw_khz;
  uint8_t lora_sf;
  uint8_t lora_cr;
  int8_t lora_tx_power_dbm;
};

void loadSettings(WebSettings* out_settings);
void applyRadioProfile(hal::TloraPagerRadioConfig* radio_config, const WebSettings& settings);

bool begin(mesh::MeshAdapter* mesh_adapter, const WebSettings& initial_settings);
void loop();
void end();

bool running();
const char* mode();
const char* ip();

}  // namespace web
}  // namespace plumeria
