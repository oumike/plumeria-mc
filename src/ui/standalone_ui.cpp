#include "ui/standalone_ui.h"

#include <Arduino.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

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

constexpr lv_coord_t kOuterPad = 2;
constexpr lv_coord_t kGap = 2;
constexpr lv_coord_t kMinRailW = 44;
constexpr lv_coord_t kMaxRailW = 60;
constexpr lv_coord_t kRailBottomPad = 6;
constexpr lv_coord_t kMainBottomInset = 4;
constexpr lv_coord_t kHeaderH = 22;
constexpr lv_coord_t kShortcutH = 18;
constexpr lv_coord_t kMsgScrollStep = 12;

const char* kShortcutNames[5] = {
    "DM",
    "CFG",
    "NODES",
    "LIVE",
    "LEGEND",
};

lv_coord_t clampCoord(lv_coord_t value, lv_coord_t low, lv_coord_t high) {
  if (value < low) {
    return low;
  }
  if (value > high) {
    return high;
  }
  return value;
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
  lv_style_set_text_font(&style_msg_rx_, LV_FONT_DEFAULT);

  lv_style_init(&style_msg_tx_);
  lv_style_set_text_color(&style_msg_tx_, kColorTx);
  lv_style_set_text_font(&style_msg_tx_, LV_FONT_DEFAULT);

  lv_style_init(&style_msg_ack_);
  lv_style_set_text_color(&style_msg_ack_, kColorAck);
  lv_style_set_text_font(&style_msg_ack_, LV_FONT_DEFAULT);

  lv_style_init(&style_msg_err_);
  lv_style_set_text_color(&style_msg_err_, kColorErr);
  lv_style_set_text_font(&style_msg_err_, LV_FONT_DEFAULT);

  styles_ready_ = true;
  return true;
}

