#include <Arduino.h>
#include <lvgl.h>
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

plumeria::hal::DeviceBoard g_board;
plumeria::hal::DeviceLvgl g_display;
plumeria::mesh::MeshAdapter g_mesh;
plumeria::ui::StandaloneUi g_ui;
plumeria::web::WebSettings g_web_settings{};
char g_channel_names_buf[kUiChannelCapacity][32]{};

void initialize_lvgl() {
  lv_init();
  Serial.println("[LVGL] Initialized");
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
  plumeria::hal::RadioConfig radio_cfg = g_board.defaultRadioConfig();
  plumeria::web::applyRadioProfile(&radio_cfg, g_web_settings);

  if (board_ready && display_ready) {
    mesh_ready = g_mesh.begin(radio_cfg);
  }

  if (mesh_ready) {
    memset(g_channel_names_buf, 0, sizeof(g_channel_names_buf));
    const int channel_count = g_mesh.exportChannels(g_channel_names_buf, kUiChannelCapacity);
    g_ui.setChannels(g_channel_names_buf, channel_count > 0 ? static_cast<size_t>(channel_count) : 0);
  }

  g_ui.attachMeshAdapter(&g_mesh);
  g_ui.begin();
  g_ui.setMeshReady(mesh_ready);

  plumeria::web::begin(&g_mesh, g_web_settings);
}

void loop() {
  g_board.loop();
  g_display.loop();
  g_mesh.loop();
  sync_ui_channels_from_mesh();
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
