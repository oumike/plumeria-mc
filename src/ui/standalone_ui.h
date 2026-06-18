#pragma once

#include "mesh/mesh_adapter.h"

#include <lvgl.h>

namespace plumeria {
namespace ui {

class StandaloneUi {
 public:
  bool begin();
  void loop();

  void attachMeshAdapter(mesh::MeshAdapter* adapter);
  void setFirstInstallIdentityPrompt(bool enabled);
  void setChannels(const char names[][32], size_t count);
  void setMeshReady(bool ready);
  void setWifiState(bool config_server_on, bool sta_connected, bool ap_mode);
  void applyEvent(const mesh::MeshEvent& event);

 private:
  enum class FocusZone : uint8_t {
    Selector,
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

  static constexpr uint8_t kChannelCount = 40;
  static constexpr uint8_t kShortcutCount = 3;
#if defined(DEVICE_HELTEC_V4_EXPANSION)
  static constexpr uint8_t kCfgRowCount = 4;
#else
  static constexpr uint8_t kCfgRowCount = 7;
#endif
  static constexpr uint8_t kContactActionCount = 2;
  static constexpr uint8_t kMaxContactsUi = 8;
  static constexpr size_t kMaxChatRows = 96;
  static constexpr size_t kMaxStoredChatRows = 128;

  bool createStyles();
  void buildLayout();
  void bindInputGroup();

  void refreshChannelVisuals();
  void refreshSelectorVisuals();
  void refreshDropdownVisuals();
  void refreshShortcutVisuals();
  void refreshHeaderVisuals();
  void refreshUnreadPulse(uint32_t now_ms);
  void refreshComposeDialog();
  void refreshClockIfNeeded(uint32_t now_ms);
  void syncChannelsFromMeshIfNeeded(uint32_t now_ms);

  void openChannelDropdown();
  void closeChannelDropdown(bool keep_highlight = false);
  void moveDropdownHighlight(int delta);
  void openComposeDialog();
  void closeComposeDialog(bool restore_chat_focus);
  bool handleComposeKey(uint32_t key);
  void openCfgDialog();
  void closeCfgDialog(bool focus_chat = false);
  void refreshCfgDialog();
  void moveCfgSelection(int delta);
  void activateCfgSelection();
  bool openContactsDialog();
  bool ensureContactsDialogBuilt();
  void closeContactsDialog(bool focus_chat = false);
  void refreshContactsDialog(bool reload_from_mesh = true);
  void moveContactsSelection(int delta);
  void activateContactsAction(uint8_t action_idx);
  void openDmDialog(const char* contact_name, const char* contact_key);
  void closeDmDialog(bool focus_chat = false);
  void appendDmLine(const char* contact_name, const char* contact_key, const char* text, ChatLineKind kind);
  void rebuildDmDialog();
  void clearDmPanel();
  void openHelpDialog();
  void closeHelpDialog();
  void triggerAdvertZeroHop();
  void triggerAdvertFlood();
  void openAdvertPopup(const char* text, bool is_error);
  void closeAdvertPopup();
  bool setGpsEnabled(bool enabled);
  bool exportConfigToSd();
  bool importConfigFromSd();
  bool deleteConfigFromSd();
  void openIdentityNamePrompt();
  bool applyIdentityNameFromPrompt();
  bool sendComposeMessage();
  void scrollChatUp();
  void scrollChatDown();

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
  bool loadChatHistoryFromFs();
  bool saveChatHistoryToFs();
  bool loadDmHistoryFromFs();
  bool saveDmHistoryToFs();
  bool pruneStoredDmByRetention(uint32_t now_epoch);

  void handleKey(uint32_t key);
  void handleClick(lv_obj_t* target);
  void showComposeKeyboard();
  void hideComposeKeyboard();

  static void onFocusableEvent(lv_event_t* event);
  static void onComposeKeyboardEvent(lv_event_t* event);

  lv_obj_t* root_ = nullptr;
  lv_obj_t* main_panel_ = nullptr;

