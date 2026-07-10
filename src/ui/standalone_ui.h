#pragma once

#include "mesh/mesh_adapter.h"

#include <lvgl.h>

namespace plumeria {
namespace ui {

class StandaloneUi {
 public:
  bool begin();
  void loop();

  void showSplash(uint32_t duration_ms = 1800);
  void showSplash(const char* node_name, uint32_t duration_ms = 1800);
  void dismissSplash();

  void attachMeshAdapter(mesh::MeshAdapter* adapter);
  void setFirstInstallIdentityPrompt(bool enabled);
  void setFirstInstallImportAvailable(bool available);
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
  static constexpr uint8_t kShortcutCount = 4;
#if defined(DEVICE_HELTEC_V4_EXPANSION) && !defined(DEVICE_CARDPUTER_LORA_HAT)
  static constexpr uint8_t kCfgRowCount = 8;
#else
  static constexpr uint8_t kCfgRowCount = 11;
#endif
#if defined(DEVICE_HELTEC_V4_EXPANSION) && !defined(DEVICE_CARDPUTER_LORA_HAT)
  static constexpr uint8_t kContactActionCount = 4;  // Fav, Admin, DM, Close
#else
  static constexpr uint8_t kContactActionCount = 3;  // Fav, Admin, DM
#endif
  static constexpr uint8_t kMaxContactsUi = 8;
  static constexpr size_t kMaxChatRows = 96;
#if defined(DEVICE_TLORA_PAGER_TFT)
  static constexpr size_t kMaxStoredChatRows = 112;
#else
  static constexpr size_t kMaxStoredChatRows = 120;
#endif
  static constexpr size_t kMetricChartPoints = 60;

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
  // Shared confirmation modal (used by config and contacts).
  enum class ConfirmKind : uint8_t { None, CfgRow, ContactDelete, ImportFirstInstall, RegionDefault };

  // First-install onboarding wizard: import? -> name -> region -> wifi -> reboot.
  enum class OnboardingStep : uint8_t { None, Import, Name, Region, RegionList, WifiSsid, WifiPass };
  void startOnboarding();
  void advanceOnboardingToName();
  void openOnboardingComposePrompt(const char* placeholder, uint16_t max_len, bool allow_skip);
  void declineConfirm();
  void openRegionChoicePrompt();
  void openRegionListDialog();
  bool ensureRegionListDialogBuilt();
  void chooseRegionAndAdvance(const char* region_id);
  void openWifiSsidPrompt();
  void openWifiPassPrompt();
  bool commitOnboardingText();
  void onboardingSkipOrCancel();
  void finishOnboardingAndReboot();
  bool ensureConfirmDialogBuilt();
  void openConfirmDialog(ConfirmKind kind, const char* title, const char* body, uint32_t guard_ms,
                         const char* yes_label = nullptr, const char* no_label = nullptr);
  void closeConfirmDialog();
  void acceptConfirmDialog();
  bool cfgActionNeedsConfirm(uint8_t row) const;
  const char* cfgConfirmActionText(uint8_t row) const;
  void openCfgConfirmDialog(uint8_t row);
  void performCfgConfirmedAction(uint8_t row);
  void openContactDeleteConfirm();
  void toggleSelectedContactIgnored();
  bool ensureContactActionsPopupBuilt();
  void openContactActionsPopup();
  void closeContactActionsPopup();
  void performContactDelete();
  bool openContactsDialog();
  bool ensureContactsDialogBuilt();
  void closeContactsDialog(bool focus_chat = false);
  void refreshContactsDialog(bool reload_from_mesh = true);
  void rebuildContactsDmPanel();
  void openContactsPathDialog();
  void closeContactsPathDialog();
  void moveContactsSelection(int delta);
  void activateContactsAction(uint8_t action_idx);
  void startComposeForSelectedContact();
  void openDmDialog(const char* contact_name, const char* contact_key);
  void closeDmDialog(bool focus_chat = false);
  void appendDmLine(const char* contact_name, const char* contact_key, const char* text, ChatLineKind kind,
                    uint32_t timestamp_epoch = 0);
  bool hasStoredIncomingDmDuplicate(const char* contact_name, const char* contact_key,
                                    const char* incoming_text,
                                    uint32_t max_timestamp_epoch = 0) const;
  void rebuildDmDialog();
  void clearDmPanel();
  bool clearStoredDmConversation(const char* contact_name, const char* contact_key,
                                 bool prefer_name_match = false);
  void clearActiveDmConversation(bool from_contacts_view);
  void openHelpDialog();
  void closeHelpDialog();
  void openLiveDialog();
  void closeLiveDialog();
  void openLiveUtilDialog();
  void closeLiveUtilDialog();
  void openLiveSnrDialog();
  void closeLiveSnrDialog();
  void appendLiveFeedLine(const char* text, ChatLineKind kind);
  void clearLivePanel();
  void rebuildLiveDialog();
  void appendUtilizationSample(uint16_t util_percent, uint16_t packets_per_sec_x10);
  void appendSnrRssiSample(int16_t snr_db, int16_t rssi_dbm);
  void rebuildLiveUtilChart();
  void rebuildLiveSnrChart();
  void sampleLiveMetrics(uint32_t now_ms);
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

