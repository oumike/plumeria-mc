#pragma once

#include "mesh/mesh_adapter.h"

#include <lvgl.h>

namespace plumeria {
namespace ui {

class StandaloneUi {
 public:
  bool begin();
  void loop();

  void setChannels(const char names[][32], size_t count);
  void setMeshReady(bool ready);
  void applyEvent(const mesh::MeshEvent& event);

 private:
  enum class FocusZone : uint8_t {
    Rail,
    Chat,
    Shortcuts,
  };

  enum class ChatLineKind : uint8_t {
    Normal,
    Rx,
    Tx,
    Ack,
    Error,
  };

  static constexpr uint8_t kChannelCount = 8;
  static constexpr uint8_t kShortcutCount = 5;
  static constexpr size_t kMaxChatRows = 96;
  static constexpr size_t kMaxStoredChatRows = 128;

  bool createStyles();
  void buildLayout();
  void bindInputGroup();

  void refreshChannelVisuals();
  void refreshShortcutVisuals();
  void refreshHeaderVisuals();
  void refreshClockIfNeeded(uint32_t now_ms);

  void setFocusZone(FocusZone zone);
  void focusPrevZone();
  void focusNextZone();
  void focusCurrentZoneObject();

  void selectChannel(int index, bool activate);
  void triggerShortcut(uint8_t index);

  void appendChatLine(const char* text, ChatLineKind kind);
  int findConfiguredChannelIndex(const char* channel_name) const;
  void pushChannelHistoryLine(const char* channel_name, const char* text, ChatLineKind kind);
  void rebuildChatForActiveChannel();
  void clearChatPanel();

  void handleKey(uint32_t key);
  void handleClick(lv_obj_t* target);

  static void onFocusableEvent(lv_event_t* event);

  lv_obj_t* root_ = nullptr;
  lv_obj_t* rail_panel_ = nullptr;
  lv_obj_t* main_panel_ = nullptr;

  lv_obj_t* channel_btns_[kChannelCount]{};
  lv_obj_t* channel_labels_[kChannelCount]{};
  lv_obj_t* channel_unread_dots_[kChannelCount]{};

  lv_obj_t* header_bar_ = nullptr;
  lv_obj_t* gps_label_ = nullptr;
  lv_obj_t* wifi_label_ = nullptr;
  lv_obj_t* time_label_ = nullptr;
  lv_obj_t* battery_pct_label_ = nullptr;
  lv_obj_t* battery_bar_ = nullptr;

  lv_obj_t* chat_panel_ = nullptr;
  lv_obj_t* chat_rows_[kMaxChatRows]{};
  size_t chat_row_count_ = 0;

  lv_obj_t* shortcut_strip_ = nullptr;
  lv_obj_t* shortcut_btns_[kShortcutCount]{};
  lv_obj_t* shortcut_labels_[kShortcutCount]{};

  lv_group_t* key_group_ = nullptr;

  lv_style_t style_root_;
  lv_style_t style_panel_;
  lv_style_t style_header_;
  lv_style_t style_chat_;
  lv_style_t style_button_;
  lv_style_t style_button_focused_;
  lv_style_t style_button_active_;
  lv_style_t style_shortcut_active_;
  lv_style_t style_text_main_;
  lv_style_t style_text_dim_;
  lv_style_t style_msg_rx_;
  lv_style_t style_msg_tx_;
  lv_style_t style_msg_ack_;
  lv_style_t style_msg_err_;

  bool styles_ready_ = false;
  bool started_ = false;

  FocusZone focus_zone_ = FocusZone::Rail;
  uint8_t selected_channel_ = 0;
  uint8_t active_channel_ = 0;
  uint8_t selected_shortcut_ = 0;
  uint8_t configured_channel_count_ = 0;
  char configured_channel_names_[kChannelCount][32]{};
  bool unread_channels_[kChannelCount]{};

  struct StoredChatLine {
    char channel_name[32];
    char text[96];
    ChatLineKind kind;
  };
  StoredChatLine stored_chat_[kMaxStoredChatRows]{};
  size_t stored_chat_head_ = 0;
  size_t stored_chat_count_ = 0;

  bool mesh_ready_ = false;
  bool gps_ok_ = false;
  bool wifi_ok_ = false;
  uint8_t battery_pct_ = 100;

  uint32_t last_clock_update_ms_ = 0;
  uint16_t last_clock_minute_ = 0xFFFF;
};

}  // namespace ui
}  // namespace plumeria