  lv_obj_t* header_bar_ = nullptr;
  lv_obj_t* channel_selector_btn_ = nullptr;
  lv_obj_t* channel_selector_label_ = nullptr;
  lv_obj_t* channel_selector_caret_ = nullptr;
  lv_obj_t* channel_dropdown_panel_ = nullptr;
  lv_obj_t* channel_dropdown_list_ = nullptr;
  lv_obj_t* channel_dropdown_rows_[kChannelCount]{};
  lv_obj_t* channel_dropdown_labels_[kChannelCount]{};
  lv_obj_t* gps_label_ = nullptr;
  lv_obj_t* wifi_label_ = nullptr;
  lv_obj_t* wifi_ap_badge_label_ = nullptr;
  lv_obj_t* time_label_ = nullptr;
  lv_obj_t* battery_pct_label_ = nullptr;
  lv_obj_t* battery_bar_ = nullptr;

  lv_obj_t* chat_panel_ = nullptr;
  lv_obj_t* chat_advz_btn_ = nullptr;
  lv_obj_t* chat_advz_label_ = nullptr;
  lv_obj_t* chat_advf_btn_ = nullptr;
  lv_obj_t* chat_advf_label_ = nullptr;
  lv_obj_t* chat_new_btn_ = nullptr;
  lv_obj_t* chat_new_label_ = nullptr;
  lv_obj_t* chat_rows_[kMaxChatRows]{};
  size_t chat_row_count_ = 0;
  lv_obj_t* compose_dialog_ = nullptr;
  lv_obj_t* compose_title_label_ = nullptr;
  lv_obj_t* compose_hint_label_ = nullptr;
  lv_obj_t* compose_input_ = nullptr;
  lv_obj_t* compose_keyboard_ = nullptr;
  lv_obj_t* cfg_dialog_ = nullptr;
  lv_obj_t* cfg_title_label_ = nullptr;
  lv_obj_t* cfg_action_label_ = nullptr;
  lv_obj_t* cfg_status_label_ = nullptr;
  lv_obj_t* cfg_close_btn_ = nullptr;
  lv_obj_t* cfg_close_label_ = nullptr;
  lv_obj_t* cfg_rows_[kCfgRowCount]{};
  lv_obj_t* cfg_row_labels_[kCfgRowCount]{};
  lv_obj_t* contacts_dialog_ = nullptr;
  lv_obj_t* contacts_title_label_ = nullptr;
  lv_obj_t* contacts_status_label_ = nullptr;
  lv_obj_t* contacts_close_btn_ = nullptr;
  lv_obj_t* contacts_close_label_ = nullptr;
  lv_obj_t* contacts_nodes_panel_ = nullptr;
  lv_obj_t* contacts_node_rows_[kMaxContactsUi]{};
  lv_obj_t* contacts_node_labels_[kMaxContactsUi]{};
  lv_obj_t* contacts_detail_panel_ = nullptr;
  lv_obj_t* contacts_action_rows_[kContactActionCount]{};
  lv_obj_t* contacts_action_labels_[kContactActionCount]{};
  lv_obj_t* contacts_full_name_label_ = nullptr;
  lv_obj_t* contacts_lat_lon_label_ = nullptr;
  lv_obj_t* contacts_last_heard_label_ = nullptr;
  lv_obj_t* contacts_telemetry_label_ = nullptr;
  lv_obj_t* dm_dialog_ = nullptr;
  lv_obj_t* dm_title_label_ = nullptr;
  lv_obj_t* dm_new_btn_ = nullptr;
  lv_obj_t* dm_new_label_ = nullptr;
  lv_obj_t* dm_panel_ = nullptr;
  lv_obj_t* dm_close_btn_ = nullptr;
  lv_obj_t* dm_close_label_ = nullptr;
  lv_obj_t* dm_rows_[kMaxChatRows]{};
  size_t dm_row_count_ = 0;
  lv_obj_t* help_dialog_ = nullptr;
  lv_obj_t* help_title_label_ = nullptr;
  lv_obj_t* help_body_label_ = nullptr;
  lv_obj_t* help_close_btn_ = nullptr;
  lv_obj_t* help_close_label_ = nullptr;
  lv_obj_t* advert_popup_ = nullptr;
  lv_obj_t* advert_popup_label_ = nullptr;

  lv_obj_t* shortcut_strip_ = nullptr;
  lv_obj_t* shortcut_btns_[kShortcutCount]{};
  lv_obj_t* shortcut_labels_[kShortcutCount]{};

  lv_group_t* key_group_ = nullptr;