  void appendChatLine(const char* text, ChatLineKind kind, uint32_t timestamp_epoch = 0);
  int findConfiguredChannelIndex(const char* channel_name) const;
  void pushChannelHistoryLine(const char* channel_name, const char* text, ChatLineKind kind,
                              uint32_t timestamp_epoch = 0);
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
  void showAdminPasswordKeyboard();
  void hideAdminPasswordKeyboard();

  static void onFocusableEvent(lv_event_t* event);
  static void onContactsEvent(lv_event_t* event);
  static void onRegionListEvent(lv_event_t* event);
  static void onDmEvent(lv_event_t* event);
  static void onComposeActionEvent(lv_event_t* event);
  static void onComposeKeyboardEvent(lv_event_t* event);
  static void onOpenContactsDialogAsync(void* user_data);
  static void onContactsPostOpenAsync(void* user_data);
  static void onOpenComposeDialogAsync(void* user_data);
  static void onComposePostOpenAsync(void* user_data);
  static void onAdminPwEvent(lv_event_t* e);
  static void onAdminScreenEvent(lv_event_t* e);
  static void onAdminCmdEvent(lv_event_t* e);

  void openAdminPasswordDialog(const char* contact_name, const char* contact_key, uint8_t contact_type);
  void closeAdminPasswordDialog();
  void submitAdminPassword();
  void openAdminScreen(const char* contact_name);
  void closeAdminScreen();
  void openAdminCommandDialog();
  void closeAdminCommandDialog();
  void submitAdminCommand();
  void loadAdminPassword(const char* public_key_hex, char* out_pw, size_t out_size);
  void saveAdminPassword(const char* public_key_hex, const char* password);
  void applyAdminLoginEvent(const mesh::MeshEvent& event);
  void applyAdminCommandEvent(const mesh::MeshEvent& event);
  void refreshAdminStatusLabel();
  void refreshAdminCommandHistoryLabel();
  void appendAdminCommandHistory(const char* command, const char* result, bool pending);
  bool pending_contacts_open_ = false;
  bool pending_contacts_show_ = false;
  bool pending_contacts_post_open_ = false;
  bool pending_compose_post_open_ = false;

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
  lv_obj_t* compose_action_row_ = nullptr;
  lv_obj_t* compose_cancel_btn_ = nullptr;
  lv_obj_t* compose_cancel_label_ = nullptr;
  lv_obj_t* compose_send_btn_ = nullptr;
  lv_obj_t* compose_send_label_ = nullptr;
  lv_obj_t* compose_keyboard_ = nullptr;
  lv_obj_t* cfg_dialog_ = nullptr;
  lv_obj_t* cfg_title_label_ = nullptr;
  lv_obj_t* cfg_action_label_ = nullptr;
  lv_obj_t* cfg_status_label_ = nullptr;
  lv_obj_t* cfg_close_btn_ = nullptr;
  lv_obj_t* cfg_close_label_ = nullptr;
  lv_obj_t* cfg_content_panel_ = nullptr;
  lv_obj_t* cfg_rows_[kCfgRowCount]{};
  lv_obj_t* cfg_row_labels_[kCfgRowCount]{};
  lv_obj_t* confirm_backdrop_ = nullptr;
  lv_obj_t* confirm_dialog_ = nullptr;
  lv_obj_t* confirm_title_label_ = nullptr;
  lv_obj_t* confirm_action_label_ = nullptr;
  lv_obj_t* confirm_yes_btn_ = nullptr;
  lv_obj_t* confirm_yes_label_ = nullptr;
  lv_obj_t* confirm_no_btn_ = nullptr;
  lv_obj_t* confirm_no_label_ = nullptr;
  lv_obj_t* contacts_dialog_ = nullptr;
  lv_obj_t* contacts_status_label_ = nullptr;
  lv_obj_t* contacts_detail_panel_ = nullptr;
  lv_obj_t* contacts_detail_info_panel_ = nullptr;
  lv_obj_t* contacts_action_rows_[kContactActionCount]{};
  lv_obj_t* contacts_action_labels_[kContactActionCount]{};
  lv_obj_t* contacts_full_name_label_ = nullptr;
  lv_obj_t* contacts_lat_lon_label_ = nullptr;
  lv_obj_t* contacts_last_heard_label_ = nullptr;
  lv_obj_t* contacts_telemetry_label_ = nullptr;
  lv_obj_t* contacts_dm_panel_ = nullptr;
  lv_obj_t* contacts_dm_clear_btn_ = nullptr;
  lv_obj_t* contacts_dm_clear_label_ = nullptr;
  lv_obj_t* contacts_dm_new_btn_ = nullptr;
  lv_obj_t* contacts_dm_new_label_ = nullptr;
  lv_obj_t* contacts_dm_hint_label_ = nullptr;
  lv_obj_t* contacts_path_btn_ = nullptr;
  lv_obj_t* contacts_path_label_ = nullptr;
  lv_obj_t* contacts_ignore_btn_ = nullptr;
  lv_obj_t* contacts_ignore_label_ = nullptr;
  lv_obj_t* contacts_del_btn_ = nullptr;
  lv_obj_t* contacts_del_label_ = nullptr;
  // "Contact Actions" header button + the pop-up it opens (holds Admin/Path/Ignore/Del).
  lv_obj_t* contacts_actions_btn_ = nullptr;
  lv_obj_t* contacts_actions_label_ = nullptr;
  lv_obj_t* contacts_actions_backdrop_ = nullptr;
  lv_obj_t* contacts_actions_panel_ = nullptr;
  lv_obj_t* contacts_actions_admin_btn_ = nullptr;
  lv_obj_t* contacts_actions_admin_label_ = nullptr;
  lv_obj_t* contacts_actions_close_btn_ = nullptr;
  bool contacts_actions_open_ = false;
  lv_obj_t* contacts_path_dialog_ = nullptr;
  lv_obj_t* contacts_path_title_label_ = nullptr;
  lv_obj_t* contacts_path_body_panel_ = nullptr;
  lv_obj_t* contacts_path_body_label_ = nullptr;
  lv_obj_t* contacts_path_close_btn_ = nullptr;
  lv_obj_t* contacts_path_close_label_ = nullptr;
  lv_obj_t* dm_dialog_ = nullptr;
  lv_obj_t* dm_title_label_ = nullptr;
  lv_obj_t* dm_clear_btn_ = nullptr;
  lv_obj_t* dm_clear_label_ = nullptr;
  lv_obj_t* dm_new_btn_ = nullptr;
  lv_obj_t* dm_new_label_ = nullptr;
  lv_obj_t* dm_panel_ = nullptr;
  lv_obj_t* dm_close_btn_ = nullptr;
  lv_obj_t* dm_close_label_ = nullptr;
  lv_obj_t* dm_hint_label_ = nullptr;
  lv_obj_t* dm_rows_[kMaxChatRows]{};
  size_t dm_row_count_ = 0;
  lv_obj_t* dm_pending_ack_label_ = nullptr;
  char dm_pending_ack_contact_key_[65] = {};
  char dm_pending_ack_contact_name_[32] = {};
  char dm_pending_ack_hhmm_[8] = {};
  char dm_pending_ack_snippet_[97] = {};
  uint8_t dm_pending_ack_count_ = 0;
  size_t dm_pending_ack_stored_idx_ = SIZE_MAX;

