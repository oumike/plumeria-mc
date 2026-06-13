#include "ui/standalone_ui.h"

#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "web/web_config.h"

#ifndef PLUMERIA_KEY_DEBUG
#define PLUMERIA_KEY_DEBUG 0
#endif

#ifndef LV_SYMBOL_GPS
#define LV_SYMBOL_GPS LV_SYMBOL_DRIVE
#endif

namespace plumeria {
namespace ui {

namespace {

const lv_color_t kColorBgRoot = lv_color_hex(0x08121B);
const lv_color_t kColorPanel = lv_color_hex(0x0C1A27);
const lv_color_t kColorPanelAlt = lv_color_hex(0x102335);
const lv_color_t kColorBorder = lv_color_hex(0x1D3C55);
const lv_color_t kColorFocus = lv_color_hex(0x33D1FF);
const lv_color_t kColorActive = lv_color_hex(0x1E9ED1);
const lv_color_t kColorUnread = lv_color_hex(0x6EF0FF);
const lv_color_t kColorTextMain = lv_color_hex(0xD8E7F2);
const lv_color_t kColorTextDim = lv_color_hex(0x8FA8BA);
const lv_color_t kColorRx = lv_color_hex(0x89E3FF);
const lv_color_t kColorTx = lv_color_hex(0xA8FFB5);
const lv_color_t kColorAck = lv_color_hex(0x7ED6A7);
const lv_color_t kColorErr = lv_color_hex(0xFF7D7D);
const lv_color_t kColorWifiOn = lv_color_hex(0x59D88E);
const lv_color_t kColorWifiOff = lv_color_hex(0xF56767);
const lv_color_t kColorWifiApBadge = lv_color_hex(0xD8E7F2);

constexpr lv_coord_t kOuterPad = 2;
constexpr lv_coord_t kGap = 2;
constexpr lv_coord_t kMainBottomInset = 4;
constexpr lv_coord_t kHeaderH = 30;
constexpr lv_coord_t kShortcutH = 18;
constexpr lv_coord_t kMsgScrollStep = 12;
constexpr lv_coord_t kSelectorMinW = 60;
constexpr lv_coord_t kSelectorMaxW = 120;
constexpr lv_coord_t kDropdownVisibleRows = 7;
constexpr lv_coord_t kDropdownRowH = 20;
constexpr lv_coord_t kDropdownPanelPadY = 10;
constexpr lv_coord_t kDropdownRightGap = 6;
constexpr lv_coord_t kDropdownScrollbarW = 4;
constexpr lv_coord_t kDropdownBottomPad = 10;
constexpr lv_coord_t kHeaderTimeGap = 8;
constexpr lv_coord_t kHeaderBatteryTextX = -34;
constexpr lv_coord_t kHeaderBatteryBarX = -2;
constexpr lv_coord_t kHeaderIconsToBatteryGap = 18;
constexpr lv_coord_t kHeaderIconsGap = 10;
constexpr lv_coord_t kComposeDialogMinW = 160;
constexpr lv_coord_t kComposeDialogMaxW = 236;
constexpr lv_coord_t kComposeDialogH = 70;
constexpr uint32_t kNavDebounceMs = 120;
constexpr uint32_t kLocalEchoSuppressMs = 3000;
constexpr uint32_t kChatPersistFlushMs = 2000;
constexpr uint32_t kChannelSyncMs = 1000;
constexpr time_t kTimeValidEpoch = 1700000000;
constexpr char kUiPrefsNs[] = "ui_state";
constexpr char kChatHistoryBlobKey[] = "chat_blob";
constexpr char kChatHistoryCountKey[] = "chat_count";
constexpr char kCfgSdDir[] = "/plumeria";
constexpr char kCfgSdPath[] = "/plumeria/plumeria-config.yaml";
constexpr uint32_t kCfgSdClockHz = 800000UL;

const char* kShortcutNames[4] = {
    "CFG",
  "CONTACTS",
    "LIVE",
    "LEGEND",
};

const char* kCfgRowLabels[6] = {
  "Node Name",
  "Radio Preset",
  "Web Config",
  "GPS",
  "Export Config",
  "Import Config",
};

const char* radioPresetDisplayName(const char* region) {
  if (!region || region[0] == '\0') {
    return "-";
  }
  if (strcmp(region, "US") == 0) {
    return "US/Canada";
  }
  return region;
}

struct PersistedChatLine {
  char channel_name[32];
  char text[96];
  uint8_t kind;
};

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

  SPI.begin(sd_sck, sd_miso, sd_mosi);
  if (!SD.begin(sd_cs, SPI, kCfgSdClockHz)) {
    setErrText(out_err, out_err_size, "SD mount failed");
    return false;
  }

  if (SD.cardType() == CARD_NONE) {
    setErrText(out_err, out_err_size, "No SD card");
    return false;
  }