  lv_style_t style_root_;
  lv_style_t style_panel_;
  lv_style_t style_header_;
  lv_style_t style_chat_;
  lv_style_t style_chat_focused_;
  lv_style_t style_button_;
  lv_style_t style_button_focused_;
  lv_style_t style_button_active_;
  lv_style_t style_selector_anchor_;
  lv_style_t style_dropdown_panel_;
  lv_style_t style_dropdown_highlight_;
  lv_style_t style_dropdown_active_;
  lv_style_t style_shortcut_active_;
  lv_style_t style_unread_edge_;
  lv_style_t style_text_main_;
  lv_style_t style_text_dim_;
  lv_style_t style_msg_rx_;
  lv_style_t style_msg_tx_;
  lv_style_t style_msg_ack_;
  lv_style_t style_msg_err_;

  bool styles_ready_ = false;
  bool started_ = false;
  bool channel_dropdown_open_ = false;
  bool compose_open_ = false;
  bool compose_dm_mode_ = false;
  bool compose_return_to_dm_ = false;
  bool cfg_open_ = false;
  bool contacts_open_ = false;
  bool dm_open_ = false;
  bool help_open_ = false;
  bool advert_popup_open_ = false;
  bool has_unread_dm_ = false;
  bool contacts_actions_focused_ = false;
  bool cfg_import_confirm_armed_ = false;
  bool cfg_delete_confirm_armed_ = false;
  bool first_install_identity_prompt_ = false;
  bool first_install_auto_export_pending_ = false;
  bool identity_prompt_open_ = false;
  uint8_t pending_chat_focus_attempts_ = 0;
  uint8_t dropdown_highlight_channel_ = 0;
  uint8_t cfg_selected_row_ = 0;
  uint8_t contacts_selected_index_ = 0;
  uint8_t contacts_action_index_ = 0;
  uint8_t contacts_count_ = 0;
  uint8_t contacts_row_capacity_ = 0;
  uint32_t last_selector_action_ms_ = 0;
  uint32_t last_dropdown_open_ms_ = 0;
  uint32_t advert_popup_deadline_ms_ = 0;
  char cfg_action_text_[48]{};
  char cfg_status_text_[96]{};
  char contacts_status_text_[96]{};

  FocusZone focus_zone_ = FocusZone::Selector;
  uint8_t selected_channel_ = 0;
  uint8_t active_channel_ = 0;
  uint8_t selected_shortcut_ = 0;
  uint8_t configured_channel_count_ = 0;
  char configured_channel_names_[kChannelCount][32]{};
  char compose_target_channel_[32]{};
  char compose_target_dm_pubkey_[65]{};
  char dm_active_name_[32]{};
  char dm_active_key_[65]{};
  char last_dm_sender_name_[32]{};
  char last_dm_sender_key_[65]{};
  char pending_local_echo_channel_[32]{};
  char pending_local_echo_text_[96]{};
  uint32_t pending_local_echo_deadline_ms_ = 0;
  bool unread_channels_[kChannelCount]{};
  mesh::MeshContactSummary contacts_cache_[kMaxContactsUi]{};

  mesh::MeshAdapter* mesh_adapter_ = nullptr;

  struct StoredDmLine {
    char contact_name[32];
    char contact_key[65];
    char text[96];
    ChatLineKind kind;
    uint32_t timestamp_epoch;
  };
  StoredDmLine stored_dm_[kMaxStoredChatRows]{};
  size_t stored_dm_head_ = 0;
  size_t stored_dm_count_ = 0;

  struct StoredChatLine {
    char channel_name[32];
    char text[96];
    ChatLineKind kind;
  };
  StoredChatLine stored_chat_[kMaxStoredChatRows]{};
  size_t stored_chat_head_ = 0;
  size_t stored_chat_count_ = 0;
  bool chat_history_dirty_ = false;
  uint32_t last_chat_persist_ms_ = 0;
  bool dm_history_dirty_ = false;
  uint32_t last_dm_persist_ms_ = 0;
  uint32_t last_dm_retention_prune_ms_ = 0;

  bool mesh_ready_ = false;
  bool gps_ok_ = false;
  bool wifi_ok_ = false;
  bool wifi_config_server_on_ = false;
  bool wifi_ap_mode_ = false;
  uint8_t battery_pct_ = 100;

  uint32_t last_clock_update_ms_ = 0;
  uint16_t last_clock_minute_ = 0xFFFF;
  uint32_t last_channel_sync_ms_ = 0;
  uint32_t last_unread_pulse_ms_ = 0;
};

}  // namespace ui
}  // namespace plumeria