  // Admin password dialog
  lv_obj_t* admin_pw_dialog_ = nullptr;
  lv_obj_t* admin_pw_title_label_ = nullptr;
  lv_obj_t* admin_pw_input_ = nullptr;
  lv_obj_t* admin_pw_keyboard_ = nullptr;
  lv_obj_t* admin_pw_save_btn_ = nullptr;
  lv_obj_t* admin_pw_save_label_ = nullptr;
  lv_obj_t* admin_pw_ok_btn_ = nullptr;
  lv_obj_t* admin_pw_cancel_btn_ = nullptr;
  bool admin_pw_open_ = false;
  bool admin_pw_save_ = false;
  bool admin_join_after_login_ = false;
  uint32_t room_join_replay_dedup_until_ms_ = 0;
  uint32_t room_join_replay_snapshot_epoch_ = 0;
  char room_join_replay_key_[65] = {};
  char room_join_replay_name_[32] = {};

  // Admin screen (blank for now)
  lv_obj_t* admin_screen_dialog_ = nullptr;
  lv_obj_t* admin_screen_title_label_ = nullptr;
  lv_obj_t* admin_screen_auth_label_ = nullptr;
  lv_obj_t* admin_screen_close_btn_ = nullptr;
  lv_obj_t* admin_screen_status_label_ = nullptr;
  lv_obj_t* admin_screen_history_panel_ = nullptr;
  lv_obj_t* admin_screen_history_label_ = nullptr;
  lv_obj_t* admin_screen_hint_label_ = nullptr;
  bool admin_screen_open_ = false;
  uint32_t admin_screen_key_guard_until_ms_ = 0;