void StandaloneUi::buildLayout() {
  const lv_coord_t screen_w = lv_disp_get_hor_res(nullptr);
  const lv_coord_t screen_h = lv_disp_get_ver_res(nullptr);

  const lv_coord_t rail_w = clampCoord(screen_w / 7, kMinRailW, kMaxRailW);
  const lv_coord_t rail_h = screen_h - (kOuterPad * 2) - kRailBottomPad;

  const lv_coord_t main_x = kOuterPad + rail_w + kGap;
  const lv_coord_t main_w = screen_w - main_x - kOuterPad;
  const lv_coord_t main_h = screen_h - (kOuterPad * 2);

  const lv_coord_t header_h = clampCoord(kHeaderH + ((screen_h - 170) / 10), 20, 26);
  const lv_coord_t shortcut_h = clampCoord(kShortcutH + ((screen_h - 170) / 18), 16, 22);
  const lv_coord_t chat_y = header_h + kGap;
  const lv_coord_t chat_h = main_h - header_h - shortcut_h - (kGap * 2) - kMainBottomInset;

  root_ = lv_obj_create(lv_scr_act());
  lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
  lv_obj_add_style(root_, &style_root_, 0);
  lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

  rail_panel_ = lv_obj_create(root_);
  lv_obj_set_pos(rail_panel_, kOuterPad, kOuterPad);
  lv_obj_set_size(rail_panel_, rail_w, rail_h);
  lv_obj_add_style(rail_panel_, &style_panel_, 0);
  lv_obj_set_layout(rail_panel_, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(rail_panel_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(rail_panel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(rail_panel_, LV_OBJ_FLAG_SCROLLABLE);

  const lv_coord_t rail_inner_h = rail_h - 6;
  const lv_coord_t ch_gap = 2;
  lv_coord_t ch_h = (rail_inner_h - ((kChannelCount - 1) * ch_gap)) / kChannelCount;
  ch_h = clampCoord(ch_h, 14, 22);

  for (uint8_t i = 0; i < kChannelCount; i++) {
    channel_btns_[i] = lv_btn_create(rail_panel_);
    lv_obj_set_size(channel_btns_[i], rail_w - 6, ch_h);
    lv_obj_add_style(channel_btns_[i], &style_button_, 0);
    lv_obj_add_style(channel_btns_[i], &style_button_focused_, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(channel_btns_[i], onFocusableEvent, LV_EVENT_KEY, this);
    lv_obj_add_event_cb(channel_btns_[i], onFocusableEvent, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(channel_btns_[i], onFocusableEvent, LV_EVENT_FOCUSED, this);

    channel_labels_[i] = lv_label_create(channel_btns_[i]);
    lv_label_set_text(channel_labels_[i], "-");
    lv_obj_add_style(channel_labels_[i], &style_text_main_, 0);
    lv_obj_align(channel_labels_[i], LV_ALIGN_LEFT_MID, 2, 0);

    channel_unread_dots_[i] = lv_obj_create(channel_btns_[i]);
    lv_obj_set_size(channel_unread_dots_[i], 4, 4);
    lv_obj_set_style_radius(channel_unread_dots_[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(channel_unread_dots_[i], LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(channel_unread_dots_[i], kColorUnread, 0);
    lv_obj_set_style_border_width(channel_unread_dots_[i], 0, 0);
    lv_obj_set_style_pad_all(channel_unread_dots_[i], 0, 0);
    lv_obj_align(channel_unread_dots_[i], LV_ALIGN_RIGHT_MID, -2, 0);
    lv_obj_add_flag(channel_unread_dots_[i], LV_OBJ_FLAG_HIDDEN);
  }

  main_panel_ = lv_obj_create(root_);
  lv_obj_set_pos(main_panel_, main_x, kOuterPad);
  lv_obj_set_size(main_panel_, main_w, main_h);
  lv_obj_add_style(main_panel_, &style_panel_, 0);
  lv_obj_clear_flag(main_panel_, LV_OBJ_FLAG_SCROLLABLE);

  header_bar_ = lv_obj_create(main_panel_);
  lv_obj_set_pos(header_bar_, 0, 0);
  lv_obj_set_size(header_bar_, LV_PCT(100), header_h);
  lv_obj_add_style(header_bar_, &style_header_, 0);
  lv_obj_clear_flag(header_bar_, LV_OBJ_FLAG_SCROLLABLE);

  gps_label_ = lv_label_create(header_bar_);
  lv_obj_add_style(gps_label_, &style_text_dim_, 0);
  lv_obj_align(gps_label_, LV_ALIGN_LEFT_MID, 0, 0);

  wifi_label_ = lv_label_create(header_bar_);
  lv_obj_add_style(wifi_label_, &style_text_dim_, 0);
  lv_obj_align_to(wifi_label_, gps_label_, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

  time_label_ = lv_label_create(header_bar_);
  lv_obj_add_style(time_label_, &style_text_main_, 0);
  lv_obj_align(time_label_, LV_ALIGN_CENTER, 0, 0);

  battery_pct_label_ = lv_label_create(header_bar_);
  lv_obj_add_style(battery_pct_label_, &style_text_main_, 0);
  lv_obj_align(battery_pct_label_, LV_ALIGN_RIGHT_MID, -34, 0);

  battery_bar_ = lv_bar_create(header_bar_);
  lv_obj_set_size(battery_bar_, 26, 6);
  lv_obj_align(battery_bar_, LV_ALIGN_RIGHT_MID, -2, 0);
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
  lv_obj_set_layout(chat_panel_, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(chat_panel_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(chat_panel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_scroll_dir(chat_panel_, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(chat_panel_, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(chat_panel_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(chat_panel_, onFocusableEvent, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(chat_panel_, onFocusableEvent, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(chat_panel_, onFocusableEvent, LV_EVENT_FOCUSED, this);

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

  for (uint8_t i = 0; i < configured_channel_count_; i++) {
    lv_group_add_obj(key_group_, channel_btns_[i]);
  }
  lv_group_add_obj(key_group_, chat_panel_);
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

  if (configured_channel_count_ == 0) {
    const char defaults[2][32] = {
        "Public",
        "#rhino",
    };
    setChannels(defaults, 2);
  }

  if (!createStyles()) {
    return false;
  }

  buildLayout();
  bindInputGroup();
  active_channel_ = selected_channel_;
  rebuildChatForActiveChannel();

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
    strncpy(configured_channel_names_[1], "#rhino", sizeof(configured_channel_names_[1]) - 1);
    configured_channel_count_ = 2;
  }

  if (selected_channel_ >= configured_channel_count_) {
    selected_channel_ = 0;
  }
  if (active_channel_ >= configured_channel_count_) {
    active_channel_ = selected_channel_;
  }

  memset(unread_channels_, 0, sizeof(unread_channels_));
  stored_chat_head_ = 0;
  stored_chat_count_ = 0;

  if (started_) {
    refreshChannelVisuals();
    bindInputGroup();
    rebuildChatForActiveChannel();
  }
}

void StandaloneUi::refreshChannelVisuals() {
  for (uint8_t i = 0; i < kChannelCount; i++) {
    if (i < configured_channel_count_) {
      lv_obj_clear_flag(channel_btns_[i], LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(channel_labels_[i], configured_channel_names_[i]);
    } else {
      lv_obj_add_flag(channel_btns_[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(channel_unread_dots_[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    lv_obj_remove_style(channel_btns_[i], &style_button_active_, 0);
    if (i == active_channel_) {
      lv_obj_add_style(channel_btns_[i], &style_button_active_, 0);
    }

    if (unread_channels_[i] && i != active_channel_) {
      lv_obj_clear_flag(channel_unread_dots_[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(channel_unread_dots_[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
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
  lv_label_set_text(gps_label_, gps_ok_ ? "GPS" : "gps");
  lv_label_set_text(wifi_label_, wifi_ok_ ? "WiFi" : "wifi");

  lv_obj_set_style_text_color(gps_label_, gps_ok_ ? kColorTextMain : kColorTextDim, 0);
  lv_obj_set_style_text_color(wifi_label_, wifi_ok_ ? kColorTextMain : kColorTextDim, 0);

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

  const uint32_t now_min = now_ms / 60000UL;
  if (now_min == last_clock_minute_) {
    return;
  }
  last_clock_minute_ = static_cast<uint16_t>(now_min & 0xFFFF);

  const uint8_t hh = static_cast<uint8_t>((now_min / 60UL) % 24UL);
  const uint8_t mm = static_cast<uint8_t>(now_min % 60UL);
  char text[6];
  snprintf(text, sizeof(text), "%02u:%02u", static_cast<unsigned>(hh), static_cast<unsigned>(mm));

  lv_label_set_text(time_label_, text);
}

void StandaloneUi::focusCurrentZoneObject() {
  if (!key_group_) {
    return;
  }

  switch (focus_zone_) {
    case FocusZone::Rail:
      if (configured_channel_count_ == 0) {
        lv_group_focus_obj(chat_panel_);
      } else {
        lv_group_focus_obj(channel_btns_[selected_channel_]);
      }
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
  focus_zone_ = zone;
  focusCurrentZoneObject();
  refreshShortcutVisuals();
}

void StandaloneUi::focusPrevZone() {
  switch (focus_zone_) {
    case FocusZone::Rail:
      setFocusZone(FocusZone::Shortcuts);
      break;
    case FocusZone::Chat:
      setFocusZone(FocusZone::Rail);
      break;
    case FocusZone::Shortcuts:
      setFocusZone(FocusZone::Chat);
      break;
  }
}

void StandaloneUi::focusNextZone() {
  switch (focus_zone_) {
    case FocusZone::Rail:
      setFocusZone(FocusZone::Chat);
      break;
    case FocusZone::Chat:
      setFocusZone(FocusZone::Shortcuts);
      break;
    case FocusZone::Shortcuts:
      setFocusZone(FocusZone::Rail);
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
  if (activate) {
    active_channel_ = selected_channel_;
    unread_channels_[active_channel_] = false;
    rebuildChatForActiveChannel();
  }

  refreshChannelVisuals();
  if (focus_zone_ == FocusZone::Rail) {
    focusCurrentZoneObject();
  }
}

void StandaloneUi::triggerShortcut(uint8_t index) {
  (void)index;
  if (index >= kShortcutCount) {
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
    return;
  }

  strncpy(stored_chat_[stored_chat_head_].channel_name, channel_name,
          sizeof(stored_chat_[stored_chat_head_].channel_name) - 1);
  stored_chat_[stored_chat_head_].channel_name[sizeof(stored_chat_[stored_chat_head_].channel_name) - 1] = '\0';
  strncpy(stored_chat_[stored_chat_head_].text, text, sizeof(stored_chat_[stored_chat_head_].text) - 1);
  stored_chat_[stored_chat_head_].text[sizeof(stored_chat_[stored_chat_head_].text) - 1] = '\0';
  stored_chat_[stored_chat_head_].kind = kind;
  stored_chat_head_ = (stored_chat_head_ + 1) % kMaxStoredChatRows;
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
  switch (key) {
    case LV_KEY_LEFT:
      focusPrevZone();
      return;
    case LV_KEY_RIGHT:
      focusNextZone();
      return;
    case LV_KEY_UP:
      if (focus_zone_ == FocusZone::Rail) {
        selectChannel(static_cast<int>(selected_channel_) - 1, true);
      } else if (focus_zone_ == FocusZone::Shortcuts) {
        if (selected_shortcut_ == 0) {
          selected_shortcut_ = kShortcutCount - 1;
        } else {
          selected_shortcut_--;
        }
        refreshShortcutVisuals();
        focusCurrentZoneObject();
      } else {
        lv_obj_scroll_by(chat_panel_, 0, kMsgScrollStep, LV_ANIM_OFF);
      }
      return;
    case LV_KEY_DOWN:
      if (focus_zone_ == FocusZone::Rail) {
        selectChannel(static_cast<int>(selected_channel_) + 1, true);
      } else if (focus_zone_ == FocusZone::Shortcuts) {
        selected_shortcut_ = static_cast<uint8_t>((selected_shortcut_ + 1) % kShortcutCount);
        refreshShortcutVisuals();
        focusCurrentZoneObject();
      } else {
        lv_obj_scroll_by(chat_panel_, 0, -kMsgScrollStep, LV_ANIM_OFF);
      }
      return;
    case LV_KEY_ENTER:
      if (focus_zone_ == FocusZone::Shortcuts) {
        triggerShortcut(selected_shortcut_);
      } else {
        focusNextZone();
      }
      return;
    default:
      return;
  }
}

void StandaloneUi::handleClick(lv_obj_t* target) {
  if (target == chat_panel_) {
    setFocusZone(FocusZone::Chat);
    return;
  }

  for (uint8_t i = 0; i < configured_channel_count_; i++) {
    if (target == channel_btns_[i]) {
      selected_channel_ = i;
      setFocusZone(FocusZone::Rail);
      selectChannel(i, true);
      return;
    }
  }

  for (uint8_t i = 0; i < kShortcutCount; i++) {
    if (target == shortcut_btns_[i]) {
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
      lv_indev_t* indev = lv_indev_get_act();
      if (indev) {
        ui->handleKey(lv_indev_get_key(indev));
      }
      break;
    }
    case LV_EVENT_FOCUSED:
      if (target == ui->chat_panel_) {
        ui->focus_zone_ = FocusZone::Chat;
      }
      for (uint8_t i = 0; i < ui->configured_channel_count_; i++) {
        if (target == ui->channel_btns_[i]) {
          ui->focus_zone_ = FocusZone::Rail;
          ui->selected_channel_ = i;
          break;
        }
      }
      for (uint8_t i = 0; i < kShortcutCount; i++) {
        if (target == ui->shortcut_btns_[i]) {
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
  refreshClockIfNeeded(now);
}

void StandaloneUi::setMeshReady(bool ready) {
  mesh_ready_ = ready;
  wifi_ok_ = ready;

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
