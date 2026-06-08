#include <Arduino.h>
#include <lvgl.h>

#include "config/features.h"
#include "hal/tlora_pager_board.h"
#include "hal/tlora_pager_lvgl.h"
#include "mesh/mesh_adapter.h"
#include "ui/standalone_ui.h"
#include "web/web_config.h"

namespace {

constexpr uint32_t kLvglTickMs = 5;

plumeria::hal::TloraPagerBoard g_board;
plumeria::hal::TloraPagerLvgl g_display;
plumeria::mesh::MeshAdapter g_mesh;
plumeria::ui::StandaloneUi g_ui;
plumeria::web::WebSettings g_web_settings{};

void initialize_lvgl() {
  lv_init();
  Serial.println("[LVGL] Initialized");
}

}  // namespace

void setup() {
  Serial.begin(115200);

  if (plumeria::config::kCompanionEnabled) {
    Serial.println("[BOOT] Companion mode requested but disabled in this firmware.");
  }

  initialize_lvgl();

  bool board_ready = g_board.begin();
  bool display_ready = g_display.begin();
  bool mesh_ready = false;

  plumeria::web::loadSettings(&g_web_settings);
  plumeria::hal::TloraPagerRadioConfig radio_cfg = g_board.defaultRadioConfig();
  plumeria::web::applyRadioProfile(&radio_cfg, g_web_settings);

  if (board_ready && display_ready) {
    mesh_ready = g_mesh.begin(radio_cfg);
  }

  if (mesh_ready) {
    char channel_names[8][32]{};
    const int channel_count = g_mesh.exportChannels(channel_names, 8);
    g_ui.setChannels(channel_names, channel_count > 0 ? static_cast<size_t>(channel_count) : 0);
  }

  g_ui.begin();
  g_ui.setMeshReady(mesh_ready);

  plumeria::web::begin(&g_mesh, g_web_settings);
}

void loop() {
  g_board.loop();
  g_display.loop();
  g_mesh.loop();
  plumeria::web::loop();

  plumeria::mesh::MeshEvent events[4];
  size_t event_count = g_mesh.drainEvents(events, 4);
  for (size_t i = 0; i < event_count; i++) {
    g_ui.applyEvent(events[i]);
  }

  g_ui.loop();

  lv_tick_inc(kLvglTickMs);
  lv_timer_handler();
  delay(kLvglTickMs);
}