  // Admin command dialog
  lv_obj_t* admin_cmd_dialog_ = nullptr;
  lv_obj_t* admin_cmd_title_label_ = nullptr;
  lv_obj_t* admin_cmd_input_ = nullptr;
  lv_obj_t* admin_cmd_run_btn_ = nullptr;
  lv_obj_t* admin_cmd_cancel_btn_ = nullptr;
  bool admin_cmd_open_ = false;

  static constexpr uint8_t kAdminCmdHistoryMax = 10;
  struct AdminCmdHistoryEntry {
    char command[64];
    char result[96];
    bool pending;
  };
  AdminCmdHistoryEntry admin_cmd_history_[kAdminCmdHistoryMax]{};
  uint8_t admin_cmd_history_count_ = 0;
  char admin_cmd_history_render_[1200] = {};

  // Pending admin target
  char admin_target_key_[65] = {};
  char admin_target_name_[32] = {};
  uint8_t admin_target_type_ = 0;

  // Admin login state machine
  enum class AdminLoginState : uint8_t {
    Idle,
    Sending,
    Pending,
    Success,
    Failed,
    TimedOut,
  };
  AdminLoginState admin_login_state_ = AdminLoginState::Idle;
  bool admin_is_admin_ = false;
  uint8_t admin_login_acl_ = 0;
  uint8_t admin_login_fw_ver_ = 0;

  lv_obj_t* help_dialog_ = nullptr;
  lv_obj_t* help_body_panel_ = nullptr;
  lv_obj_t* help_title_label_ = nullptr;
  lv_obj_t* help_body_label_ = nullptr;
  lv_obj_t* help_close_btn_ = nullptr;
  lv_obj_t* help_close_label_ = nullptr;
  lv_obj_t* live_dialog_ = nullptr;
  lv_obj_t* live_title_label_ = nullptr;
  lv_obj_t* live_body_panel_ = nullptr;
  lv_obj_t* live_hint_label_ = nullptr;
  lv_obj_t* live_close_btn_ = nullptr;
  lv_obj_t* live_close_label_ = nullptr;
  lv_obj_t* live_util_dialog_ = nullptr;
  lv_obj_t* live_util_title_label_ = nullptr;
  lv_obj_t* live_util_chart_ = nullptr;
  lv_obj_t* live_util_units_label_ = nullptr;
  lv_obj_t* live_util_hint_label_ = nullptr;
  lv_obj_t* live_util_stats_label_ = nullptr;
  lv_chart_series_t* live_util_series_ = nullptr;
  lv_obj_t* live_snr_dialog_ = nullptr;
  lv_obj_t* live_snr_title_label_ = nullptr;
  lv_obj_t* live_snr_chart_ = nullptr;
  lv_obj_t* live_snr_units_label_ = nullptr;
  lv_obj_t* live_snr_hint_label_ = nullptr;
  lv_obj_t* live_snr_stats_label_ = nullptr;
  lv_chart_series_t* live_snr_series_ = nullptr;
  lv_chart_series_t* live_rssi_series_ = nullptr;
  lv_obj_t* live_rows_[kMaxChatRows]{};
  size_t live_row_count_ = 0;
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