  setErrText(out_err, out_err_size, "");
  return true;
#endif
}

lv_coord_t clampCoord(lv_coord_t value, lv_coord_t low, lv_coord_t high) {
  if (value < low) {
    return low;
  }
  if (value > high) {
    return high;
  }
  return value;
}

bool hasAncestor(lv_obj_t* node, lv_obj_t* ancestor) {
  if (!node || !ancestor) {
    return false;
  }
  for (lv_obj_t* p = node; p != nullptr; p = lv_obj_get_parent(p)) {
    if (p == ancestor) {
      return true;
    }
  }
  return false;
}

const lv_font_t* headerBarFont() {
#if defined(LV_FONT_MONTSERRAT_12) && LV_FONT_MONTSERRAT_12
  return &lv_font_montserrat_12;
#else
  return LV_FONT_DEFAULT;
#endif
}

const lv_font_t* chatPanelFont() {
#if defined(LV_FONT_MONTSERRAT_10) && LV_FONT_MONTSERRAT_10
  return &lv_font_montserrat_10;
#elif defined(LV_FONT_MONTSERRAT_12) && LV_FONT_MONTSERRAT_12
  return &lv_font_montserrat_12;
#else
  return LV_FONT_DEFAULT;
#endif
}

}  // namespace

bool StandaloneUi::createStyles() {
  if (styles_ready_) {
    return true;
  }

  lv_style_init(&style_root_);
  lv_style_set_bg_opa(&style_root_, LV_OPA_COVER);
  lv_style_set_bg_color(&style_root_, kColorBgRoot);
  lv_style_set_border_width(&style_root_, 0);
  lv_style_set_pad_all(&style_root_, 0);
  lv_style_set_radius(&style_root_, 0);

  lv_style_init(&style_panel_);
  lv_style_set_bg_opa(&style_panel_, LV_OPA_COVER);
  lv_style_set_bg_color(&style_panel_, kColorPanel);
  lv_style_set_border_width(&style_panel_, 1);
  lv_style_set_border_color(&style_panel_, kColorBorder);
  lv_style_set_radius(&style_panel_, 2);
  lv_style_set_pad_all(&style_panel_, 2);

  lv_style_init(&style_header_);
  lv_style_set_bg_opa(&style_header_, LV_OPA_COVER);
  lv_style_set_bg_color(&style_header_, kColorPanelAlt);
  lv_style_set_border_width(&style_header_, 1);
  lv_style_set_border_color(&style_header_, kColorBorder);
  lv_style_set_radius(&style_header_, 2);
  lv_style_set_pad_left(&style_header_, 4);
  lv_style_set_pad_right(&style_header_, 4);
  lv_style_set_pad_top(&style_header_, 2);
  lv_style_set_pad_bottom(&style_header_, 2);

  lv_style_init(&style_chat_);
  lv_style_set_bg_opa(&style_chat_, LV_OPA_COVER);
  lv_style_set_bg_color(&style_chat_, lv_color_hex(0x0A1622));
  lv_style_set_border_width(&style_chat_, 1);
  lv_style_set_border_color(&style_chat_, kColorBorder);
  lv_style_set_radius(&style_chat_, 2);
  lv_style_set_pad_left(&style_chat_, 3);
  lv_style_set_pad_right(&style_chat_, 3);
  lv_style_set_pad_top(&style_chat_, 2);
  lv_style_set_pad_bottom(&style_chat_, 2);
  lv_style_set_pad_row(&style_chat_, 1);

  lv_style_init(&style_chat_focused_);
  lv_style_set_border_width(&style_chat_focused_, 1);
  lv_style_set_border_color(&style_chat_focused_, kColorFocus);
  lv_style_set_border_opa(&style_chat_focused_, LV_OPA_COVER);

  lv_style_init(&style_button_);
  lv_style_set_bg_opa(&style_button_, LV_OPA_COVER);
  lv_style_set_bg_color(&style_button_, kColorPanelAlt);
  lv_style_set_border_width(&style_button_, 1);
  lv_style_set_border_color(&style_button_, kColorBorder);
  lv_style_set_radius(&style_button_, 2);
  lv_style_set_pad_all(&style_button_, 1);
  lv_style_set_text_color(&style_button_, kColorTextMain);
  lv_style_set_text_font(&style_button_, LV_FONT_DEFAULT);

  lv_style_init(&style_button_focused_);
  lv_style_set_border_color(&style_button_focused_, kColorFocus);
  lv_style_set_bg_color(&style_button_focused_, lv_color_hex(0x133149));
  lv_style_set_text_color(&style_button_focused_, lv_color_hex(0xEAF8FF));

  lv_style_init(&style_button_active_);
  lv_style_set_bg_color(&style_button_active_, kColorActive);
  lv_style_set_border_color(&style_button_active_, lv_color_hex(0x54D6FF));
  lv_style_set_text_color(&style_button_active_, lv_color_hex(0xFFFFFF));

  lv_style_init(&style_selector_anchor_);
  lv_style_set_bg_opa(&style_selector_anchor_, LV_OPA_COVER);
  lv_style_set_bg_color(&style_selector_anchor_, lv_color_hex(0x133049));
  lv_style_set_border_width(&style_selector_anchor_, 1);
  lv_style_set_border_color(&style_selector_anchor_, kColorBorder);
  lv_style_set_radius(&style_selector_anchor_, 2);
  lv_style_set_pad_left(&style_selector_anchor_, 3);
  lv_style_set_pad_right(&style_selector_anchor_, 3);

  lv_style_init(&style_dropdown_panel_);
  lv_style_set_bg_opa(&style_dropdown_panel_, LV_OPA_COVER);
  lv_style_set_bg_color(&style_dropdown_panel_, lv_color_hex(0x132433));
  lv_style_set_border_width(&style_dropdown_panel_, 1);
  lv_style_set_border_color(&style_dropdown_panel_, kColorFocus);
  lv_style_set_radius(&style_dropdown_panel_, 2);
  lv_style_set_pad_all(&style_dropdown_panel_, 2);

  lv_style_init(&style_dropdown_highlight_);
  lv_style_set_bg_opa(&style_dropdown_highlight_, LV_OPA_COVER);
  lv_style_set_bg_color(&style_dropdown_highlight_, lv_color_hex(0x48DBFF));
  lv_style_set_text_color(&style_dropdown_highlight_, lv_color_hex(0x03131F));
  lv_style_set_border_color(&style_dropdown_highlight_, lv_color_hex(0xA5F0FF));

  lv_style_init(&style_dropdown_active_);
  lv_style_set_bg_opa(&style_dropdown_active_, LV_OPA_70);
  lv_style_set_bg_color(&style_dropdown_active_, lv_color_hex(0x18668A));
  lv_style_set_border_color(&style_dropdown_active_, lv_color_hex(0x6EE7FF));

  lv_style_init(&style_shortcut_active_);
  lv_style_set_bg_color(&style_shortcut_active_, lv_color_hex(0x15435F));
  lv_style_set_border_color(&style_shortcut_active_, kColorFocus);

  lv_style_init(&style_text_main_);
  lv_style_set_text_color(&style_text_main_, kColorTextMain);
  lv_style_set_text_font(&style_text_main_, LV_FONT_DEFAULT);

  lv_style_init(&style_text_dim_);
  lv_style_set_text_color(&style_text_dim_, kColorTextDim);
  lv_style_set_text_font(&style_text_dim_, LV_FONT_DEFAULT);

  lv_style_init(&style_msg_rx_);
  lv_style_set_text_color(&style_msg_rx_, kColorRx);

  lv_style_init(&style_msg_tx_);
  lv_style_set_text_color(&style_msg_tx_, kColorTx);

  lv_style_init(&style_msg_ack_);
  lv_style_set_text_color(&style_msg_ack_, kColorAck);

  lv_style_init(&style_msg_err_);
  lv_style_set_text_color(&style_msg_err_, kColorErr);

  styles_ready_ = true;
  return true;
}

void StandaloneUi::attachMeshAdapter(mesh::MeshAdapter* adapter) {
  mesh_adapter_ = adapter;
}

void StandaloneUi::buildLayout() {
  const lv_coord_t screen_w = lv_disp_get_hor_res(nullptr);
  const lv_coord_t screen_h = lv_disp_get_ver_res(nullptr);

  const lv_coord_t main_w = screen_w - (kOuterPad * 2);
  const lv_coord_t main_h = screen_h - (kOuterPad * 2);

  const lv_coord_t header_h = clampCoord(kHeaderH + ((screen_h - 170) / 14), 30, 35);
  const lv_coord_t shortcut_h = clampCoord(kShortcutH + ((screen_h - 170) / 18), 16, 22);
  const lv_coord_t chat_y = header_h + kGap;
  const lv_coord_t chat_h = main_h - header_h - shortcut_h - (kGap * 2) - kMainBottomInset;
  const lv_font_t* header_font = headerBarFont();

  root_ = lv_obj_create(lv_scr_act());
  lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
  lv_obj_add_style(root_, &style_root_, 0);
  lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(root_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(root_, onFocusableEvent, LV_EVENT_CLICKED, this);

  main_panel_ = lv_obj_create(root_);
  lv_obj_set_pos(main_panel_, kOuterPad, kOuterPad);
  lv_obj_set_size(main_panel_, main_w, main_h);
  lv_obj_add_style(main_panel_, &style_panel_, 0);
  lv_obj_clear_flag(main_panel_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(main_panel_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(main_panel_, onFocusableEvent, LV_EVENT_CLICKED, this);

  header_bar_ = lv_obj_create(main_panel_);
  lv_obj_set_pos(header_bar_, 0, 0);
  lv_obj_set_size(header_bar_, LV_PCT(100), header_h);
  lv_obj_add_style(header_bar_, &style_header_, 0);
  lv_obj_clear_flag(header_bar_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(header_bar_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(header_bar_, onFocusableEvent, LV_EVENT_CLICKED, this);

  const lv_coord_t selector_w = clampCoord(main_w / 4, kSelectorMinW, kSelectorMaxW);
  const lv_coord_t selector_h = header_h - 8;
  channel_selector_btn_ = lv_btn_create(header_bar_);
  lv_obj_set_size(channel_selector_btn_, selector_w, selector_h);
  lv_obj_align(channel_selector_btn_, LV_ALIGN_LEFT_MID, 2, 0);
  lv_obj_add_style(channel_selector_btn_, &style_button_, 0);
  lv_obj_add_style(channel_selector_btn_, &style_button_focused_, LV_STATE_FOCUSED);
  lv_obj_add_style(channel_selector_btn_, &style_selector_anchor_, 0);
  lv_obj_set_ext_click_area(channel_selector_btn_, 6);
  lv_obj_add_event_cb(channel_selector_btn_, onFocusableEvent, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(channel_selector_btn_, onFocusableEvent, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(channel_selector_btn_, onFocusableEvent, LV_EVENT_FOCUSED, this);

  channel_selector_label_ = lv_label_create(channel_selector_btn_);
  lv_obj_add_style(channel_selector_label_, &style_text_main_, 0);
  lv_obj_set_style_text_font(channel_selector_label_, header_font, 0);
  lv_obj_align(channel_selector_label_, LV_ALIGN_LEFT_MID, 0, 0);

  channel_selector_caret_ = lv_label_create(channel_selector_btn_);
  lv_obj_add_style(channel_selector_caret_, &style_text_dim_, 0);
  lv_obj_set_style_text_font(channel_selector_caret_, header_font, 0);
  lv_obj_align(channel_selector_caret_, LV_ALIGN_RIGHT_MID, -1, 0);
  lv_obj_add_flag(channel_selector_caret_, LV_OBJ_FLAG_HIDDEN);

  channel_dropdown_panel_ = lv_obj_create(root_);
  lv_obj_add_style(channel_dropdown_panel_, &style_dropdown_panel_, 0);
  lv_obj_set_size(channel_dropdown_panel_, selector_w + 12, kDropdownRowH + kDropdownPanelPadY);
  lv_obj_set_pos(channel_dropdown_panel_, kOuterPad + 2, kOuterPad + header_h + 2);
  lv_obj_add_flag(channel_dropdown_panel_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_scrollbar_mode(channel_dropdown_panel_, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(channel_dropdown_panel_, LV_OBJ_FLAG_SCROLLABLE);

  channel_dropdown_list_ = lv_obj_create(channel_dropdown_panel_);
  lv_obj_set_size(channel_dropdown_list_, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(channel_dropdown_list_, 0, 0);
  lv_obj_add_style(channel_dropdown_list_, &style_panel_, 0);
  lv_obj_set_layout(channel_dropdown_list_, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(channel_dropdown_list_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(channel_dropdown_list_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_scroll_dir(channel_dropdown_list_, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(channel_dropdown_list_, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(channel_dropdown_list_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_right(channel_dropdown_list_, kDropdownRightGap, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(channel_dropdown_list_, kDropdownBottomPad, LV_PART_MAIN);
  lv_obj_set_style_width(channel_dropdown_list_, kDropdownScrollbarW, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(channel_dropdown_list_, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_color(channel_dropdown_list_, lv_color_hex(0x2C7CA5), LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(channel_dropdown_list_, 2, LV_PART_SCROLLBAR);

  for (uint8_t i = 0; i < kChannelCount; i++) {
    channel_dropdown_rows_[i] = lv_btn_create(channel_dropdown_list_);
    lv_obj_set_width(channel_dropdown_rows_[i], LV_PCT(100));
    lv_obj_set_height(channel_dropdown_rows_[i], kDropdownRowH);
    lv_obj_add_style(channel_dropdown_rows_[i], &style_button_, 0);
    lv_obj_add_style(channel_dropdown_rows_[i], &style_button_focused_, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(channel_dropdown_rows_[i], onFocusableEvent, LV_EVENT_KEY, this);
    lv_obj_add_event_cb(channel_dropdown_rows_[i], onFocusableEvent, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(channel_dropdown_rows_[i], onFocusableEvent, LV_EVENT_FOCUSED, this);

    channel_dropdown_labels_[i] = lv_label_create(channel_dropdown_rows_[i]);
    lv_obj_add_style(channel_dropdown_labels_[i], &style_text_main_, 0);
    lv_obj_align(channel_dropdown_labels_[i], LV_ALIGN_LEFT_MID, 1, 0);
  }

  battery_pct_label_ = lv_label_create(header_bar_);
  lv_obj_add_style(battery_pct_label_, &style_text_main_, 0);
  lv_obj_set_style_text_font(battery_pct_label_, header_font, 0);
  lv_obj_align(battery_pct_label_, LV_ALIGN_RIGHT_MID, kHeaderBatteryTextX, 0);

  gps_label_ = lv_label_create(header_bar_);
  lv_obj_add_style(gps_label_, &style_text_dim_, 0);
  lv_obj_set_style_text_font(gps_label_, header_font, 0);
  lv_obj_align_to(gps_label_, battery_pct_label_, LV_ALIGN_OUT_LEFT_MID,
                  -(kHeaderIconsToBatteryGap + kHeaderIconsGap), 0);

  wifi_label_ = lv_label_create(header_bar_);
  lv_obj_add_style(wifi_label_, &style_text_dim_, 0);
  lv_obj_set_style_text_font(wifi_label_, header_font, 0);
  lv_obj_align_to(wifi_label_, gps_label_, LV_ALIGN_OUT_RIGHT_MID, kHeaderIconsGap, 0);

  wifi_ap_badge_label_ = lv_label_create(header_bar_);
  lv_obj_add_style(wifi_ap_badge_label_, &style_text_main_, 0);
  lv_obj_set_style_text_font(wifi_ap_badge_label_, chatPanelFont(), 0);
  lv_label_set_text(wifi_ap_badge_label_, "AP");
  lv_obj_add_flag(wifi_ap_badge_label_, LV_OBJ_FLAG_HIDDEN);

  time_label_ = lv_label_create(header_bar_);
  lv_obj_add_style(time_label_, &style_text_main_, 0);
  lv_obj_set_style_text_font(time_label_, header_font, 0);
  lv_obj_align_to(time_label_, battery_pct_label_, LV_ALIGN_OUT_LEFT_MID, -kHeaderIconsToBatteryGap, 0);

  battery_bar_ = lv_bar_create(header_bar_);
  lv_obj_set_size(battery_bar_, 26, 6);
  lv_obj_align(battery_bar_, LV_ALIGN_RIGHT_MID, kHeaderBatteryBarX, 0);
  lv_bar_set_range(battery_bar_, 0, 100);
  lv_obj_set_style_bg_color(battery_bar_, lv_color_hex(0x0B1E2D), LV_PART_MAIN);
  lv_obj_set_style_border_color(battery_bar_, kColorBorder, LV_PART_MAIN);
  lv_obj_set_style_border_width(battery_bar_, 1, LV_PART_MAIN);
  lv_obj_set_style_bg_color(battery_bar_, lv_color_hex(0x59D8A0), LV_PART_INDICATOR);
  lv_obj_set_style_pad_all(battery_bar_, 0, LV_PART_MAIN);

  chat_panel_ = lv_obj_create(main_panel_);
  lv_obj_set_pos(chat_panel_, 0, chat_y);
  lv_obj_set_size(chat_panel_, LV_PCT(100), chat_h);
  lv_obj_add_style(chat_panel_, &style_chat_, 0);
  // Keep chat visual neutral while focused.
  lv_obj_set_style_border_color(chat_panel_, kColorBorder, LV_STATE_FOCUSED);
  lv_obj_set_style_border_width(chat_panel_, 1, LV_STATE_FOCUSED);
  lv_obj_set_style_outline_width(chat_panel_, 0, LV_STATE_FOCUSED);
  lv_obj_set_layout(chat_panel_, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(chat_panel_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(chat_panel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_scroll_dir(chat_panel_, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(chat_panel_, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(chat_panel_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(chat_panel_, LV_OBJ_FLAG_CLICK_FOCUSABLE);
  lv_obj_add_event_cb(chat_panel_, onFocusableEvent, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(chat_panel_, onFocusableEvent, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(chat_panel_, onFocusableEvent, LV_EVENT_FOCUSED, this);

  compose_dialog_ = lv_obj_create(root_);
  lv_obj_add_style(compose_dialog_, &style_panel_, 0);
  lv_obj_set_size(compose_dialog_,
                  clampCoord(main_w - 8, kComposeDialogMinW, kComposeDialogMaxW),
                  kComposeDialogH);
  lv_obj_center(compose_dialog_);
  lv_obj_add_flag(compose_dialog_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(compose_dialog_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(compose_dialog_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(compose_dialog_, onFocusableEvent, LV_EVENT_CLICKED, this);

  compose_title_label_ = lv_label_create(compose_dialog_);
  lv_obj_add_style(compose_title_label_, &style_text_main_, 0);
  lv_obj_align(compose_title_label_, LV_ALIGN_TOP_LEFT, 4, 2);

  compose_input_ = lv_textarea_create(compose_dialog_);
  lv_obj_set_size(compose_input_, LV_PCT(100), 30);
  lv_obj_align(compose_input_, LV_ALIGN_BOTTOM_MID, 0, -3);
  lv_textarea_set_one_line(compose_input_, true);
  lv_textarea_set_max_length(compose_input_, 90);
  lv_textarea_set_placeholder_text(compose_input_, "Type and press Enter");
  lv_obj_add_event_cb(compose_input_, onFocusableEvent, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(compose_input_, onFocusableEvent, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(compose_input_, onFocusableEvent, LV_EVENT_FOCUSED, this);

  cfg_dialog_ = lv_obj_create(root_);
  lv_obj_add_style(cfg_dialog_, &style_panel_, 0);
  lv_obj_set_size(cfg_dialog_, clampCoord(main_w - 6, 220, 280), clampCoord(main_h - 10, 170, 230));
  lv_obj_center(cfg_dialog_);
  lv_obj_add_flag(cfg_dialog_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(cfg_dialog_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(cfg_dialog_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(cfg_dialog_, onFocusableEvent, LV_EVENT_CLICKED, this);

  cfg_title_label_ = lv_label_create(cfg_dialog_);
  lv_obj_add_style(cfg_title_label_, &style_text_main_, 0);
  lv_label_set_text(cfg_title_label_, "Configuration");
  lv_obj_align(cfg_title_label_, LV_ALIGN_TOP_LEFT, 4, 2);

  cfg_action_label_ = lv_label_create(cfg_dialog_);
  lv_obj_add_style(cfg_action_label_, &style_text_dim_, 0);
  lv_label_set_text(cfg_action_label_, "");
  lv_obj_align(cfg_action_label_, LV_ALIGN_TOP_RIGHT, -4, 2);

  cfg_status_label_ = lv_label_create(cfg_dialog_);
  lv_obj_add_style(cfg_status_label_, &style_text_dim_, 0);
  lv_label_set_text(cfg_status_label_, "Enter - Activate Option, Backspace - Close Configuration");
#if defined(LV_FONT_MONTSERRAT_10) && LV_FONT_MONTSERRAT_10
  lv_obj_set_style_text_font(cfg_status_label_, &lv_font_montserrat_10, 0);
#endif
  lv_obj_set_width(cfg_status_label_, LV_PCT(100));
  lv_obj_align(cfg_status_label_, LV_ALIGN_BOTTOM_LEFT, 4, -2);

  for (uint8_t i = 0; i < kCfgRowCount; i++) {
    cfg_rows_[i] = lv_btn_create(cfg_dialog_);
    lv_obj_set_size(cfg_rows_[i], LV_PCT(100), 20);
    lv_obj_set_pos(cfg_rows_[i], 2, 20 + (i * 20));
    lv_obj_add_style(cfg_rows_[i], &style_button_, 0);
    lv_obj_add_style(cfg_rows_[i], &style_button_focused_, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(cfg_rows_[i], onFocusableEvent, LV_EVENT_KEY, this);
    lv_obj_add_event_cb(cfg_rows_[i], onFocusableEvent, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(cfg_rows_[i], onFocusableEvent, LV_EVENT_FOCUSED, this);

    cfg_row_labels_[i] = lv_label_create(cfg_rows_[i]);
    lv_obj_add_style(cfg_row_labels_[i], &style_text_main_, 0);
    lv_label_set_text(cfg_row_labels_[i], kCfgRowLabels[i]);
    lv_obj_align(cfg_row_labels_[i], LV_ALIGN_LEFT_MID, 1, 0);
  }

  // Visual split between status rows (name/preset) and actionable rows.
  lv_obj_t* cfg_divider = lv_obj_create(cfg_dialog_);
  lv_obj_set_size(cfg_divider, LV_PCT(100), 3);
  lv_obj_set_pos(cfg_divider, 2, (20 + (2 * 20)) - 1);
  lv_obj_set_style_bg_opa(cfg_divider, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(cfg_divider, lv_color_hex(0x2B4A63), 0);
  lv_obj_set_style_border_width(cfg_divider, 0, 0);
  lv_obj_set_style_radius(cfg_divider, 0, 0);
  lv_obj_clear_flag(cfg_divider, LV_OBJ_FLAG_CLICKABLE);

  shortcut_strip_ = lv_obj_create(main_panel_);
  lv_obj_set_pos(shortcut_strip_, 0, main_h - shortcut_h - kMainBottomInset);
  lv_obj_set_size(shortcut_strip_, LV_PCT(100), shortcut_h);
  lv_obj_add_style(shortcut_strip_, &style_header_, 0);
  lv_obj_clear_flag(shortcut_strip_, LV_OBJ_FLAG_SCROLLABLE);

  const lv_coord_t sc_gap = 2;
  const lv_coord_t sc_btn_w = (main_w - 6 - ((kShortcutCount - 1) * sc_gap)) / kShortcutCount;
  for (uint8_t i = 0; i < kShortcutCount; i++) {
    shortcut_btns_[i] = lv_btn_create(shortcut_strip_);
    lv_obj_set_size(shortcut_btns_[i], sc_btn_w, shortcut_h - 4);
    lv_obj_set_pos(shortcut_btns_[i], 2 + i * (sc_btn_w + sc_gap), 2);
    lv_obj_add_style(shortcut_btns_[i], &style_button_, 0);
    lv_obj_add_style(shortcut_btns_[i], &style_button_focused_, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(shortcut_btns_[i], onFocusableEvent, LV_EVENT_KEY, this);
    lv_obj_add_event_cb(shortcut_btns_[i], onFocusableEvent, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(shortcut_btns_[i], onFocusableEvent, LV_EVENT_FOCUSED, this);

    shortcut_labels_[i] = lv_label_create(shortcut_btns_[i]);
    lv_obj_add_style(shortcut_labels_[i], &style_text_dim_, 0);
    lv_label_set_text(shortcut_labels_[i], kShortcutNames[i]);
    lv_obj_center(shortcut_labels_[i]);
  }

  refreshChannelVisuals();
  refreshShortcutVisuals();
  refreshHeaderVisuals();
}

void StandaloneUi::bindInputGroup() {
  if (key_group_ != nullptr) {
    lv_group_del(key_group_);
    key_group_ = nullptr;
  }

  key_group_ = lv_group_create();
  if (!key_group_) {
    return;
  }

  lv_group_add_obj(key_group_, channel_selector_btn_);
  for (uint8_t i = 0; i < configured_channel_count_; i++) {
    lv_group_add_obj(key_group_, channel_dropdown_rows_[i]);
  }
  lv_group_add_obj(key_group_, chat_panel_);
  if (compose_input_) {
    lv_group_add_obj(key_group_, compose_input_);
  }
  for (uint8_t i = 0; i < kCfgRowCount; i++) {
    if (cfg_rows_[i]) {
      lv_group_add_obj(key_group_, cfg_rows_[i]);
    }
  }
  for (uint8_t i = 0; i < kShortcutCount; i++) {
    lv_group_add_obj(key_group_, shortcut_btns_[i]);
  }

  for (lv_indev_t* indev = lv_indev_get_next(nullptr); indev; indev = lv_indev_get_next(indev)) {
    lv_indev_type_t type = lv_indev_get_type(indev);
    if (type == LV_INDEV_TYPE_KEYPAD || type == LV_INDEV_TYPE_ENCODER) {
      lv_indev_set_group(indev, key_group_);
    }
  }

  focusCurrentZoneObject();
}

bool StandaloneUi::begin() {
  if (started_) {
    return true;
  }

#if PLUMERIA_KEY_DEBUG
  Serial.println("[KEYUI] debug=1 ui begin");
#endif

  if (configured_channel_count_ == 0) {
    const char defaults[1][32] = {
        "Public",
    };
    setChannels(defaults, 1);
  }

  if (!createStyles()) {
    return false;
  }

  buildLayout();
  loadChatHistoryFromFs();
  bindInputGroup();
  active_channel_ = selected_channel_;
  rebuildChatForActiveChannel();
  last_chat_persist_ms_ = millis();

  started_ = true;
  return true;
}

void StandaloneUi::setChannels(const char names[][32], size_t count) {
  configured_channel_count_ = 0;
  memset(configured_channel_names_, 0, sizeof(configured_channel_names_));

  if (names && count > 0) {
    const size_t capped_count = count > kChannelCount ? kChannelCount : count;
    for (size_t i = 0; i < capped_count; i++) {
      if (names[i][0] == '\0') {
        continue;
      }
      strncpy(configured_channel_names_[configured_channel_count_], names[i],
              sizeof(configured_channel_names_[configured_channel_count_]) - 1);
      configured_channel_names_[configured_channel_count_][sizeof(configured_channel_names_[configured_channel_count_]) -
                                                          1] = '\0';
      configured_channel_count_++;
    }
  }

  if (configured_channel_count_ == 0) {
    strncpy(configured_channel_names_[0], "Public", sizeof(configured_channel_names_[0]) - 1);
    configured_channel_count_ = 1;
  }

  if (selected_channel_ >= configured_channel_count_) {
    selected_channel_ = 0;
  }
  if (active_channel_ >= configured_channel_count_) {
    active_channel_ = selected_channel_;
  }

  dropdown_highlight_channel_ = selected_channel_;
  memset(unread_channels_, 0, sizeof(unread_channels_));

  if (started_) {
    closeChannelDropdown(false);
    refreshDropdownVisuals();
    refreshChannelVisuals();
    bindInputGroup();
    rebuildChatForActiveChannel();
  }
}

void StandaloneUi::refreshComposeDialog() {
  if (!compose_title_label_) {
    return;
  }

  char title[64];
  snprintf(title, sizeof(title), "Send to %s", compose_target_channel_[0] ? compose_target_channel_ : "-");
  lv_label_set_text(compose_title_label_, title);
}

void StandaloneUi::refreshChannelVisuals() {
  refreshSelectorVisuals();
  refreshDropdownVisuals();
}

void StandaloneUi::refreshSelectorVisuals() {
  if (!channel_selector_btn_ || !channel_selector_label_ || !channel_selector_caret_) {
    return;
  }

  const char* active_name = (configured_channel_count_ > 0) ? configured_channel_names_[active_channel_] : "-";
  lv_label_set_text(channel_selector_label_, active_name);
  lv_label_set_text(channel_selector_caret_, "");

  const size_t name_len = strlen(active_name);
  const lv_coord_t selector_w =
      clampCoord(static_cast<lv_coord_t>(name_len * 7 + 16), kSelectorMinW, kSelectorMaxW);
  lv_obj_set_width(channel_selector_btn_, selector_w);
  lv_obj_align(channel_selector_btn_, LV_ALIGN_LEFT_MID, 2, 0);
  lv_obj_set_width(channel_dropdown_panel_, selector_w + 12);

  lv_obj_remove_style(channel_selector_btn_, &style_button_active_, 0);
  if (channel_dropdown_open_) {
    lv_obj_add_style(channel_selector_btn_, &style_button_active_, 0);
  }
}

void StandaloneUi::refreshDropdownVisuals() {
  if (!channel_dropdown_panel_) {
    return;
  }

  const uint8_t visible_rows =
      configured_channel_count_ == 0 ? 1
                                     : (configured_channel_count_ > kDropdownVisibleRows ? kDropdownVisibleRows
                                                                                           : configured_channel_count_);
  const bool needs_scroll = configured_channel_count_ > kDropdownVisibleRows;
  lv_obj_set_height(channel_dropdown_panel_,
            static_cast<lv_coord_t>(visible_rows * kDropdownRowH + kDropdownPanelPadY + kDropdownBottomPad));
  lv_obj_set_scrollbar_mode(channel_dropdown_list_, needs_scroll ? LV_SCROLLBAR_MODE_ACTIVE
                                                                  : LV_SCROLLBAR_MODE_OFF);
  if (needs_scroll) {
    lv_obj_add_flag(channel_dropdown_list_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(channel_dropdown_list_, LV_DIR_VER);
    lv_obj_add_flag(channel_dropdown_list_, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_add_flag(channel_dropdown_list_, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  } else {
    lv_obj_set_scroll_dir(channel_dropdown_list_, LV_DIR_NONE);
    lv_obj_clear_flag(channel_dropdown_list_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(channel_dropdown_list_, LV_OBJ_FLAG_SCROLL_ELASTIC);
    lv_obj_clear_flag(channel_dropdown_list_, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_scroll_to_y(channel_dropdown_list_, 0, LV_ANIM_OFF);
  }

  for (uint8_t i = 0; i < kChannelCount; i++) {
    if (i >= configured_channel_count_) {
      lv_obj_add_flag(channel_dropdown_rows_[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    lv_obj_clear_flag(channel_dropdown_rows_[i], LV_OBJ_FLAG_HIDDEN);

    char row_text[48];
    snprintf(row_text, sizeof(row_text), "%s%s", configured_channel_names_[i],
         unread_channels_[i] ? " !" : "");
    lv_label_set_text(channel_dropdown_labels_[i], row_text);

    lv_obj_remove_style(channel_dropdown_rows_[i], &style_dropdown_active_, 0);
    lv_obj_remove_style(channel_dropdown_rows_[i], &style_dropdown_highlight_, 0);

    if (i == active_channel_) {
      lv_obj_add_style(channel_dropdown_rows_[i], &style_dropdown_active_, 0);
    }
    if (channel_dropdown_open_ && i == dropdown_highlight_channel_) {
      lv_obj_add_style(channel_dropdown_rows_[i], &style_dropdown_highlight_, 0);
      lv_obj_scroll_to_view(channel_dropdown_rows_[i], LV_ANIM_OFF);
    }
  }

  if (channel_dropdown_open_) {
    lv_obj_clear_flag(channel_dropdown_panel_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(channel_dropdown_panel_, LV_OBJ_FLAG_HIDDEN);
  }
}

void StandaloneUi::openChannelDropdown() {
  if (channel_dropdown_open_ || configured_channel_count_ == 0) {
    return;
  }

  channel_dropdown_open_ = true;
  dropdown_highlight_channel_ = selected_channel_;
  refreshDropdownVisuals();
  refreshChannelVisuals();
  focusCurrentZoneObject();
}

void StandaloneUi::closeChannelDropdown(bool keep_highlight) {
  if (!channel_dropdown_open_) {
    return;
  }

  channel_dropdown_open_ = false;
  if (!keep_highlight) {
    dropdown_highlight_channel_ = selected_channel_;
  }
  refreshChannelVisuals();
}

void StandaloneUi::moveDropdownHighlight(int delta) {
  if (configured_channel_count_ == 0) {
    return;
  }

  int next = static_cast<int>(dropdown_highlight_channel_) + delta;
  if (next < 0) {
    next = configured_channel_count_ - 1;
  } else if (next >= configured_channel_count_) {
    next = 0;
  }

  dropdown_highlight_channel_ = static_cast<uint8_t>(next);
  refreshDropdownVisuals();
}

void StandaloneUi::openComposeDialog() {
  if (compose_open_ || !compose_dialog_ || !compose_input_ || configured_channel_count_ == 0 ||
      active_channel_ >= configured_channel_count_) {
    return;
  }

  strncpy(compose_target_channel_, configured_channel_names_[active_channel_], sizeof(compose_target_channel_) - 1);
  compose_target_channel_[sizeof(compose_target_channel_) - 1] = '\0';
  lv_textarea_set_text(compose_input_, "");
  refreshComposeDialog();

  compose_open_ = true;
  lv_obj_clear_flag(compose_dialog_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(compose_dialog_);
  if (key_group_) {
    lv_group_focus_obj(compose_input_);
  }
}

void StandaloneUi::closeComposeDialog(bool restore_chat_focus) {
  if (!compose_open_ || !compose_dialog_ || !compose_input_) {
    return;
  }

  compose_open_ = false;
  lv_obj_add_flag(compose_dialog_, LV_OBJ_FLAG_HIDDEN);
  lv_textarea_set_text(compose_input_, "");

  if (restore_chat_focus) {
    focus_zone_ = FocusZone::Chat;
    focusCurrentZoneObject();
    refreshShortcutVisuals();
  }
}

bool StandaloneUi::sendComposeMessage() {
  if (!compose_input_) {
    return false;
  }

  const char* text = lv_textarea_get_text(compose_input_);
  if (!text || text[0] == '\0') {
    return false;
  }

  const bool sent = mesh_adapter_ && mesh_adapter_->sendChannelMessage(compose_target_channel_, text);
  if (!sent) {
    appendChatLine("[ERR] Send failed", ChatLineKind::Error);
    return false;
  }

  char display_text[96];
  snprintf(display_text, sizeof(display_text), "Me: %s", text);

  strncpy(pending_local_echo_channel_, compose_target_channel_, sizeof(pending_local_echo_channel_) - 1);
  pending_local_echo_channel_[sizeof(pending_local_echo_channel_) - 1] = '\0';
  strncpy(pending_local_echo_text_, text, sizeof(pending_local_echo_text_) - 1);
  pending_local_echo_text_[sizeof(pending_local_echo_text_) - 1] = '\0';
  pending_local_echo_deadline_ms_ = millis() + kLocalEchoSuppressMs;

  pushChannelHistoryLine(compose_target_channel_, display_text, ChatLineKind::Tx);
  if (strcmp(compose_target_channel_, configured_channel_names_[active_channel_]) == 0) {
    appendChatLine(display_text, ChatLineKind::Tx);
  }
  return true;
}

bool StandaloneUi::handleComposeKey(uint32_t key) {
  if (!compose_open_ || !compose_input_) {
    return false;
  }

  const bool is_backspace = (key == LV_KEY_BACKSPACE || key == 8 || key == 127);

  if (key == LV_KEY_ENTER || key == '\n' || key == '\r') {
    if (sendComposeMessage()) {
      closeComposeDialog(true);
    }
    return true;
  }

  if (key == LV_KEY_ESC) {
    closeComposeDialog(true);
    return true;
  }

  if (is_backspace) {
    const char* text = lv_textarea_get_text(compose_input_);
    if (!text || text[0] == '\0') {
      closeComposeDialog(true);
      return true;
    }
    return true;
  }

  return true;
}

void StandaloneUi::refreshCfgDialog() {
  if (!cfg_dialog_ || !cfg_status_label_ || !cfg_action_label_) {
    return;
  }

  char node_name[32] = {};
  if (mesh_adapter_) {
    mesh_adapter_->getNodeName(node_name, sizeof(node_name));
  }

  plumeria::web::WebSettings web_settings{};
  plumeria::web::loadSettings(&web_settings);

  char row_text[96] = {};
  snprintf(row_text, sizeof(row_text), "Name: %s", node_name[0] ? node_name : "-");
  lv_label_set_text(cfg_row_labels_[0], row_text);

  snprintf(row_text, sizeof(row_text), "Radio Preset: %s", radioPresetDisplayName(web_settings.region));
  lv_label_set_text(cfg_row_labels_[1], row_text);

  const bool web_on = plumeria::web::running();
  const char* web_ip = plumeria::web::ip();
  if (!web_on) {
    snprintf(row_text, sizeof(row_text), "Web Config: OFF");
  } else if (web_ip && web_ip[0] != '\0') {
    snprintf(row_text, sizeof(row_text), "Web Config: ON (%s)", web_ip);
  } else {
    snprintf(row_text, sizeof(row_text), "Web Config: ON");
  }
  lv_label_set_text(cfg_row_labels_[2], row_text);

  lv_label_set_text(cfg_row_labels_[3], web_settings.send_location_in_advert ? "GPS Advert: ON"
                                                                              : "GPS Advert: OFF");
#if defined(DEVICE_HELTEC_V4_EXPANSION)
  lv_label_set_text(cfg_row_labels_[4], "Export Config: N/A (no SD)");
  lv_label_set_text(cfg_row_labels_[5], "Import Config: N/A (no SD)");
#else
  lv_label_set_text(cfg_row_labels_[4], "Export Config -> SD /plumeria");
  lv_label_set_text(cfg_row_labels_[5], cfg_import_confirm_armed_ ? "Import Config [PRESS ENTER AGAIN]"
                                                                   : "Import Config <- SD /plumeria");
#endif

  for (uint8_t i = 0; i < kCfgRowCount; i++) {
    lv_obj_remove_style(cfg_rows_[i], &style_button_active_, 0);
    if (i == cfg_selected_row_) {
      lv_obj_add_style(cfg_rows_[i], &style_button_active_, 0);
    }
  }

  lv_label_set_text(cfg_status_label_, "Enter - Activate Option, Backspace - Close Configuration");
  lv_label_set_text(cfg_action_label_, cfg_action_text_);
}

void StandaloneUi::openCfgDialog() {
  if (cfg_open_ || !cfg_dialog_) {
    return;
  }
  cfg_open_ = true;
  cfg_selected_row_ = 0;
  cfg_import_confirm_armed_ = false;
  cfg_action_text_[0] = '\0';
  cfg_status_text_[0] = '\0';
  lv_obj_clear_flag(cfg_dialog_, LV_OBJ_FLAG_HIDDEN);
  if (shortcut_strip_) {
    lv_obj_add_flag(shortcut_strip_, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_move_foreground(cfg_dialog_);
  refreshCfgDialog();
  if (key_group_ && cfg_rows_[cfg_selected_row_]) {
    lv_group_focus_obj(cfg_rows_[cfg_selected_row_]);
  }
}

void StandaloneUi::closeCfgDialog(bool focus_chat) {
  if (!cfg_open_ || !cfg_dialog_) {
    return;
  }
  cfg_open_ = false;
  cfg_import_confirm_armed_ = false;
  lv_obj_add_flag(cfg_dialog_, LV_OBJ_FLAG_HIDDEN);
  if (shortcut_strip_) {
    lv_obj_clear_flag(shortcut_strip_, LV_OBJ_FLAG_HIDDEN);
  }
  if (focus_chat) {
    setFocusZone(FocusZone::Chat);
  } else {
    focus_zone_ = FocusZone::Shortcuts;
    selected_shortcut_ = 0;
    refreshShortcutVisuals();
    focusCurrentZoneObject();
  }
}

void StandaloneUi::moveCfgSelection(int delta) {
  int next = static_cast<int>(cfg_selected_row_) + delta;
  if (next < 0) {
    next = kCfgRowCount - 1;
  } else if (next >= static_cast<int>(kCfgRowCount)) {
    next = 0;
  }
  cfg_selected_row_ = static_cast<uint8_t>(next);
  if (cfg_selected_row_ != 5) {
    cfg_import_confirm_armed_ = false;
  }
  refreshCfgDialog();
  if (key_group_ && cfg_rows_[cfg_selected_row_]) {
    lv_group_focus_obj(cfg_rows_[cfg_selected_row_]);
  }
}

bool StandaloneUi::exportConfigToSd() {
  String text;
  if (!plumeria::web::exportConfigText(&text) || text.length() == 0) {
    return false;
  }

  char sd_err[64] = {};
  if (!sdBeginForCurrentBoard(sd_err, sizeof(sd_err))) {
    Serial.printf("[CFG] export SD init failed: %s\n", sd_err);
    return false;
  }

  if (!SD.exists(kCfgSdDir)) {
    if (!SD.mkdir(kCfgSdDir)) {
      return false;
    }
  }

  if (SD.exists(kCfgSdPath)) {
    SD.remove(kCfgSdPath);
  }

  File file = SD.open(kCfgSdPath, FILE_WRITE);
  if (!file) {
    return false;
  }

  const size_t wrote = file.print(text);
  file.close();
  return wrote == static_cast<size_t>(text.length());
}

bool StandaloneUi::setGpsAdvertEnabled(bool enabled) {
  plumeria::web::WebSettings web_settings{};
  plumeria::web::loadSettings(&web_settings);
  if (web_settings.send_location_in_advert == enabled) {
    return true;
  }

  // Apply immediately to runtime mesh even when web config is currently disabled.
  if (mesh_adapter_ && !mesh_adapter_->setAdvertLocation(enabled, web_settings.node_latitude, web_settings.node_longitude)) {
    return false;
  }
  if (mesh_adapter_) {
    mesh_adapter_->broadcastSelfAdvertNow();
  }

  char err[96] = {};
  return plumeria::web::setSendLocationInAdvert(enabled, err, sizeof(err));
}

bool StandaloneUi::importConfigFromSd() {
  char sd_err[64] = {};
  if (!sdBeginForCurrentBoard(sd_err, sizeof(sd_err))) {
    snprintf(cfg_status_text_, sizeof(cfg_status_text_), "Import failed: %s", sd_err[0] ? sd_err : "SD init failed");
    cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
    return false;
  }

  File file = SD.open(kCfgSdPath, FILE_READ);
  if (!file) {
    strncpy(cfg_status_text_, "Import failed: /plumeria/plumeria-config.yaml missing", sizeof(cfg_status_text_) - 1);
    cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
    return false;
  }

  String text;
  text.reserve(4096);
  while (file.available()) {
    text += static_cast<char>(file.read());
  }
  file.close();

  char err[96] = {};
  if (!plumeria::web::importConfigText(text.c_str(), true, err, sizeof(err))) {
    strncpy(cfg_status_text_, err[0] ? err : "Import failed", sizeof(cfg_status_text_) - 1);
    cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
    return false;
  }

  return true;
}

void StandaloneUi::activateCfgSelection() {
  if (cfg_selected_row_ != 5) {
    cfg_import_confirm_armed_ = false;
  }

  switch (cfg_selected_row_) {
    case 0:
    case 1:
      strncpy(cfg_status_text_, "Read-only row. Use web config to edit settings.", sizeof(cfg_status_text_) - 1);
      cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
      strncpy(cfg_action_text_, "No change", sizeof(cfg_action_text_) - 1);
      cfg_action_text_[sizeof(cfg_action_text_) - 1] = '\0';
      break;
    case 2: {
      if (plumeria::web::running()) {
        plumeria::web::end();
        strncpy(cfg_status_text_, "Web Config disabled", sizeof(cfg_status_text_) - 1);
        strncpy(cfg_action_text_, "Web disabled", sizeof(cfg_action_text_) - 1);
      } else {
        plumeria::web::WebSettings web_settings{};
        plumeria::web::loadSettings(&web_settings);
        if (plumeria::web::begin(mesh_adapter_, web_settings)) {
          const char* web_ip = plumeria::web::ip();
          if (web_ip && web_ip[0] != '\0') {
            snprintf(cfg_status_text_, sizeof(cfg_status_text_),
                     "Web Config enabled (%s)",
                     web_ip);
          } else {
            strncpy(cfg_status_text_, "Web Config enabled", sizeof(cfg_status_text_) - 1);
            cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
          }
          strncpy(cfg_action_text_, "Web enabled", sizeof(cfg_action_text_) - 1);
        } else {
          strncpy(cfg_status_text_, "Web Config start failed", sizeof(cfg_status_text_) - 1);
          strncpy(cfg_action_text_, "Web start failed", sizeof(cfg_action_text_) - 1);
        }
      }
      cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
      cfg_action_text_[sizeof(cfg_action_text_) - 1] = '\0';
      break;
    }
    case 3: {
      plumeria::web::WebSettings web_settings{};
      plumeria::web::loadSettings(&web_settings);
      const bool next_enabled = !web_settings.send_location_in_advert;
      if (setGpsAdvertEnabled(next_enabled)) {
        strncpy(cfg_status_text_, next_enabled ? "GPS advert enabled" : "GPS advert disabled",
                sizeof(cfg_status_text_) - 1);
        strncpy(cfg_action_text_, next_enabled ? "GPS turned on" : "GPS turned off", sizeof(cfg_action_text_) - 1);
      } else {
        strncpy(cfg_status_text_, "GPS toggle failed", sizeof(cfg_status_text_) - 1);
        strncpy(cfg_action_text_, "GPS toggle failed", sizeof(cfg_action_text_) - 1);
      }
      cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
      cfg_action_text_[sizeof(cfg_action_text_) - 1] = '\0';
      break;
    }
    case 4:
#if defined(DEVICE_HELTEC_V4_EXPANSION)
      strncpy(cfg_status_text_, "Export unavailable on this hardware", sizeof(cfg_status_text_) - 1);
      strncpy(cfg_action_text_, "Export unavailable", sizeof(cfg_action_text_) - 1);
      cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
      cfg_action_text_[sizeof(cfg_action_text_) - 1] = '\0';
#else
      if (exportConfigToSd()) {
        strncpy(cfg_status_text_, "Exported to /plumeria/plumeria-config.yaml", sizeof(cfg_status_text_) - 1);
        strncpy(cfg_action_text_, "Config exported", sizeof(cfg_action_text_) - 1);
      } else {
        strncpy(cfg_status_text_, "Export failed", sizeof(cfg_status_text_) - 1);
        strncpy(cfg_action_text_, "Export failed", sizeof(cfg_action_text_) - 1);
      }
      cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
      cfg_action_text_[sizeof(cfg_action_text_) - 1] = '\0';
#endif
      break;
  case 5:
#if defined(DEVICE_HELTEC_V4_EXPANSION)
      strncpy(cfg_status_text_, "Import unavailable on this hardware", sizeof(cfg_status_text_) - 1);
      strncpy(cfg_action_text_, "Import unavailable", sizeof(cfg_action_text_) - 1);
      cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
      cfg_action_text_[sizeof(cfg_action_text_) - 1] = '\0';
#else
      if (!cfg_import_confirm_armed_) {
        cfg_import_confirm_armed_ = true;
        strncpy(cfg_status_text_, "Press Enter again to confirm import", sizeof(cfg_status_text_) - 1);
        cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
        strncpy(cfg_action_text_, "Import armed", sizeof(cfg_action_text_) - 1);
        cfg_action_text_[sizeof(cfg_action_text_) - 1] = '\0';
      } else if (importConfigFromSd()) {
        cfg_import_confirm_armed_ = false;
        strncpy(cfg_status_text_, "Import applied. Rebooting...", sizeof(cfg_status_text_) - 1);
        cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
        strncpy(cfg_action_text_, "Config imported", sizeof(cfg_action_text_) - 1);
        cfg_action_text_[sizeof(cfg_action_text_) - 1] = '\0';
        refreshCfgDialog();
        delay(120);
        ESP.restart();
      } else {
        strncpy(cfg_action_text_, "Import failed", sizeof(cfg_action_text_) - 1);
        cfg_action_text_[sizeof(cfg_action_text_) - 1] = '\0';
      }
#endif
      break;
    default:
      break;
  }

  refreshCfgDialog();
}

void StandaloneUi::scrollChatUp() {
  if (!chat_panel_) {
    return;
  }
  lv_obj_scroll_by(chat_panel_, 0, kMsgScrollStep, LV_ANIM_OFF);
}

void StandaloneUi::scrollChatDown() {
  if (!chat_panel_) {
    return;
  }
  lv_obj_scroll_by(chat_panel_, 0, -kMsgScrollStep, LV_ANIM_OFF);
}

void StandaloneUi::refreshShortcutVisuals() {
  for (uint8_t i = 0; i < kShortcutCount; i++) {
    lv_obj_remove_style(shortcut_btns_[i], &style_shortcut_active_, 0);
    if (focus_zone_ == FocusZone::Shortcuts && i == selected_shortcut_) {
      lv_obj_add_style(shortcut_btns_[i], &style_shortcut_active_, 0);
    }
  }
}

void StandaloneUi::refreshHeaderVisuals() {
  if (!channel_selector_btn_ || !gps_label_ || !wifi_label_ || !time_label_ || !battery_pct_label_ ||
      !battery_bar_ || !wifi_ap_badge_label_) {
    return;
  }

  lv_label_set_text(gps_label_, LV_SYMBOL_GPS);
  lv_label_set_text(wifi_label_, LV_SYMBOL_WIFI);
  lv_obj_align_to(gps_label_, channel_selector_btn_, LV_ALIGN_OUT_RIGHT_MID, kHeaderTimeGap, 0);
  lv_obj_align_to(wifi_label_, gps_label_, LV_ALIGN_OUT_RIGHT_MID, kHeaderIconsGap, 0);
  lv_obj_align_to(time_label_, battery_pct_label_, LV_ALIGN_OUT_LEFT_MID, -kHeaderIconsToBatteryGap, 0);
  lv_obj_align(battery_pct_label_, LV_ALIGN_RIGHT_MID, kHeaderBatteryTextX, 0);
  lv_obj_align(battery_bar_, LV_ALIGN_RIGHT_MID, kHeaderBatteryBarX, 0);
  lv_obj_align_to(wifi_ap_badge_label_, wifi_label_, LV_ALIGN_TOP_RIGHT, 4, -4);

  lv_obj_set_style_text_color(gps_label_, gps_ok_ ? kColorTextMain : kColorTextDim, 0);
  if (!wifi_config_server_on_) {
    lv_obj_set_style_text_color(wifi_label_, kColorWifiOff, 0);
    lv_obj_add_flag(wifi_ap_badge_label_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_set_style_text_color(wifi_label_, kColorWifiOn, 0);
    if (wifi_ap_mode_ || !wifi_ok_) {
      lv_obj_set_style_text_color(wifi_ap_badge_label_, kColorWifiApBadge, 0);
      lv_obj_clear_flag(wifi_ap_badge_label_, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(wifi_ap_badge_label_, LV_OBJ_FLAG_HIDDEN);
    }
  }

  char batt_text[8];
  snprintf(batt_text, sizeof(batt_text), "%u%%", static_cast<unsigned>(battery_pct_));
  lv_label_set_text(battery_pct_label_, batt_text);

  lv_bar_set_value(battery_bar_, battery_pct_, LV_ANIM_OFF);
}

void StandaloneUi::refreshClockIfNeeded(uint32_t now_ms) {
  if (now_ms - last_clock_update_ms_ < 1000) {
    return;
  }
  last_clock_update_ms_ = now_ms;

  uint8_t hh = 0;
  uint8_t mm = 0;
  time_t now_time = time(nullptr);
  if (now_time >= kTimeValidEpoch) {
    struct tm tm_now{};
    localtime_r(&now_time, &tm_now);
    hh = static_cast<uint8_t>(tm_now.tm_hour);
    mm = static_cast<uint8_t>(tm_now.tm_min);
  } else {
    const uint32_t now_min = now_ms / 60000UL;
    hh = static_cast<uint8_t>((now_min / 60UL) % 24UL);
    mm = static_cast<uint8_t>(now_min % 60UL);
  }

  const uint16_t minute_key = static_cast<uint16_t>(hh * 60U + mm);
  if (minute_key == last_clock_minute_) {
    return;
  }
  last_clock_minute_ = minute_key;

  char text[6];
  snprintf(text, sizeof(text), "%02u:%02u", static_cast<unsigned>(hh), static_cast<unsigned>(mm));
  lv_label_set_text(time_label_, text);
}

void StandaloneUi::syncChannelsFromMeshIfNeeded(uint32_t now_ms) {
  if (!mesh_ready_ || !mesh_adapter_) {
    return;
  }

  if (now_ms - last_channel_sync_ms_ < kChannelSyncMs) {
    return;
  }
  last_channel_sync_ms_ = now_ms;

  char channel_names[kChannelCount][32]{};
  const int exported = mesh_adapter_->exportChannels(channel_names, kChannelCount);
  const uint8_t exported_count =
      exported > 0 ? static_cast<uint8_t>(exported) : static_cast<uint8_t>(0);

  bool changed = exported_count != configured_channel_count_;
  if (!changed) {
    for (uint8_t i = 0; i < exported_count; i++) {
      if (strcmp(configured_channel_names_[i], channel_names[i]) != 0) {
        changed = true;
        break;
      }
    }
  }

  if (!changed) {
    return;
  }

  setChannels(channel_names, exported_count);
}

void StandaloneUi::focusCurrentZoneObject() {
  if (!key_group_) {
    return;
  }

  if (cfg_open_ && cfg_rows_[cfg_selected_row_]) {
    lv_group_focus_obj(cfg_rows_[cfg_selected_row_]);
    return;
  }

  if (compose_open_ && compose_input_) {
    lv_group_focus_obj(compose_input_);
    return;
  }

  if (channel_dropdown_open_ && configured_channel_count_ > 0) {
    lv_group_focus_obj(channel_dropdown_rows_[dropdown_highlight_channel_]);
    return;
  }

  switch (focus_zone_) {
    case FocusZone::Selector:
      lv_group_focus_obj(channel_selector_btn_);
      break;
    case FocusZone::Chat:
      lv_group_focus_obj(chat_panel_);
      break;
    case FocusZone::Shortcuts:
      lv_group_focus_obj(shortcut_btns_[selected_shortcut_]);
      break;
  }
}

void StandaloneUi::setFocusZone(FocusZone zone) {
  if (zone != FocusZone::Selector && channel_dropdown_open_) {
    closeChannelDropdown(true);
  }
  focus_zone_ = zone;
  focusCurrentZoneObject();
  refreshShortcutVisuals();
}

void StandaloneUi::focusPrevZone() {
  switch (focus_zone_) {
    case FocusZone::Selector:
      setFocusZone(FocusZone::Shortcuts);
      break;
    case FocusZone::Chat:
      setFocusZone(FocusZone::Selector);
      break;
    case FocusZone::Shortcuts:
      setFocusZone(FocusZone::Chat);
      break;
  }
}

void StandaloneUi::focusNextZone() {
  switch (focus_zone_) {
    case FocusZone::Selector:
      setFocusZone(FocusZone::Chat);
      break;
    case FocusZone::Chat:
      setFocusZone(FocusZone::Shortcuts);
      break;
    case FocusZone::Shortcuts:
      setFocusZone(FocusZone::Selector);
      break;
  }
}

void StandaloneUi::selectChannel(int index, bool activate) {
  if (configured_channel_count_ == 0) {
    return;
  }

  if (index < 0) {
    index = configured_channel_count_ - 1;
  }
  if (index >= configured_channel_count_) {
    index = 0;
  }

  selected_channel_ = static_cast<uint8_t>(index);
  dropdown_highlight_channel_ = selected_channel_;

  if (activate) {
    active_channel_ = selected_channel_;
    unread_channels_[active_channel_] = false;
    rebuildChatForActiveChannel();
    pending_chat_focus_attempts_ = 8;
  }

  refreshChannelVisuals();
}

void StandaloneUi::triggerShortcut(uint8_t index) {
  if (index >= kShortcutCount) {
    return;
  }
  closeChannelDropdown(true);

  if (index == 0) {
    openCfgDialog();
    return;
  }

  if (index == 1) {
    appendChatLine("[INFO] CONTACTS selected", ChatLineKind::Normal);
    return;
  }
}

void StandaloneUi::appendChatLine(const char* text, ChatLineKind kind) {
  if (!text || text[0] == '\0' || !chat_panel_) {
    return;
  }

  const bool at_bottom = lv_obj_get_scroll_bottom(chat_panel_) <= 2;
  const lv_coord_t scroll_y = lv_obj_get_scroll_y(chat_panel_);

  if (chat_row_count_ >= kMaxChatRows) {
    lv_obj_del(chat_rows_[0]);
    for (size_t i = 1; i < chat_row_count_; i++) {
      chat_rows_[i - 1] = chat_rows_[i];
    }
    chat_row_count_--;
  }

  lv_obj_t* row = lv_label_create(chat_panel_);
  lv_obj_set_width(row, LV_PCT(100));
  lv_label_set_long_mode(row, LV_LABEL_LONG_WRAP);
  lv_obj_add_style(row, &style_text_main_, 0);
  lv_obj_set_style_text_font(row, chatPanelFont(), 0);

  switch (kind) {
    case ChatLineKind::Rx:
      lv_obj_add_style(row, &style_msg_rx_, 0);
      break;
    case ChatLineKind::Tx:
      lv_obj_add_style(row, &style_msg_tx_, 0);
      break;
    case ChatLineKind::Ack:
      lv_obj_add_style(row, &style_msg_ack_, 0);
      break;
    case ChatLineKind::Error:
      lv_obj_add_style(row, &style_msg_err_, 0);
      break;
    case ChatLineKind::Normal:
    default:
      break;
  }

  lv_label_set_text(row, text);
  chat_rows_[chat_row_count_++] = row;

  if (at_bottom) {
    lv_obj_scroll_to_y(chat_panel_, LV_COORD_MAX, LV_ANIM_OFF);
  } else {
    lv_obj_scroll_to_y(chat_panel_, scroll_y, LV_ANIM_OFF);
  }
}

int StandaloneUi::findConfiguredChannelIndex(const char* channel_name) const {
  if (!channel_name || channel_name[0] == '\0') {
    return -1;
  }

  for (uint8_t i = 0; i < configured_channel_count_; i++) {
    if (strcmp(configured_channel_names_[i], channel_name) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void StandaloneUi::pushChannelHistoryLine(const char* channel_name, const char* text, ChatLineKind kind) {
  if (!channel_name || channel_name[0] == '\0' || !text || text[0] == '\0') {
    return;
  }

  if (stored_chat_count_ < kMaxStoredChatRows) {
    size_t write_index = (stored_chat_head_ + stored_chat_count_) % kMaxStoredChatRows;
    strncpy(stored_chat_[write_index].channel_name, channel_name, sizeof(stored_chat_[write_index].channel_name) - 1);
    stored_chat_[write_index].channel_name[sizeof(stored_chat_[write_index].channel_name) - 1] = '\0';
    strncpy(stored_chat_[write_index].text, text, sizeof(stored_chat_[write_index].text) - 1);
    stored_chat_[write_index].text[sizeof(stored_chat_[write_index].text) - 1] = '\0';
    stored_chat_[write_index].kind = kind;
    stored_chat_count_++;
    chat_history_dirty_ = true;
    return;
  }

  strncpy(stored_chat_[stored_chat_head_].channel_name, channel_name,
          sizeof(stored_chat_[stored_chat_head_].channel_name) - 1);
  stored_chat_[stored_chat_head_].channel_name[sizeof(stored_chat_[stored_chat_head_].channel_name) - 1] = '\0';
  strncpy(stored_chat_[stored_chat_head_].text, text, sizeof(stored_chat_[stored_chat_head_].text) - 1);
  stored_chat_[stored_chat_head_].text[sizeof(stored_chat_[stored_chat_head_].text) - 1] = '\0';
  stored_chat_[stored_chat_head_].kind = kind;
  stored_chat_head_ = (stored_chat_head_ + 1) % kMaxStoredChatRows;
  chat_history_dirty_ = true;
}

bool StandaloneUi::loadChatHistoryFromFs() {
  Preferences prefs;
  if (!prefs.begin(kUiPrefsNs, true)) {
    return false;
  }

  const size_t blob_len = prefs.getBytesLength(kChatHistoryBlobKey);
  if (blob_len < sizeof(PersistedChatLine)) {
    prefs.end();
    return false;
  }

  int count = static_cast<int>(blob_len / sizeof(PersistedChatLine));
  const uint16_t declared = prefs.getUShort(kChatHistoryCountKey, static_cast<uint16_t>(count));
  if (declared > 0 && declared < static_cast<uint16_t>(count)) {
    count = declared;
  }

  if (count > static_cast<int>(kMaxStoredChatRows)) {
    count = static_cast<int>(kMaxStoredChatRows);
  }

  static PersistedChatLine persisted_buf[kMaxStoredChatRows]{};
  const size_t to_read = static_cast<size_t>(count) * sizeof(PersistedChatLine);
  memset(persisted_buf, 0, sizeof(persisted_buf));
  const size_t got = prefs.getBytes(kChatHistoryBlobKey, persisted_buf, to_read);
  prefs.end();

  const int loaded_count = static_cast<int>(got / sizeof(PersistedChatLine));
  if (loaded_count <= 0) {
    return false;
  }

  stored_chat_head_ = 0;
  stored_chat_count_ = 0;
  memset(stored_chat_, 0, sizeof(stored_chat_));

  for (int i = 0; i < loaded_count && stored_chat_count_ < kMaxStoredChatRows; i++) {
    const PersistedChatLine& persisted = persisted_buf[i];
    size_t write_index = (stored_chat_head_ + stored_chat_count_) % kMaxStoredChatRows;
    strncpy(stored_chat_[write_index].channel_name, persisted.channel_name,
            sizeof(stored_chat_[write_index].channel_name) - 1);
    stored_chat_[write_index].channel_name[sizeof(stored_chat_[write_index].channel_name) - 1] = '\0';
    strncpy(stored_chat_[write_index].text, persisted.text, sizeof(stored_chat_[write_index].text) - 1);
    stored_chat_[write_index].text[sizeof(stored_chat_[write_index].text) - 1] = '\0';
    stored_chat_[write_index].kind =
        (persisted.kind <= static_cast<uint8_t>(ChatLineKind::Error))
            ? static_cast<ChatLineKind>(persisted.kind)
            : ChatLineKind::Normal;
    stored_chat_count_++;
  }

  chat_history_dirty_ = false;
  return stored_chat_count_ > 0;
}

bool StandaloneUi::saveChatHistoryToFs() {
  Preferences prefs;
  if (!prefs.begin(kUiPrefsNs, false)) {
    return false;
  }

  static PersistedChatLine persisted_buf[kMaxStoredChatRows]{};
  const size_t count = stored_chat_count_ > kMaxStoredChatRows ? kMaxStoredChatRows : stored_chat_count_;
  memset(persisted_buf, 0, sizeof(persisted_buf));

  for (size_t i = 0; i < count; i++) {
    size_t idx = (stored_chat_head_ + i) % kMaxStoredChatRows;
    PersistedChatLine& persisted = persisted_buf[i];
    strncpy(persisted.channel_name, stored_chat_[idx].channel_name, sizeof(persisted.channel_name) - 1);
    persisted.channel_name[sizeof(persisted.channel_name) - 1] = '\0';
    strncpy(persisted.text, stored_chat_[idx].text, sizeof(persisted.text) - 1);
    persisted.text[sizeof(persisted.text) - 1] = '\0';
    persisted.kind = static_cast<uint8_t>(stored_chat_[idx].kind);
  }

  bool ok = true;
  if (count > 0) {
    const size_t blob_len = count * sizeof(PersistedChatLine);
    const size_t wrote = prefs.putBytes(kChatHistoryBlobKey, persisted_buf, blob_len);
    if (wrote != blob_len) {
      ok = false;
    }
  } else if (prefs.isKey(kChatHistoryBlobKey)) {
    if (!prefs.remove(kChatHistoryBlobKey)) {
      ok = false;
    }
  }

  prefs.putUShort(kChatHistoryCountKey, static_cast<uint16_t>(count));
  prefs.end();
  if (!ok) {
    return false;
  }

  chat_history_dirty_ = false;
  return true;
}

void StandaloneUi::clearChatPanel() {
  for (size_t i = 0; i < chat_row_count_; i++) {
    if (chat_rows_[i]) {
      lv_obj_del(chat_rows_[i]);
      chat_rows_[i] = nullptr;
    }
  }
  chat_row_count_ = 0;
}

void StandaloneUi::rebuildChatForActiveChannel() {
  if (!chat_panel_ || configured_channel_count_ == 0 || active_channel_ >= configured_channel_count_) {
    clearChatPanel();
    return;
  }

  clearChatPanel();
  const char* active_name = configured_channel_names_[active_channel_];

  for (size_t i = 0; i < stored_chat_count_; i++) {
    size_t idx = (stored_chat_head_ + i) % kMaxStoredChatRows;
    if (strcmp(stored_chat_[idx].channel_name, active_name) == 0) {
      appendChatLine(stored_chat_[idx].text, stored_chat_[idx].kind);
    }
  }
}

void StandaloneUi::handleKey(uint32_t key) {
  uint32_t norm_key = key;
  const bool is_lvgl_special =
      (norm_key == LV_KEY_UP || norm_key == LV_KEY_DOWN || norm_key == LV_KEY_LEFT || norm_key == LV_KEY_RIGHT ||
       norm_key == LV_KEY_ENTER || norm_key == LV_KEY_ESC || norm_key == LV_KEY_BACKSPACE ||
       norm_key == LV_KEY_DEL || norm_key == LV_KEY_HOME || norm_key == LV_KEY_END || norm_key == LV_KEY_NEXT ||
       norm_key == LV_KEY_PREV);
  if (!is_lvgl_special && norm_key >= 1 && norm_key <= 26) {
    // Support Ctrl+A..Ctrl+Z style values for shortcut keys without breaking LVGL key constants.
    norm_key = static_cast<uint32_t>('a' + (norm_key - 1));
  }
  if (norm_key >= 'A' && norm_key <= 'Z') {
    norm_key = norm_key - 'A' + 'a';
  }

#if PLUMERIA_KEY_DEBUG
  Serial.printf("[KEYUI] handleKey raw=%lu norm=%lu cfg=%d compose=%d dropdown=%d focus=%u\n",
                static_cast<unsigned long>(key), static_cast<unsigned long>(norm_key), cfg_open_ ? 1 : 0,
                compose_open_ ? 1 : 0, channel_dropdown_open_ ? 1 : 0, static_cast<unsigned>(focus_zone_));
#endif

  const uint32_t now = millis();
  const bool is_escape = (norm_key == LV_KEY_ESC || norm_key == 27 || norm_key == 8);
  lv_obj_t* focused = key_group_ ? lv_group_get_focused(key_group_) : nullptr;
  const bool chat_focused = (focused == chat_panel_);

  if (compose_open_) {
    handleComposeKey(key);
    return;
  }

  // Global shortcut: open CFG from any non-compose screen.
  if (norm_key == 'c') {
    selected_shortcut_ = 0;
    setFocusZone(FocusZone::Shortcuts);
    triggerShortcut(0);
    return;
  }

  // Global shortcut: open compose from any non-compose screen.
  if (norm_key == 'm') {
    setFocusZone(FocusZone::Chat);
    openComposeDialog();
    return;
  }

  if (cfg_open_) {
    if (norm_key == LV_KEY_UP || norm_key == 'k') {
      moveCfgSelection(-1);
      return;
    }
    if (norm_key == LV_KEY_DOWN || norm_key == 'j') {
      moveCfgSelection(1);
      return;
    }
    if (norm_key == LV_KEY_ENTER || norm_key == '\n' || norm_key == '\r') {
      activateCfgSelection();
      return;
    }
    if (norm_key == LV_KEY_ESC) {
      closeCfgDialog(false);
      return;
    }
    if (norm_key == LV_KEY_BACKSPACE || norm_key == 8 || norm_key == 127) {
      closeCfgDialog(true);
      return;
    }
    return;
  }

  if (channel_dropdown_open_) {
    if (norm_key == LV_KEY_UP || norm_key == 'k') {
      moveDropdownHighlight(-1);
      return;
    }
    if (norm_key == LV_KEY_DOWN || norm_key == 'j') {
      moveDropdownHighlight(1);
      return;
    }
    if (norm_key == LV_KEY_ENTER) {
      if (now - last_selector_action_ms_ < kNavDebounceMs) {
        return;
      }
      last_selector_action_ms_ = now;
      selectChannel(dropdown_highlight_channel_, true);
      closeChannelDropdown(true);
      return;
    }
    if (is_escape) {
      closeChannelDropdown(false);
      focusCurrentZoneObject();
      return;
    }
    return;
  }

  switch (norm_key) {
    case LV_KEY_LEFT:
      focusPrevZone();
      return;
    case LV_KEY_RIGHT:
      focusNextZone();
      return;
    case LV_KEY_UP:
      if (focus_zone_ == FocusZone::Shortcuts) {
        if (selected_shortcut_ == 0) {
          selected_shortcut_ = kShortcutCount - 1;
        } else {
          selected_shortcut_--;
        }
        refreshShortcutVisuals();
        focusCurrentZoneObject();
      } else if (focus_zone_ == FocusZone::Chat || chat_focused) {
        scrollChatUp();
      }
      return;
    case LV_KEY_DOWN:
      if (focus_zone_ == FocusZone::Shortcuts) {
        selected_shortcut_ = static_cast<uint8_t>((selected_shortcut_ + 1) % kShortcutCount);
        refreshShortcutVisuals();
        focusCurrentZoneObject();
      } else if (focus_zone_ == FocusZone::Chat || chat_focused) {
        scrollChatDown();
      }
      return;
    case LV_KEY_ENTER:
      if (focus_zone_ == FocusZone::Shortcuts) {
        triggerShortcut(selected_shortcut_);
      } else if (focus_zone_ == FocusZone::Selector) {
        if (now - last_selector_action_ms_ < kNavDebounceMs) {
          return;
        }
        last_selector_action_ms_ = now;
        if (channel_dropdown_open_) {
          closeChannelDropdown(false);
        } else {
          openChannelDropdown();
        }
      } else if (focus_zone_ == FocusZone::Chat || chat_focused) {
        openComposeDialog();
      }
      return;
    case 'j':
      if (focus_zone_ == FocusZone::Chat || chat_focused) {
        scrollChatUp();
      }
      return;
    case 'k':
      if (focus_zone_ == FocusZone::Chat || chat_focused) {
        scrollChatDown();
      }
      return;
    case 'c':
      return;
    case 'n':
      selected_shortcut_ = 1;
      setFocusZone(FocusZone::Shortcuts);
      triggerShortcut(1);
      return;
    default:
      if (is_escape && focus_zone_ != FocusZone::Selector) {
        setFocusZone(FocusZone::Selector);
      }
      return;
  }
}

void StandaloneUi::handleClick(lv_obj_t* target) {
  if (cfg_open_) {
    for (uint8_t i = 0; i < kCfgRowCount; i++) {
      if (target == cfg_rows_[i] || target == cfg_row_labels_[i]) {
        cfg_selected_row_ = i;
        if (cfg_selected_row_ != 5) {
          cfg_import_confirm_armed_ = false;
        }
        refreshCfgDialog();
        return;
      }
    }
    if (hasAncestor(target, cfg_dialog_)) {
      return;
    }
  }

  if (compose_open_) {
    if (hasAncestor(target, compose_dialog_) && compose_input_ && key_group_) {
      lv_group_focus_obj(compose_input_);
    }
    return;
  }

  const bool in_selector = hasAncestor(target, channel_selector_btn_);
  const bool in_dropdown = hasAncestor(target, channel_dropdown_panel_);
  int dropdown_row_idx = -1;

  for (uint8_t i = 0; i < configured_channel_count_; i++) {
    if (target == channel_dropdown_rows_[i] || target == channel_dropdown_labels_[i]) {
      dropdown_row_idx = static_cast<int>(i);
      break;
    }
  }

  if (channel_dropdown_open_ && !in_selector && !in_dropdown) {
    closeChannelDropdown(false);
  }

  if (in_selector) {
    const uint32_t now = millis();
    if (now - last_selector_action_ms_ < kNavDebounceMs) {
      return;
    }
    last_selector_action_ms_ = now;
    setFocusZone(FocusZone::Selector);
    if (channel_dropdown_open_) {
      closeChannelDropdown(false);
    } else {
      openChannelDropdown();
    }
    return;
  }

  if (dropdown_row_idx >= 0) {
    dropdown_highlight_channel_ = static_cast<uint8_t>(dropdown_row_idx);
    selectChannel(dropdown_row_idx, true);
    closeChannelDropdown(true);
    return;
  }

  if (hasAncestor(target, chat_panel_)) {
    setFocusZone(FocusZone::Chat);
    return;
  }

  for (uint8_t i = 0; i < kShortcutCount; i++) {
    if (hasAncestor(target, shortcut_btns_[i])) {
      selected_shortcut_ = i;
      setFocusZone(FocusZone::Shortcuts);
      refreshShortcutVisuals();
      triggerShortcut(i);
      return;
    }
  }
}

void StandaloneUi::onFocusableEvent(lv_event_t* event) {
  auto* ui = static_cast<StandaloneUi*>(lv_event_get_user_data(event));
  if (!ui) {
    return;
  }

  lv_obj_t* target = lv_event_get_target(event);
  switch (lv_event_get_code(event)) {
    case LV_EVENT_KEY: {
      const uint32_t event_key = lv_event_get_key(event);
#if PLUMERIA_KEY_DEBUG
      Serial.printf("[KEYUI] LV_EVENT_KEY event_key=%lu target=%p\n", static_cast<unsigned long>(event_key),
                    static_cast<void*>(target));
#endif
      if (event_key != 0) {
        ui->handleKey(event_key);
        break;
      }
      lv_indev_t* indev = lv_indev_get_act();
      if (indev) {
        const uint32_t indev_key = lv_indev_get_key(indev);
#if PLUMERIA_KEY_DEBUG
        Serial.printf("[KEYUI] LV_EVENT_KEY indev_key=%lu\n", static_cast<unsigned long>(indev_key));
#endif
        ui->handleKey(indev_key);
      }
      break;
    }
    case LV_EVENT_FOCUSED:
      if (ui->pending_chat_focus_attempts_ > 0 && target != ui->chat_panel_ && target != ui->compose_input_) {
        break;
      }
      if (target == ui->channel_selector_btn_) {
        ui->focus_zone_ = FocusZone::Selector;
      }
      if (target == ui->chat_panel_) {
        ui->focus_zone_ = FocusZone::Chat;
      }
      if (target == ui->compose_input_) {
        ui->focus_zone_ = FocusZone::Chat;
      }
      for (uint8_t i = 0; i < ui->configured_channel_count_; i++) {
        if (target == ui->channel_dropdown_rows_[i] || target == ui->channel_dropdown_labels_[i]) {
          ui->focus_zone_ = FocusZone::Selector;
          ui->dropdown_highlight_channel_ = i;
          ui->refreshDropdownVisuals();
          break;
        }
      }
      for (uint8_t i = 0; i < kCfgRowCount; i++) {
        if (target == ui->cfg_rows_[i] || target == ui->cfg_row_labels_[i]) {
          ui->cfg_selected_row_ = i;
          if (ui->cfg_selected_row_ != 5) {
            ui->cfg_import_confirm_armed_ = false;
          }
          break;
        }
      }
      for (uint8_t i = 0; i < kShortcutCount; i++) {
        if (target == ui->shortcut_btns_[i] || target == ui->shortcut_labels_[i]) {
          ui->focus_zone_ = FocusZone::Shortcuts;
          ui->selected_shortcut_ = i;
          break;
        }
      }
      ui->refreshShortcutVisuals();
      break;
    case LV_EVENT_CLICKED:
      ui->handleClick(target);
      break;
    default:
      break;
  }
}

void StandaloneUi::loop() {
  if (!started_) {
    return;
  }

  const uint32_t now = millis();
  syncChannelsFromMeshIfNeeded(now);
  refreshClockIfNeeded(now);

  if (chat_history_dirty_ && now - last_chat_persist_ms_ >= kChatPersistFlushMs) {
    saveChatHistoryToFs();
    last_chat_persist_ms_ = now;
  }

  if (pending_chat_focus_attempts_ > 0) {
    lv_obj_t* focused = key_group_ ? lv_group_get_focused(key_group_) : nullptr;
    if (focused == chat_panel_ && focus_zone_ == FocusZone::Chat) {
      pending_chat_focus_attempts_ = 0;
      return;
    }

    setFocusZone(FocusZone::Chat);
    pending_chat_focus_attempts_--;
  }
}

void StandaloneUi::setMeshReady(bool ready) {
  mesh_ready_ = ready;

  if (!started_) {
    return;
  }

  if (ready) {
    saveChatHistoryToFs();
    last_chat_persist_ms_ = millis();
  }

  refreshHeaderVisuals();
}

void StandaloneUi::setWifiState(bool config_server_on, bool sta_connected, bool ap_mode) {
  wifi_config_server_on_ = config_server_on;
  wifi_ok_ = sta_connected;
  wifi_ap_mode_ = ap_mode;

  if (!started_) {
    return;
  }

  refreshHeaderVisuals();
}

void StandaloneUi::applyEvent(const mesh::MeshEvent& event) {
  if (!started_) {
    return;
  }

  if (event.type != mesh::MeshEventType::ChannelMessage) {
    return;
  }

  const int channel_index = findConfiguredChannelIndex(event.channel_name);
  if (channel_index < 0) {
    return;
  }

  if (pending_local_echo_deadline_ms_ != 0) {
    const uint32_t now = millis();
    const bool expired = static_cast<int32_t>(now - pending_local_echo_deadline_ms_) >= 0;
    if (expired) {
      pending_local_echo_deadline_ms_ = 0;
      pending_local_echo_channel_[0] = '\0';
      pending_local_echo_text_[0] = '\0';
    } else if (strcmp(event.channel_name, pending_local_echo_channel_) == 0 &&
               strcmp(event.text, pending_local_echo_text_) == 0) {
      pending_local_echo_deadline_ms_ = 0;
      pending_local_echo_channel_[0] = '\0';
      pending_local_echo_text_[0] = '\0';
      return;
    }
  }

  pushChannelHistoryLine(event.channel_name, event.text, ChatLineKind::Rx);

  if (static_cast<uint8_t>(channel_index) == active_channel_) {
    appendChatLine(event.text, ChatLineKind::Rx);
  } else {
    unread_channels_[channel_index] = true;
    refreshChannelVisuals();
  }
}

}  // namespace ui
}  // namespace plumeria