  lv_obj_t* splash_overlay_ = nullptr;
  uint32_t splash_duration_ms_ = 0;
  uint32_t splash_dismiss_ms_ = 0;
  bool dm_open_ = false;
  bool help_open_ = false;
  bool live_open_ = false;
  bool live_util_open_ = false;
  bool live_snr_open_ = false;
  bool advert_popup_open_ = false;
  bool has_unread_dm_ = false;
  bool contacts_nav_focused_ = false;
  bool contacts_dm_open_ = false;
  bool contacts_path_open_ = false;
  bool confirm_open_ = false;
  bool first_install_identity_prompt_ = false;
  bool first_install_auto_export_pending_ = false;
  bool first_install_import_available_ = false;
  bool identity_prompt_open_ = false;
  OnboardingStep onboarding_step_ = OnboardingStep::None;
  char onboarding_wifi_ssid_[64] = {};
  lv_obj_t* region_list_backdrop_ = nullptr;
  lv_obj_t* region_list_panel_ = nullptr;
  uint8_t region_list_selected_ = 0;
  uint8_t pending_chat_focus_attempts_ = 0;
  uint8_t dropdown_highlight_channel_ = 0;
  uint8_t cfg_selected_row_ = 0;
  uint8_t confirm_pending_row_ = 0xFF;
  uint32_t confirm_guard_until_ms_ = 0;
  bool confirm_swallow_first_click_ = false;
  ConfirmKind confirm_kind_ = ConfirmKind::None;
  char contacts_pending_delete_key_[65] = {};
  char contacts_pending_delete_name_[32] = {};
  uint8_t contacts_selected_index_ = 0;
  uint8_t contacts_action_index_ = 0;
  uint8_t contacts_count_ = 0;
  uint32_t compose_opened_ms_ = 0;
  uint32_t last_selector_action_ms_ = 0;
  uint32_t last_cfg_action_ms_ = 0;
  uint8_t last_cfg_action_row_ = 0xFF;
  uint32_t last_dropdown_open_ms_ = 0;
  uint32_t contacts_dropdown_guard_until_ms_ = 0;
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
  uint32_t room_ingest_last_name_hash_ = 0;
  uint32_t room_ingest_last_key_hash_ = 0;
  uint32_t room_ingest_last_text_hash_ = 0;
  uint32_t room_ingest_last_ms_ = 0;
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
  uint32_t dm_last_date_key_ = 0;

  struct StoredChatLine {
    char channel_name[32];
    char text[96];
    ChatLineKind kind;
    uint32_t timestamp_epoch;
  };
  StoredChatLine stored_chat_[kMaxStoredChatRows]{};
  size_t stored_chat_head_ = 0;
  size_t stored_chat_count_ = 0;
  uint32_t chat_last_date_key_ = 0;
  StoredChatLine stored_live_[kMaxStoredChatRows]{};
  size_t stored_live_head_ = 0;
  size_t stored_live_count_ = 0;
  uint16_t util_history_[kMetricChartPoints]{};
  size_t util_history_head_ = 0;
  size_t util_history_count_ = 0;
  int16_t snr_history_[kMetricChartPoints]{};
  int16_t rssi_history_[kMetricChartPoints]{};
  size_t radio_history_head_ = 0;
  size_t radio_history_count_ = 0;
  uint16_t util_latest_percent_ = 0;
  uint16_t util_latest_pps_x10_ = 0;
  int16_t last_snr_db_ = 0;
  int16_t last_rssi_dbm_ = -120;
  uint32_t last_util_sample_ms_ = 0;
  uint32_t last_util_raw_rx_count_ = 0;
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
