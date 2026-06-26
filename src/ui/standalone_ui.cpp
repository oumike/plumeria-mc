#include "ui/standalone_ui.h"

#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <esp_vfs_fat.h>
#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "web/web_config.h"

#ifndef PLUMERIA_KEY_DEBUG
#define PLUMERIA_KEY_DEBUG 0
#endif

#ifndef PLUMERIA_CONTACTS_TRACE
#define PLUMERIA_CONTACTS_TRACE 0
#endif

#if PLUMERIA_CONTACTS_TRACE
#define CTS_TRACE(...)                   \
  do {                                   \
    Serial.printf("[CTS] " __VA_ARGS__); \
    Serial.print("\\n");               \
  } while (0)
#else
#define CTS_TRACE(...) \
  do {                 \
  } while (0)
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
const lv_color_t kColorLiveInfo = lv_color_hex(0x7ED6A7);
const lv_color_t kColorLiveDirect = lv_color_hex(0xFFD27A);
const lv_color_t kColorLiveChannel = lv_color_hex(0x89E3FF);
const lv_color_t kColorWifiOn = lv_color_hex(0x59D88E);
const lv_color_t kColorWifiOff = lv_color_hex(0xF56767);
const lv_color_t kColorWifiApBadge = lv_color_hex(0xD8E7F2);

constexpr lv_coord_t kOuterPad = 0;
constexpr lv_coord_t kGap = 2;
#if defined(DEVICE_CARDPUTER_LORA_HAT)
constexpr lv_coord_t kMainBottomInset = 0;
#else
constexpr lv_coord_t kMainBottomInset = 4;
#endif
constexpr lv_coord_t kHeaderH = 30;
#if defined(DEVICE_HELTEC_V4_EXPANSION)
constexpr lv_coord_t kShortcutH = 22;
constexpr lv_coord_t kShortcutMinH = 20;
constexpr lv_coord_t kShortcutMaxH = 26;
constexpr bool kTouchDirectActivate = true;
constexpr lv_coord_t kHeltecMainButtonBottomInset = 6;
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
constexpr lv_coord_t kShortcutH = 14;
constexpr lv_coord_t kShortcutMinH = 12;
constexpr lv_coord_t kShortcutMaxH = 16;
constexpr bool kTouchDirectActivate = false;
#else
constexpr lv_coord_t kShortcutH = 18;
constexpr lv_coord_t kShortcutMinH = 16;
constexpr lv_coord_t kShortcutMaxH = 22;
constexpr bool kTouchDirectActivate = false;
#endif
constexpr lv_coord_t kMsgScrollStep = 12;
constexpr lv_coord_t kSelectorMinW = 60;
constexpr lv_coord_t kSelectorMaxW = 120;
constexpr lv_coord_t kDropdownVisibleRows = 7;
constexpr lv_coord_t kDropdownRowH = 20;
constexpr lv_coord_t kDropdownPanelPadY = 10;
constexpr lv_coord_t kDropdownRightGap = 6;
constexpr lv_coord_t kDropdownScrollbarW = 4;
constexpr lv_coord_t kDropdownHeightSafetyPad = 2;
constexpr size_t kDropdownNameMaxChars = 12;
constexpr lv_coord_t kChannelButtonRadius = 4;
constexpr lv_coord_t kShortcutButtonRadius = 4;
constexpr lv_coord_t kHeaderTimeGap = 8;
constexpr lv_coord_t kHeaderBatteryTextX = -34;
constexpr lv_coord_t kHeaderBatteryBarX = -2;
#if defined(DEVICE_CARDPUTER_LORA_HAT)
constexpr lv_coord_t kHeaderIconsToBatteryGap = 10;
#else
constexpr lv_coord_t kHeaderIconsToBatteryGap = 18;
#endif
constexpr lv_coord_t kHeaderIconsGap = 10;
constexpr lv_coord_t kComposeDialogMinW = 160;
constexpr lv_coord_t kComposeDialogMaxW = 236;
constexpr lv_coord_t kComposeDialogH = 90;
constexpr lv_coord_t kComposeDialogSingleLineH = 78;
constexpr lv_coord_t kComposeInputH = 54;
constexpr lv_coord_t kComposeInputSingleLineH = 24;
constexpr size_t kComposeMessageMaxChars = 90;
constexpr lv_coord_t kContactsDialogMinW = 220;
constexpr lv_coord_t kContactsDialogMinH = 170;
constexpr lv_coord_t kContactsDialogMaxH = 230;
#if defined(DEVICE_CARDPUTER_LORA_HAT)
constexpr lv_coord_t kModalVerticalNudgeY = 4;
#else
constexpr lv_coord_t kModalVerticalNudgeY = 0;
#endif
constexpr uint32_t kNavDebounceMs = 120;
#if defined(DEVICE_TLORA_PAGER_TFT)
constexpr uint32_t kContactsDropdownEnterGuardMs = 800;
#else
constexpr uint32_t kContactsDropdownEnterGuardMs = 260;
#endif
constexpr uint32_t kComposeOpenTapIgnoreMs = 180;
constexpr uint32_t kLocalEchoSuppressMs = 3000;
constexpr uint32_t kChatPersistFlushMs = 2000;
constexpr uint32_t kDmPersistFlushMs = 1000;
constexpr uint32_t kDmRetentionPruneMs = 300000;
constexpr uint32_t kDmRetentionSeconds = 10UL * 24UL * 60UL * 60UL;
constexpr uint32_t kAdvertPopupAutoCloseMs = 2000;
constexpr size_t kDmDialogRecentLimit = 30;
constexpr uint32_t kChannelSyncMs = 1000;
constexpr time_t kTimeValidEpoch = 1700000000;
constexpr char kUiPrefsNs[] = "ui_state";
constexpr char kChatHistoryBlobKey[] = "chat_blob";
constexpr char kChatHistoryCountKey[] = "chat_count";
constexpr char kDmHistoryBlobKey[] = "dm_blob";
constexpr char kDmHistoryCountKey[] = "dm_count";
constexpr char kCfgSdDir[] = "/plumeria";
constexpr char kCfgSdPath[] = "/plumeria/plumeria-config.yaml";
constexpr char kCfgSdPathFallback[] = "/plumeria-config.yaml";
constexpr uint32_t kCfgSdClockHz = 800000UL;

#if defined(DEVICE_TLORA_PAGER_TFT)
constexpr bool kPagerWideDialogLayout = true;
#else
constexpr bool kPagerWideDialogLayout = false;
#endif

#if defined(DEVICE_HELTEC_V4_EXPANSION)
constexpr bool kUseOnscreenKeyboard = true;
#else
constexpr bool kUseOnscreenKeyboard = false;
#endif

#if defined(DEVICE_CARDPUTER_LORA_HAT) || (defined(DEVICE_HELTEC_V4_EXPANSION) && defined(DEVICE_UI_VERTICAL))
constexpr bool kCompactFavoriteActionLabels = true;
#else
constexpr bool kCompactFavoriteActionLabels = false;
#endif

constexpr bool kEnableContactsDialog = true;

lv_coord_t dialogMaxW(lv_coord_t regular_max, lv_coord_t pager_max) {
  return kPagerWideDialogLayout ? pager_max : regular_max;
}

lv_coord_t dialogInsetW(lv_coord_t regular_inset, lv_coord_t pager_inset) {
  return kPagerWideDialogLayout ? pager_inset : regular_inset;
}

lv_coord_t composeHintWidthPct() {
  return kPagerWideDialogLayout ? 98 : 96;
}

lv_coord_t composeInputWidthPct() {
  return kPagerWideDialogLayout ? 96 : 92;
}

lv_coord_t composeInputBottomInset() {
  return kPagerWideDialogLayout ? -6 : -8;
}

lv_coord_t contactsLeftPanePercent() {
  return kPagerWideDialogLayout ? 37 : 44;
}

const char* addFavoriteActionLabel() {
  return kCompactFavoriteActionLabels ? "Add Fav" : "Add (F)avorite";
}

const char* favoriteActionLabel(bool currently_favorite) {
  if (kCompactFavoriteActionLabels) {
    return currently_favorite ? "Rem Fav" : "Add Fav";
  }
  return currently_favorite ? "Remove (F)avorite" : "Add (F)avorite";
}

uint8_t clampOptionCount(uint8_t count, uint8_t limit) {
  return count > limit ? limit : count;
}

#if defined(DEVICE_HELTEC_V4_EXPANSION)
constexpr bool kKeyboardNavEnabled = false;
#else
constexpr bool kKeyboardNavEnabled = true;
#endif

#if defined(DEVICE_CARDPUTER_LORA_HAT)
uint32_t remapCardputerNavKey(uint32_t key, bool allow_nav_remap) {
  if (key == '`' || key == '~') {
    return LV_KEY_ESC;
  }
  if (!allow_nav_remap) {
    return key;
  }

  switch (key) {
    case ';':
      return LV_KEY_UP;
    case '.':
      return LV_KEY_DOWN;
    case ',':
      return LV_KEY_LEFT;
    case '/':
      return LV_KEY_RIGHT;
    default:
      return key;
  }
}
#endif

#if defined(DEVICE_HELTEC_V4_EXPANSION) && defined(HELTEC_COMPACT_SELECTOR)
const char* kShortcutNames[] = {
  "CFG",
  "CONT",
  "LIVE",
  "HELP",
};
#elif defined(DEVICE_HELTEC_V4_EXPANSION)
const char* kShortcutNames[] = {
  "CFG",
  "CONTACTS",
  "LIVE",
  "HELP",
};
#elif defined(DEVICE_CARDPUTER_LORA_HAT)
const char* kShortcutNames[] = {
  "(C)FG",
  "C(O)NT",
  "(L)IVE",
  "(H)ELP",
};
#else
const char* kShortcutNames[] = {
  "(C)FG",
  "C(O)NTACTS",
  "(L)IVE",
  "(H)ELP",
};
#endif

constexpr uint8_t kShortcutCfg = 0;
constexpr uint8_t kShortcutContacts = 1;
constexpr uint8_t kShortcutLive = 2;
constexpr uint8_t kShortcutHelp = 3;

#if defined(DEVICE_HELTEC_V4_EXPANSION)
const char* kHelpBodyText =
  "Touch guide:\n"
  "CFG: Device settings and web config\n"
  "CONTACTS: Favorites and direct messages\n"
  "LIVE: Mesh traffic feed\n"
  "HELP: Open this screen\n"
  "ADVZ: Send one-hop presence advert\n"
  "ADVF: Send flood advert across mesh\n"
  "NEW: Start a room message or DM\n"
  "CLOSE: Return to chat";
#else
const char* kHelpBodyText =
  "Keyboard shortcuts:\n"
  "h = Help (global except compose)\n"
  "l = Live feed\n"
  "c = Config\n"
  "o = Contacts\n"
  "m = Compose to current room\n"
  "z = Advert zero-hop\n"
  "f = Advert flood\n"
  "d = DM from Contacts\n"
  "j = up, k = down (arrows also work)\n"
  "Backspace = close current dialog";
#endif

const char* kCfgRowLabels[] = {
  "Node Name",
  "Radio Preset",
  "Web Config",
  "GPS",
  "Multipaths",
  "Mesh Region",
#if !defined(DEVICE_HELTEC_V4_EXPANSION)
  "Export Config",
  "Import Config",
  "Delete Config",
#endif
};

constexpr uint8_t kCfgRowNodeName = 0;
constexpr uint8_t kCfgRowRadioPreset = 1;
constexpr uint8_t kCfgRowWebConfig = 2;
constexpr uint8_t kCfgRowGps = 3;
constexpr uint8_t kCfgRowMultipaths = 4;
constexpr uint8_t kCfgRowMeshRegion = 5;
#if !defined(DEVICE_HELTEC_V4_EXPANSION)
constexpr uint8_t kCfgRowExportConfig = 6;
constexpr uint8_t kCfgRowImportConfig = 7;
constexpr uint8_t kCfgRowDeleteConfig = 8;
#endif

const char* radioPresetDisplayName(const char* region) {
  if (!region || region[0] == '\0') {
    return "-";
  }
  if (strcmp(region, "US") == 0) {
    return "US/Canada";
  }
  return region;
}

void formatChannelLabelForDropdown(const char* channel_name, char* out_text, size_t out_size) {
  if (!out_text || out_size == 0) {
    return;
  }

  out_text[0] = '\0';
  if (!channel_name || channel_name[0] == '\0') {
    strncpy(out_text, "-", out_size - 1);
    out_text[out_size - 1] = '\0';
    return;
  }

  const size_t len = strlen(channel_name);
  if (len <= kDropdownNameMaxChars) {
    strncpy(out_text, channel_name, out_size - 1);
    out_text[out_size - 1] = '\0';
    return;
  }

  snprintf(out_text, out_size, "%.*s...", static_cast<int>(kDropdownNameMaxChars), channel_name);
}

size_t channelDisplayLenForDropdown(const char* channel_name) {
  if (!channel_name || channel_name[0] == '\0') {
    return 1;
  }
  const size_t len = strlen(channel_name);
  return len <= kDropdownNameMaxChars ? len : (kDropdownNameMaxChars + 3);
}

void formatChannelLabelForSelector(const char* channel_name, size_t selector_char_cap, char* out_text,
                                   size_t out_size) {
  if (!out_text || out_size == 0) {
    return;
  }

  out_text[0] = '\0';
  if (!channel_name || channel_name[0] == '\0') {
    strncpy(out_text, "-", out_size - 1);
    out_text[out_size - 1] = '\0';
    return;
  }

  const size_t len = strlen(channel_name);
  if (len <= selector_char_cap || selector_char_cap == 0) {
    strncpy(out_text, channel_name, out_size - 1);
    out_text[out_size - 1] = '\0';
    return;
  }

  if (selector_char_cap <= 3) {
    size_t dots = selector_char_cap;
    if (dots >= out_size) {
      dots = out_size - 1;
    }
    for (size_t i = 0; i < dots; i++) {
      out_text[i] = '.';
    }
    out_text[dots] = '\0';
    return;
  }

  snprintf(out_text, out_size, "%.*s...", static_cast<int>(selector_char_cap - 3), channel_name);
}

bool contactSortBefore(const mesh::MeshContactSummary& a, const mesh::MeshContactSummary& b) {
  if (a.favorite != b.favorite) {
    return a.favorite && !b.favorite;
  }
  return strcmp(a.name, b.name) < 0;
}

double contactCoordToDouble(int32_t coord_i) {
  return static_cast<double>(coord_i) / 1e7;
}

bool contactHasCoord(int32_t lat_i, int32_t lon_i) {
  if (lat_i == 0 && lon_i == 0) {
    return false;
  }
  const double lat = contactCoordToDouble(lat_i);
  const double lon = contactCoordToDouble(lon_i);
  return lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
}

char asciiLower(char c) {
  if (c >= 'A' && c <= 'Z') {
    return static_cast<char>(c - 'A' + 'a');
  }
  return c;
}

char asciiUpper(char c) {
  if (c >= 'a' && c <= 'z') {
    return static_cast<char>(c - 'a' + 'A');
  }
  return c;
}

bool dmNameLikelyMatch(const char* a, const char* b) {
  if (!a || !b) {
    return false;
  }

  size_t len_a = strlen(a);
  size_t len_b = strlen(b);
  while (len_a > 0 && a[len_a - 1] == ' ') {
    len_a--;
  }
  while (len_b > 0 && b[len_b - 1] == ' ') {
    len_b--;
  }
  if (len_a == 0 || len_b == 0) {
    return false;
  }

  const size_t min_len = len_a < len_b ? len_a : len_b;
  for (size_t i = 0; i < min_len; i++) {
    if (asciiLower(a[i]) != asciiLower(b[i])) {
      return false;
    }
  }

  if (len_a == len_b) {
    return true;
  }

  // Names may differ only by truncation to fixed-size contact buffers.
  return len_a >= 31 || len_b >= 31;
}

int findContactIndexByIdentity(const mesh::MeshContactSummary* contacts, uint8_t count, const char* key,
                               const char* name) {
  if (!contacts || count == 0) {
    return -1;
  }

  const bool has_key = key && key[0] != '\0';
  if (has_key) {
    for (uint8_t i = 0; i < count; i++) {
      if (contacts[i].public_key_hex[0] != '\0' && strcmp(contacts[i].public_key_hex, key) == 0) {
        return static_cast<int>(i);
      }
    }
  }

  if (name && name[0] != '\0') {
    for (uint8_t i = 0; i < count; i++) {
      if (dmNameLikelyMatch(contacts[i].name, name)) {
        return static_cast<int>(i);
      }
    }
  }

  return -1;
}

void abbreviateContactName(const char* name, char* out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }

  out[0] = '\0';
  if (!name || name[0] == '\0') {
    strncpy(out, "?", out_size - 1);
    out[out_size - 1] = '\0';
    return;
  }

  bool start_of_word = true;
  size_t write = 0;
  for (size_t i = 0; name[i] != '\0' && write + 1 < out_size; i++) {
    const char c = name[i];
    const bool is_alnum = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
    if (is_alnum && start_of_word) {
      out[write++] = asciiUpper(c);
      start_of_word = false;
    } else if (!is_alnum) {
      start_of_word = true;
    }
  }

  if (write == 0) {
    strncpy(out, "?", out_size - 1);
    out[out_size - 1] = '\0';
    return;
  }

  out[write] = '\0';
}

void formatEpochClockHhMm(uint32_t epoch_seconds, char* out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }

  out[0] = '\0';
  if (epoch_seconds != 0) {
    const time_t event_time = static_cast<time_t>(epoch_seconds);
    struct tm tm_event{};
    if (localtime_r(&event_time, &tm_event)) {
      snprintf(out, out_size, "%02u:%02u", static_cast<unsigned>(tm_event.tm_hour),
               static_cast<unsigned>(tm_event.tm_min));
      return;
    }
  }

  uint8_t hh = 0;
  uint8_t mm = 0;
  const time_t now_time = time(nullptr);
  if (now_time >= kTimeValidEpoch) {
    struct tm tm_now{};
    localtime_r(&now_time, &tm_now);
    hh = static_cast<uint8_t>(tm_now.tm_hour);
    mm = static_cast<uint8_t>(tm_now.tm_min);
  } else {
    const uint32_t now_min = millis() / 60000UL;
    hh = static_cast<uint8_t>((now_min / 60UL) % 24UL);
    mm = static_cast<uint8_t>(now_min % 60UL);
  }
  snprintf(out, out_size, "%02u:%02u", static_cast<unsigned>(hh), static_cast<unsigned>(mm));
}

bool dmLineHasTimestampPrefix(const char* text) {
  if (!text || text[0] != '[') {
    return false;
  }
  for (size_t i = 1; i < 8 && text[i] != '\0'; i++) {
    if (text[i] == ']') {
      return true;
    }
  }
  return false;
}

void formatDmDisplayLine(const char* line_text, uint32_t timestamp_epoch, char* out_text, size_t out_size) {
  if (!out_text || out_size == 0) {
    return;
  }

  out_text[0] = '\0';
  if (!line_text || line_text[0] == '\0') {
    return;
  }

  if (dmLineHasTimestampPrefix(line_text)) {
    strncpy(out_text, line_text, out_size - 1);
    out_text[out_size - 1] = '\0';
    return;
  }

  char hhmm[8] = {};
  formatEpochClockHhMm(timestamp_epoch, hhmm, sizeof(hhmm));
  snprintf(out_text, out_size, "[%s] %s", hhmm, line_text);
}

bool epochToLocalTm(uint32_t epoch_seconds, struct tm* out_tm) {
  if (!out_tm || epoch_seconds == 0) {
    return false;
  }

  const time_t event_time = static_cast<time_t>(epoch_seconds);
  return localtime_r(&event_time, out_tm) != nullptr;
}

uint32_t dateKeyFromEpoch(uint32_t epoch_seconds) {
  struct tm tm_event{};
  if (!epochToLocalTm(epoch_seconds, &tm_event)) {
    return 0;
  }

  const uint32_t year = static_cast<uint32_t>(tm_event.tm_year + 1900);
  const uint32_t month = static_cast<uint32_t>(tm_event.tm_mon + 1);
  const uint32_t day = static_cast<uint32_t>(tm_event.tm_mday);
  return year * 10000UL + month * 100UL + day;
}

const char* dayOrdinalSuffix(uint8_t day) {
  const uint8_t mod100 = static_cast<uint8_t>(day % 100U);
  if (mod100 >= 11U && mod100 <= 13U) {
    return "th";
  }

  switch (day % 10U) {
    case 1:
      return "st";
    case 2:
      return "nd";
    case 3:
      return "rd";
    default:
      return "th";
  }
}

void formatEpochDateMarker(uint32_t epoch_seconds, char* out_text, size_t out_size) {
  if (!out_text || out_size == 0) {
    return;
  }

  out_text[0] = '\0';
  struct tm tm_event{};
  if (!epochToLocalTm(epoch_seconds, &tm_event)) {
    return;
  }

  static const char* kMonthNames[12] = {
      "January", "February", "March",     "April",   "May",      "June",
      "July",    "August",   "September", "October", "November", "December",
  };

  const uint8_t month_idx = static_cast<uint8_t>(tm_event.tm_mon);
  if (month_idx >= 12) {
    return;
  }

  const uint8_t day = static_cast<uint8_t>(tm_event.tm_mday);
  const unsigned year = static_cast<unsigned>(tm_event.tm_year + 1900);
  snprintf(out_text, out_size, "%s %u%s %u", kMonthNames[month_idx], static_cast<unsigned>(day),
           dayOrdinalSuffix(day), year);
}

lv_obj_t* createDateMarkerRow(lv_obj_t* panel, lv_coord_t row_w, uint32_t epoch_seconds, const lv_font_t* font) {
  if (!panel) {
    return nullptr;
  }

  char date_text[48] = {};
  formatEpochDateMarker(epoch_seconds, date_text, sizeof(date_text));
  if (date_text[0] == '\0') {
    return nullptr;
  }

  lv_obj_t* row = lv_obj_create(panel);
  lv_obj_remove_style_all(row);
  lv_obj_set_width(row, row_w > 0 ? row_w : LV_PCT(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_top(row, 3, 0);
  lv_obj_set_style_pad_bottom(row, 2, 0);
  lv_obj_set_style_pad_column(row, 4, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_layout(row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t* left_line = lv_obj_create(row);
  lv_obj_remove_style_all(left_line);
  lv_obj_set_flex_grow(left_line, 1);
  lv_obj_set_height(left_line, 1);
  lv_obj_set_style_bg_color(left_line, kColorBorder, 0);
  lv_obj_set_style_bg_opa(left_line, LV_OPA_70, 0);

  lv_obj_t* marker = lv_label_create(row);
  if (font) {
    lv_obj_set_style_text_font(marker, font, 0);
  }
  lv_obj_set_style_text_color(marker, kColorTextDim, 0);
  lv_label_set_text(marker, date_text);

  lv_obj_t* right_line = lv_obj_create(row);
  lv_obj_remove_style_all(right_line);
  lv_obj_set_flex_grow(right_line, 1);
  lv_obj_set_height(right_line, 1);
  lv_obj_set_style_bg_color(right_line, kColorBorder, 0);
  lv_obj_set_style_bg_opa(right_line, LV_OPA_70, 0);

  return row;
}

void formatContactLastHeard(uint32_t lastmod, char* out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }
  if (lastmod == 0) {
    strncpy(out, "Last Heard: -", out_size - 1);
    out[out_size - 1] = '\0';
    return;
  }

  const time_t ts = static_cast<time_t>(lastmod);
  struct tm tm_last{};
  if (!localtime_r(&ts, &tm_last)) {
    strncpy(out, "Last Heard: -", out_size - 1);
    out[out_size - 1] = '\0';
    return;
  }

  snprintf(out, out_size, "Last Heard: %02d/%02d/%04d %02d:%02d", tm_last.tm_mon + 1, tm_last.tm_mday,
           tm_last.tm_year + 1900, tm_last.tm_hour, tm_last.tm_min);
}

const char* contactAdvertTypeLabel(uint8_t type) {
  switch (type) {
    case 1:
      return "chat";
    case 2:
      return "repeater";
    case 3:
      return "room";
    case 4:
      return "sensor";
    default:
      return "node";
  }
}

void appendTelemetryToken(char* out, size_t out_size, const char* token) {
  if (!out || out_size == 0 || !token || token[0] == '\0') {
    return;
  }

  if (out[0] == '\0') {
    strncpy(out, token, out_size - 1);
    out[out_size - 1] = '\0';
    return;
  }

  const size_t used = strlen(out);
  if (used + 3 >= out_size) {
    return;
  }
  snprintf(out + used, out_size - used, " | %s", token);
}

void formatContactTelemetry(const mesh::MeshContactSummary& contact, char* out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }

  if (contact.telemetry_feat1 == 0 && contact.telemetry_feat2 == 0) {
    snprintf(out, out_size, "Telemetry: -");
    return;
  }

  char detail[96] = {};
  char token[32] = {};

  if (contact.telemetry_feat1 > 0) {
    if (contact.telemetry_feat1 >= 2500 && contact.telemetry_feat1 <= 5200) {
      snprintf(token, sizeof(token), "Batt %.2fV", contact.telemetry_feat1 / 1000.0f);
    } else if (contact.telemetry_feat1 <= 100) {
      snprintf(token, sizeof(token), "Batt %u%%", static_cast<unsigned>(contact.telemetry_feat1));
    } else {
      snprintf(token, sizeof(token), "F1 %u", static_cast<unsigned>(contact.telemetry_feat1));
    }
    appendTelemetryToken(detail, sizeof(detail), token);
  }

  if (contact.telemetry_feat2 > 0) {
    if (contact.telemetry_feat2 <= 100) {
      snprintf(token, sizeof(token), "Env %u%%", static_cast<unsigned>(contact.telemetry_feat2));
    } else {
      snprintf(token, sizeof(token), "F2 %u", static_cast<unsigned>(contact.telemetry_feat2));
    }
    appendTelemetryToken(detail, sizeof(detail), token);
  }

  if (detail[0] == '\0') {
    snprintf(out, out_size, "Telemetry: %s", contactAdvertTypeLabel(contact.telemetry_adv_type));
    return;
  }

  snprintf(out, out_size, "Telemetry (%s): %s", contactAdvertTypeLabel(contact.telemetry_adv_type), detail);
}

uint32_t nowEpochSecondsOrZero() {
  time_t now = time(nullptr);
  if (now < kTimeValidEpoch) {
    return 0;
  }
  return static_cast<uint32_t>(now);
}

void formatUiClockHhMm(char* out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }

  uint8_t hh = 0;
  uint8_t mm = 0;
  const time_t now_time = time(nullptr);
  if (now_time >= kTimeValidEpoch) {
    struct tm tm_now{};
    localtime_r(&now_time, &tm_now);
    hh = static_cast<uint8_t>(tm_now.tm_hour);
    mm = static_cast<uint8_t>(tm_now.tm_min);
  } else {
    const uint32_t now_min = millis() / 60000UL;
    hh = static_cast<uint8_t>((now_min / 60UL) % 24UL);
    mm = static_cast<uint8_t>(now_min % 60UL);
  }

  snprintf(out, out_size, "%02u:%02u", static_cast<unsigned>(hh), static_cast<unsigned>(mm));
}

void buildDebugTestMessage(char* out, size_t out_size) {
  if (!out || out_size == 0) {
    return;
  }

  static const char* kWords[] = {
      "alpha", "beacon", "mesh", "signal", "packet", "uplink", "relay", "channel", "vector",
      "delta", "echo", "foxtrot", "lattice", "comms", "status", "random", "verify", "format",
      "payload", "horizon", "network", "cipher", "radio", "bridge", "node", "sample", "monitor",
  };

  out[0] = '\0';
  size_t used = 0;
  const size_t word_count = sizeof(kWords) / sizeof(kWords[0]);

  // Fill up to max allowed length with random words separated by spaces.
  for (size_t guard = 0; guard < 512 && used + 1 < out_size; guard++) {
    const char* word = kWords[random(static_cast<long>(word_count))];
    const size_t wlen = strlen(word);
    const size_t needed = wlen + (used > 0 ? 1 : 0);
    if (used + needed >= out_size) {
      break;
    }

    if (used > 0) {
      out[used++] = ' ';
    }
    memcpy(out + used, word, wlen);
    used += wlen;
    out[used] = '\0';
  }

  if (used == 0) {
    strncpy(out, "test", out_size - 1);
    out[out_size - 1] = '\0';
  }
}

struct PersistedChatLineLegacy {
  char channel_name[32];
  char text[96];
  uint8_t kind;
};

struct PersistedChatLine {
  char channel_name[32];
  char text[96];
  uint8_t kind;
  uint32_t timestamp_epoch;
};

struct PersistedDmLine {
  char contact_name[32];
  char contact_key[65];
  char text[96];
  uint8_t kind;
  uint32_t timestamp_epoch;
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

void clearSdVfsRegistration() {
  // Arduino SD defaults to "/sd" in this core, but some builds may use "/sdcard".
  // Ignore return values: unregistering a non-registered path is a no-op.
  (void)esp_vfs_fat_unregister_path("/sd");
  (void)esp_vfs_fat_unregister_path("/sdcard");
}

#if defined(DEVICE_TLORA_PAGER_TFT)
constexpr uint8_t kPagerXl9555RegOut0 = 0x02;
constexpr uint8_t kPagerXl9555RegOut1 = 0x03;
constexpr uint8_t kPagerXl9555RegCfg0 = 0x06;
constexpr uint8_t kPagerXl9555RegCfg1 = 0x07;
constexpr uint8_t kPagerExpSdDet = 10;
constexpr uint8_t kPagerExpSdPullen = 11;
constexpr uint8_t kPagerExpSdEn = 12;

enum class PagerSdPullenMode : uint8_t {
  kInput = 0,
  kLow = 1,
  kHigh = 2,
};

struct PagerSdProfile {
  bool invert_dir_sense;
  bool sd_en_high;
  PagerSdPullenMode pullen_mode;
};

bool pagerXl9555WriteReg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

bool pagerXl9555ReadReg(uint8_t addr, uint8_t reg, uint8_t* val) {
  if (!val) {
    return false;
  }
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(static_cast<int>(addr), 1) != 1) {
    return false;
  }
  *val = static_cast<uint8_t>(Wire.read());
  return true;
}

void pagerXl9555SetOutput(uint8_t pin, bool level, bool invert_dir_sense,
                          uint8_t* out0, uint8_t* out1,
                          uint8_t* cfg0, uint8_t* cfg1) {
  if (!out0 || !out1 || !cfg0 || !cfg1) {
    return;
  }
  const uint8_t bit = static_cast<uint8_t>(1U << (pin & 0x07));
  if (pin < 8) {
    if (invert_dir_sense) {
      *cfg0 |= bit;
    } else {
      *cfg0 &= static_cast<uint8_t>(~bit);
    }
    if (level) {
      *out0 |= bit;
    } else {
      *out0 &= static_cast<uint8_t>(~bit);
    }
  } else {
    if (invert_dir_sense) {
      *cfg1 |= bit;
    } else {
      *cfg1 &= static_cast<uint8_t>(~bit);
    }
    if (level) {
      *out1 |= bit;
    } else {
      *out1 &= static_cast<uint8_t>(~bit);
    }
  }
}

void pagerXl9555SetInput(uint8_t pin, bool invert_dir_sense, uint8_t* cfg0, uint8_t* cfg1) {
  if (!cfg0 || !cfg1) {
    return;
  }
  const uint8_t bit = static_cast<uint8_t>(1U << (pin & 0x07));
  if (pin < 8) {
    if (invert_dir_sense) {
      *cfg0 &= static_cast<uint8_t>(~bit);
    } else {
      *cfg0 |= bit;
    }
  } else {
    if (invert_dir_sense) {
      *cfg1 &= static_cast<uint8_t>(~bit);
    } else {
      *cfg1 |= bit;
    }
  }
}

bool pagerGetSdProfile(int profile, PagerSdProfile* out_profile) {
  if (!out_profile) {
    return false;
  }
  switch (profile) {
    case 0:
      *out_profile = {false, true, PagerSdPullenMode::kInput};
      return true;
    case 1:
      *out_profile = {false, false, PagerSdPullenMode::kInput};
      return true;
    case 2:
      *out_profile = {false, true, PagerSdPullenMode::kLow};
      return true;
    case 3:
      *out_profile = {true, true, PagerSdPullenMode::kInput};
      return true;
    case 4:
      *out_profile = {true, false, PagerSdPullenMode::kInput};
      return true;
    default:
      return false;
  }
}

bool pagerPrepareSdPower(int profile, char* out_err, size_t out_err_size) {
  PagerSdProfile cfg{};
  if (!pagerGetSdProfile(profile, &cfg)) {
    setErrText(out_err, out_err_size, "Invalid SD profile");
    return false;
  }

  Wire.begin(3, 2, 100000UL);

  uint8_t exp_addr = 0xFF;
  for (uint8_t a = 0x20; a <= 0x27; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      exp_addr = a;
      break;
    }
  }

  if (exp_addr == 0xFF) {
    setErrText(out_err, out_err_size, "SD expander not found");
    return false;
  }

  uint8_t out0 = 0xFF;
  uint8_t out1 = 0xFF;
  uint8_t cfg0 = 0xFF;
  uint8_t cfg1 = 0xFF;
  (void)pagerXl9555ReadReg(exp_addr, kPagerXl9555RegOut0, &out0);
  (void)pagerXl9555ReadReg(exp_addr, kPagerXl9555RegOut1, &out1);
  (void)pagerXl9555ReadReg(exp_addr, kPagerXl9555RegCfg0, &cfg0);
  (void)pagerXl9555ReadReg(exp_addr, kPagerXl9555RegCfg1, &cfg1);

  // Keep Pager SD probing scoped to SD-specific expander lines only.
  pagerXl9555SetOutput(kPagerExpSdEn, cfg.sd_en_high, cfg.invert_dir_sense, &out0, &out1, &cfg0, &cfg1);
  pagerXl9555SetInput(kPagerExpSdDet, cfg.invert_dir_sense, &cfg0, &cfg1);
  if (cfg.pullen_mode == PagerSdPullenMode::kInput) {
    pagerXl9555SetInput(kPagerExpSdPullen, cfg.invert_dir_sense, &cfg0, &cfg1);
  } else {
    pagerXl9555SetOutput(kPagerExpSdPullen, cfg.pullen_mode == PagerSdPullenMode::kHigh,
                         cfg.invert_dir_sense, &out0, &out1, &cfg0, &cfg1);
  }

  const bool ok = pagerXl9555WriteReg(exp_addr, kPagerXl9555RegOut0, out0) &&
                  pagerXl9555WriteReg(exp_addr, kPagerXl9555RegOut1, out1) &&
                  pagerXl9555WriteReg(exp_addr, kPagerXl9555RegCfg0, cfg0) &&
                  pagerXl9555WriteReg(exp_addr, kPagerXl9555RegCfg1, cfg1);
  if (!ok) {
    setErrText(out_err, out_err_size, "SD expander write failed");
    return false;
  }

  setErrText(out_err, out_err_size, "");
  return true;
}
#endif

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

  // Avoid duplicate VFS registration when SD is already mounted.
  if (SD.cardType() != CARD_NONE) {
    setErrText(out_err, out_err_size, "");
    return true;
  }

  SD.end();
  clearSdVfsRegistration();
  SPI.end();
  delay(6);
  SPI.begin(sd_sck, sd_miso, sd_mosi);

  static const char* kMountpoints[] = {"/sd", "/sdcard"};

#if defined(DEVICE_TLORA_PAGER_TFT)
  static const int kProfiles[] = {3, 2, 0, 1, 4};
  static const uint32_t kSpeeds[] = {4000000UL, 1000000UL, 400000UL};
  for (size_t pi = 0; pi < (sizeof(kProfiles) / sizeof(kProfiles[0])); pi++) {
    if (!pagerPrepareSdPower(kProfiles[pi], out_err, out_err_size)) {
      continue;
    }
    delay(12);
    for (size_t si = 0; si < (sizeof(kSpeeds) / sizeof(kSpeeds[0])); si++) {
      SD.end();
      clearSdVfsRegistration();
      SPI.end();
      delay(6);
      SPI.begin(sd_sck, sd_miso, sd_mosi);
      Serial.printf("[SD] try profile=%d speed=%lu\n", kProfiles[pi],
                    static_cast<unsigned long>(kSpeeds[si]));

      bool mounted = false;
      for (size_t mi = 0; mi < (sizeof(kMountpoints) / sizeof(kMountpoints[0])); mi++) {
        Serial.printf("[SD] try mountpoint=%s free_heap=%lu\n",
                      kMountpoints[mi],
                      static_cast<unsigned long>(ESP.getFreeHeap()));
        // max_files=2 trims FATFS context alloc (vfs_fat_ctx_t + N*FIL) on
        // internal heap; SDK CONFIG_FATFS_ALLOC_PREFER_EXTRAM is moot without PSRAM.
        if (SD.begin(sd_cs, SPI, kSpeeds[si], kMountpoints[mi], 2)) {
          mounted = true;
          break;
        }
        SD.end();
        clearSdVfsRegistration();
        delay(2);
      }

      if (!mounted) {
        SPI.end();
        delay(6);
        continue;
      }
      if (SD.cardType() == CARD_NONE) {
        SD.end();
        clearSdVfsRegistration();
        SPI.end();
        delay(6);
        continue;
      }
      Serial.printf("[SD] mounted profile=%d speed=%lu\n", kProfiles[pi],
                    static_cast<unsigned long>(kSpeeds[si]));
      setErrText(out_err, out_err_size, "");
      return true;
    }
    delay(8);
  }
  SD.end();
  clearSdVfsRegistration();
  SPI.end();
  delay(6);
  setErrText(out_err, out_err_size, "SD mount failed");
  return false;
#else
  bool mounted = false;
  for (size_t mi = 0; mi < (sizeof(kMountpoints) / sizeof(kMountpoints[0])); mi++) {
    if (SD.begin(sd_cs, SPI, kCfgSdClockHz, kMountpoints[mi], 2)) {
      mounted = true;
      break;
    }
    SD.end();
    clearSdVfsRegistration();
    delay(2);
  }

  if (!mounted) {
    // Recover from stale mount state by tearing down and retrying once.
    SD.end();
    clearSdVfsRegistration();
    SPI.end();
    delay(6);
    SPI.begin(sd_sck, sd_miso, sd_mosi);

    for (size_t mi = 0; mi < (sizeof(kMountpoints) / sizeof(kMountpoints[0])); mi++) {
      if (SD.begin(sd_cs, SPI, kCfgSdClockHz, kMountpoints[mi], 2)) {
        mounted = true;
        break;
      }
      SD.end();
      clearSdVfsRegistration();
      delay(2);
    }

    if (!mounted) {
      SD.end();
      clearSdVfsRegistration();
      SPI.end();
      delay(6);
      setErrText(out_err, out_err_size, "SD mount failed");
      return false;
    }
  }
#endif

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

int clampInt(int value, int low, int high) {
  if (value < low) {
    return low;
  }
  if (value > high) {
    return high;
  }
  return value;
}

bool parseRadioInfoSample(const char* text, int16_t* out_snr_db, int16_t* out_rssi_dbm) {
  if (!text || !out_snr_db || !out_rssi_dbm) {
    return false;
  }
  float snr = 0.0f;
  float rssi = 0.0f;
  if (sscanf(text, "RADIO snr=%f rssi=%f", &snr, &rssi) != 2) {
    return false;
  }
  const int snr_i = static_cast<int>(snr >= 0.0f ? snr + 0.5f : snr - 0.5f);
  const int rssi_i = static_cast<int>(rssi >= 0.0f ? rssi + 0.5f : rssi - 0.5f);
  *out_snr_db = static_cast<int16_t>(clampInt(snr_i, -40, 20));
  *out_rssi_dbm = static_cast<int16_t>(clampInt(rssi_i, -130, 0));
  return true;
}

lv_color_t liveFeedTextColor(const char* text, bool is_error) {
  if (is_error) {
    return kColorErr;
  }
  if (!text) {
    return kColorTextMain;
  }
  if (strstr(text, "] INFO ") != nullptr) {
    return kColorLiveInfo;
  }
  if (strstr(text, "] DM ") != nullptr) {
    return kColorLiveDirect;
  }
  if (strstr(text, "] CH ") != nullptr) {
    return kColorLiveChannel;
  }
  return kColorTextMain;
}

void resetPointerInputState() {
  for (lv_indev_t* indev = lv_indev_get_next(nullptr); indev; indev = lv_indev_get_next(indev)) {
    if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
      lv_indev_reset(indev, nullptr);
    }
  }
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

const lv_font_t* compactUiFont() {
#if defined(LV_FONT_MONTSERRAT_8) && LV_FONT_MONTSERRAT_8
  return &lv_font_montserrat_8;
#elif defined(LV_FONT_MONTSERRAT_10) && LV_FONT_MONTSERRAT_10
  return &lv_font_montserrat_10;
#else
  return chatPanelFont();
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

  lv_style_init(&style_unread_edge_);
  lv_style_set_bg_opa(&style_unread_edge_, LV_OPA_TRANSP);
  lv_style_set_border_width(&style_unread_edge_, 2);
  lv_style_set_border_color(&style_unread_edge_, kColorUnread);
  lv_style_set_border_opa(&style_unread_edge_, LV_OPA_60);
  lv_style_set_outline_width(&style_unread_edge_, 1);
  lv_style_set_outline_pad(&style_unread_edge_, 1);
  lv_style_set_outline_color(&style_unread_edge_, kColorUnread);
  lv_style_set_outline_opa(&style_unread_edge_, LV_OPA_40);

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

void StandaloneUi::setFirstInstallIdentityPrompt(bool enabled) {
  first_install_identity_prompt_ = enabled;
  first_install_auto_export_pending_ = enabled;
}

bool StandaloneUi::ensureContactsDialogBuilt() {
  if (!kEnableContactsDialog) {
    return false;
  }

  if (contacts_dialog_) {
    return true;
  }
  if (!main_panel_ || !header_bar_) {
    return false;
  }

  CTS_TRACE("ensureContactsDialogBuilt begin");

  const lv_coord_t header_h = lv_obj_get_height(header_bar_);
  lv_coord_t main_h = lv_obj_get_height(main_panel_);
  const lv_coord_t body_y = static_cast<lv_coord_t>(header_h + kGap);
  lv_coord_t body_h = static_cast<lv_coord_t>(main_h - body_y - kMainBottomInset);
  if (body_h < 64) {
    body_h = 64;
  }

  bool contacts_init_failed = false;
  do {
    contacts_dialog_ = lv_obj_create(main_panel_);
    if (!contacts_dialog_) {
      contacts_init_failed = true;
      break;
    }
    lv_obj_add_style(contacts_dialog_, &style_chat_, 0);
    lv_obj_set_pos(contacts_dialog_, 0, body_y);
    lv_obj_set_size(contacts_dialog_, LV_PCT(100), body_h);
    lv_obj_add_flag(contacts_dialog_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(contacts_dialog_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(contacts_dialog_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(contacts_dialog_, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(contacts_dialog_, onContactsEvent, LV_EVENT_CLICKED, this);

    contacts_status_label_ = lv_label_create(contacts_dialog_);
    if (!contacts_status_label_) {
      contacts_init_failed = true;
      break;
    }
    lv_obj_add_style(contacts_status_label_, &style_text_dim_, 0);
#if defined(LV_FONT_MONTSERRAT_10) && LV_FONT_MONTSERRAT_10
    lv_obj_set_style_text_font(contacts_status_label_, &lv_font_montserrat_10, 0);
#endif
    lv_obj_set_width(contacts_status_label_, LV_PCT(100));
    lv_obj_align(contacts_status_label_, LV_ALIGN_TOP_LEFT, 2, 2);
    lv_label_set_text(contacts_status_label_, "");

    contacts_detail_info_panel_ = lv_obj_create(contacts_dialog_);
    if (!contacts_detail_info_panel_) {
      contacts_init_failed = true;
      break;
    }
    lv_obj_set_pos(contacts_detail_info_panel_, 0, 2);
    lv_obj_set_size(contacts_detail_info_panel_, LV_PCT(100), static_cast<lv_coord_t>(body_h - 48));
    lv_obj_set_style_bg_opa(contacts_detail_info_panel_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(contacts_detail_info_panel_, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(contacts_detail_info_panel_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(contacts_detail_info_panel_, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_row(contacts_detail_info_panel_, 2, LV_PART_MAIN);
    lv_obj_set_layout(contacts_detail_info_panel_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(contacts_detail_info_panel_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(contacts_detail_info_panel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_scroll_dir(contacts_detail_info_panel_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(contacts_detail_info_panel_, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(contacts_detail_info_panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(contacts_detail_info_panel_, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(contacts_detail_info_panel_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(contacts_detail_info_panel_, onContactsEvent, LV_EVENT_CLICKED, this);

    contacts_full_name_label_ = lv_label_create(contacts_detail_info_panel_);
    if (!contacts_full_name_label_) {
      contacts_init_failed = true;
      break;
    }
    lv_obj_add_style(contacts_full_name_label_, &style_text_main_, 0);
    lv_obj_set_style_text_font(contacts_full_name_label_, compactUiFont(), 0);
    lv_obj_set_width(contacts_full_name_label_, LV_PCT(100));
    lv_label_set_long_mode(contacts_full_name_label_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(contacts_full_name_label_, "Node: -");

    contacts_lat_lon_label_ = lv_label_create(contacts_detail_info_panel_);
    if (!contacts_lat_lon_label_) {
      contacts_init_failed = true;
      break;
    }
    lv_obj_add_style(contacts_lat_lon_label_, &style_text_dim_, 0);
    lv_obj_set_style_text_font(contacts_lat_lon_label_, compactUiFont(), 0);
    lv_obj_set_width(contacts_lat_lon_label_, LV_PCT(100));
    lv_label_set_long_mode(contacts_lat_lon_label_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(contacts_lat_lon_label_, "Lat/Lon: -");

    contacts_last_heard_label_ = lv_label_create(contacts_detail_info_panel_);
    if (!contacts_last_heard_label_) {
      contacts_init_failed = true;
      break;
    }
    lv_obj_add_style(contacts_last_heard_label_, &style_text_dim_, 0);
    lv_obj_set_style_text_font(contacts_last_heard_label_, compactUiFont(), 0);
    lv_obj_set_width(contacts_last_heard_label_, LV_PCT(100));
    lv_label_set_long_mode(contacts_last_heard_label_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(contacts_last_heard_label_, "Last: -");

    contacts_telemetry_label_ = lv_label_create(contacts_detail_info_panel_);
    if (!contacts_telemetry_label_) {
      contacts_init_failed = true;
      break;
    }
    lv_obj_add_style(contacts_telemetry_label_, &style_text_dim_, 0);
    lv_obj_set_style_text_font(contacts_telemetry_label_, compactUiFont(), 0);
    lv_obj_set_width(contacts_telemetry_label_, LV_PCT(100));
    lv_label_set_long_mode(contacts_telemetry_label_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(contacts_telemetry_label_, "Telemetry: -");

    contacts_dm_new_btn_ = lv_btn_create(contacts_dialog_);
    if (!contacts_dm_new_btn_) {
      contacts_init_failed = true;
      break;
    }
    lv_obj_set_size(contacts_dm_new_btn_, 56, 18);
    lv_obj_align(contacts_dm_new_btn_, LV_ALIGN_TOP_RIGHT, -2, 18);
    lv_obj_add_style(contacts_dm_new_btn_, &style_button_, 0);
    lv_obj_add_style(contacts_dm_new_btn_, &style_button_focused_, LV_STATE_FOCUSED);
    lv_obj_clear_flag(contacts_dm_new_btn_, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(contacts_dm_new_btn_, onFocusableEvent, LV_EVENT_KEY, this);
    lv_obj_add_event_cb(contacts_dm_new_btn_, onContactsEvent, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(contacts_dm_new_btn_, onFocusableEvent, LV_EVENT_FOCUSED, this);
    lv_obj_add_flag(contacts_dm_new_btn_, LV_OBJ_FLAG_HIDDEN);

    contacts_dm_new_label_ = lv_label_create(contacts_dm_new_btn_);
    if (!contacts_dm_new_label_) {
      contacts_init_failed = true;
      break;
    }
    lv_obj_add_style(contacts_dm_new_label_, &style_text_main_, 0);
    lv_label_set_text(contacts_dm_new_label_, "NEW");
    lv_obj_center(contacts_dm_new_label_);

    contacts_dm_panel_ = lv_obj_create(contacts_dialog_);
    if (!contacts_dm_panel_) {
      contacts_init_failed = true;
      break;
    }
    lv_obj_set_pos(contacts_dm_panel_, 0, 42);
    lv_obj_set_size(contacts_dm_panel_, LV_PCT(100), static_cast<lv_coord_t>(body_h - 66));
    lv_obj_add_style(contacts_dm_panel_, &style_chat_, 0);
    lv_obj_set_scroll_dir(contacts_dm_panel_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(contacts_dm_panel_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_layout(contacts_dm_panel_, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(contacts_dm_panel_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(contacts_dm_panel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(contacts_dm_panel_, 1, LV_PART_MAIN);
    lv_obj_add_event_cb(contacts_dm_panel_, onFocusableEvent, LV_EVENT_KEY, this);
    lv_obj_add_event_cb(contacts_dm_panel_, onContactsEvent, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(contacts_dm_panel_, onFocusableEvent, LV_EVENT_FOCUSED, this);
    lv_obj_add_flag(contacts_dm_panel_, LV_OBJ_FLAG_HIDDEN);

    contacts_detail_panel_ = lv_obj_create(contacts_dialog_);
    if (!contacts_detail_panel_) {
      contacts_init_failed = true;
      break;
    }
    lv_obj_set_size(contacts_detail_panel_, LV_PCT(100),
  #if defined(DEVICE_HELTEC_V4_EXPANSION)
            46
  #else
            22
  #endif
    );
    lv_obj_align(contacts_detail_panel_, LV_ALIGN_BOTTOM_MID, 0, -1);
    lv_obj_set_style_bg_opa(contacts_detail_panel_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(contacts_detail_panel_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(contacts_detail_panel_, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(contacts_detail_panel_, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_row(contacts_detail_panel_, 2, LV_PART_MAIN);
    lv_obj_set_layout(contacts_detail_panel_, LV_LAYOUT_FLEX);
  #if defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_obj_set_flex_flow(contacts_detail_panel_, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(contacts_detail_panel_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER);
  #else
    lv_obj_set_flex_flow(contacts_detail_panel_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(contacts_detail_panel_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                LV_FLEX_ALIGN_CENTER);
  #endif

    static const char* kContactActionInitLabels[kContactActionCount] = {
      addFavoriteActionLabel(),
    #if defined(DEVICE_HELTEC_V4_EXPANSION)
      "DM",
    #else
      "(D)M",
    #endif
#if defined(DEVICE_HELTEC_V4_EXPANSION)
      "CLOSE",
#endif
    };
    for (uint8_t i = 0; i < kContactActionCount; i++) {
      contacts_action_rows_[i] = lv_btn_create(contacts_detail_panel_);
      if (!contacts_action_rows_[i]) {
        contacts_init_failed = true;
        break;
      }
      if (i < 2) {
        lv_obj_set_width(contacts_action_rows_[i], LV_PCT(49));
      } else {
        lv_obj_set_width(contacts_action_rows_[i], LV_PCT(100));
      }
      lv_obj_set_height(contacts_action_rows_[i], 22);
      lv_obj_add_style(contacts_action_rows_[i], &style_button_, 0);
      lv_obj_add_style(contacts_action_rows_[i], &style_button_focused_, LV_STATE_FOCUSED);
      lv_obj_clear_flag(contacts_action_rows_[i], LV_OBJ_FLAG_EVENT_BUBBLE);
      lv_obj_add_event_cb(contacts_action_rows_[i], onFocusableEvent, LV_EVENT_KEY, this);
      lv_obj_add_event_cb(contacts_action_rows_[i], onContactsEvent, LV_EVENT_CLICKED, this);
      lv_obj_add_event_cb(contacts_action_rows_[i], onFocusableEvent, LV_EVENT_FOCUSED, this);

      contacts_action_labels_[i] = lv_label_create(contacts_action_rows_[i]);
      if (!contacts_action_labels_[i]) {
        contacts_init_failed = true;
        break;
      }
      lv_obj_add_style(contacts_action_labels_[i], &style_text_main_, 0);
      lv_label_set_text(contacts_action_labels_[i], kContactActionInitLabels[i]);
      lv_obj_center(contacts_action_labels_[i]);
    }
    if (contacts_init_failed) {
      break;
    }
    CTS_TRACE("ensureContactsDialogBuilt done");
  } while (false);

  if (!contacts_init_failed) {
    return true;
  }

  CTS_TRACE("ensureContactsDialogBuilt failed");
  if (false) Serial.println("[UI] Contacts dialog disabled: allocation failed");
  if (contacts_dialog_) {
    lv_obj_del(contacts_dialog_);
  }
  contacts_dialog_ = nullptr;
  contacts_status_label_ = nullptr;
  contacts_detail_panel_ = nullptr;
  contacts_detail_info_panel_ = nullptr;
  contacts_full_name_label_ = nullptr;
  contacts_lat_lon_label_ = nullptr;
  contacts_last_heard_label_ = nullptr;
  contacts_telemetry_label_ = nullptr;
  contacts_dm_panel_ = nullptr;
  contacts_dm_new_btn_ = nullptr;
  contacts_dm_new_label_ = nullptr;
  memset(contacts_action_rows_, 0, sizeof(contacts_action_rows_));
  memset(contacts_action_labels_, 0, sizeof(contacts_action_labels_));
  return false;
}

void StandaloneUi::buildLayout() {
  const lv_coord_t screen_w = lv_disp_get_hor_res(nullptr);
  const lv_coord_t screen_h = lv_disp_get_ver_res(nullptr);

  const lv_coord_t main_w = screen_w - (kOuterPad * 2);
  const lv_coord_t main_h = screen_h - (kOuterPad * 2);

  const lv_coord_t header_h = clampCoord(kHeaderH + ((screen_h - 170) / 14), 30, 35);
  const lv_coord_t shortcut_h = clampCoord(kShortcutH + ((screen_h - 170) / 18),
                                           kShortcutMinH, kShortcutMaxH);
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
  lv_obj_set_style_border_width(main_panel_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(main_panel_, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(main_panel_, 0, LV_PART_MAIN);
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
  lv_obj_set_style_radius(channel_selector_btn_, kChannelButtonRadius, LV_PART_MAIN);
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
  const lv_coord_t dropdown_pad_top = lv_obj_get_style_pad_top(channel_dropdown_list_, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(channel_dropdown_list_, dropdown_pad_top, LV_PART_MAIN);
  lv_obj_set_style_width(channel_dropdown_list_, kDropdownScrollbarW, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(channel_dropdown_list_, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_color(channel_dropdown_list_, lv_color_hex(0x2C7CA5), LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(channel_dropdown_list_, 2, LV_PART_SCROLLBAR);

  for (uint8_t i = 0; i < kChannelCount; i++) {
    channel_dropdown_rows_[i] = lv_btn_create(channel_dropdown_list_);
    lv_obj_set_width(channel_dropdown_rows_[i], LV_PCT(100));
    lv_obj_set_height(channel_dropdown_rows_[i], kDropdownRowH);
    lv_obj_set_style_radius(channel_dropdown_rows_[i], kChannelButtonRadius, LV_PART_MAIN);
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
  lv_obj_add_flag(battery_pct_label_, LV_OBJ_FLAG_HIDDEN);

  gps_label_ = lv_label_create(header_bar_);
  lv_obj_add_style(gps_label_, &style_text_dim_, 0);
  lv_obj_set_style_text_font(gps_label_, header_font, 0);
  lv_obj_align_to(gps_label_, channel_selector_btn_, LV_ALIGN_OUT_RIGHT_MID, kHeaderTimeGap, 0);

  wifi_label_ = lv_label_create(header_bar_);
  lv_obj_add_style(wifi_label_, &style_text_dim_, 0);
  lv_obj_set_style_text_font(wifi_label_, header_font, 0);
  lv_obj_align_to(wifi_label_, gps_label_, LV_ALIGN_OUT_RIGHT_MID, kHeaderIconsGap, 0);

  wifi_ap_badge_label_ = lv_label_create(header_bar_);
  lv_obj_add_style(wifi_ap_badge_label_, &style_text_main_, 0);
  lv_obj_set_style_text_font(wifi_ap_badge_label_, chatPanelFont(), 0);
  lv_label_set_text(wifi_ap_badge_label_, "AP");
  lv_obj_add_flag(wifi_ap_badge_label_, LV_OBJ_FLAG_HIDDEN);

  battery_bar_ = lv_bar_create(header_bar_);
  lv_obj_set_size(battery_bar_, 26, 6);
  lv_obj_align(battery_bar_, LV_ALIGN_RIGHT_MID, kHeaderBatteryBarX, 0);
  lv_bar_set_range(battery_bar_, 0, 100);
  lv_obj_set_style_bg_color(battery_bar_, lv_color_hex(0x0B1E2D), LV_PART_MAIN);
  lv_obj_set_style_border_color(battery_bar_, kColorBorder, LV_PART_MAIN);
  lv_obj_set_style_border_width(battery_bar_, 1, LV_PART_MAIN);
  lv_obj_set_style_bg_color(battery_bar_, lv_color_hex(0x59D8A0), LV_PART_INDICATOR);
  lv_obj_set_style_pad_all(battery_bar_, 0, LV_PART_MAIN);

  time_label_ = lv_label_create(header_bar_);
  lv_obj_add_style(time_label_, &style_text_main_, 0);
  lv_obj_set_style_text_font(time_label_, header_font, 0);
  lv_obj_align_to(time_label_, battery_bar_, LV_ALIGN_OUT_LEFT_MID, -kHeaderIconsToBatteryGap, 0);

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

#if defined(DEVICE_HELTEC_V4_EXPANSION)
  chat_advz_btn_ = lv_btn_create(main_panel_);
  lv_obj_set_size(chat_advz_btn_, 56, 18);
  lv_obj_align(chat_advz_btn_, LV_ALIGN_BOTTOM_RIGHT, -122,
               static_cast<lv_coord_t>(-(shortcut_h + kMainBottomInset + kHeltecMainButtonBottomInset)));
  lv_obj_add_style(chat_advz_btn_, &style_button_, 0);
  lv_obj_add_style(chat_advz_btn_, &style_button_focused_, LV_STATE_FOCUSED);
  lv_obj_add_event_cb(chat_advz_btn_, onFocusableEvent, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(chat_advz_btn_, onFocusableEvent, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(chat_advz_btn_, onFocusableEvent, LV_EVENT_FOCUSED, this);

  chat_advz_label_ = lv_label_create(chat_advz_btn_);
  lv_obj_add_style(chat_advz_label_, &style_text_main_, 0);
  lv_label_set_text(chat_advz_label_, "ADVZ");
  lv_obj_center(chat_advz_label_);

  chat_advf_btn_ = lv_btn_create(main_panel_);
  lv_obj_set_size(chat_advf_btn_, 56, 18);
  lv_obj_align(chat_advf_btn_, LV_ALIGN_BOTTOM_RIGHT, -62,
               static_cast<lv_coord_t>(-(shortcut_h + kMainBottomInset + kHeltecMainButtonBottomInset)));
  lv_obj_add_style(chat_advf_btn_, &style_button_, 0);
  lv_obj_add_style(chat_advf_btn_, &style_button_focused_, LV_STATE_FOCUSED);
  lv_obj_add_event_cb(chat_advf_btn_, onFocusableEvent, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(chat_advf_btn_, onFocusableEvent, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(chat_advf_btn_, onFocusableEvent, LV_EVENT_FOCUSED, this);

  chat_advf_label_ = lv_label_create(chat_advf_btn_);
  lv_obj_add_style(chat_advf_label_, &style_text_main_, 0);
  lv_label_set_text(chat_advf_label_, "ADVF");
  lv_obj_center(chat_advf_label_);

  chat_new_btn_ = lv_btn_create(main_panel_);
  lv_obj_set_size(chat_new_btn_, 56, 18);
  lv_obj_align(chat_new_btn_, LV_ALIGN_BOTTOM_RIGHT, -2,
               static_cast<lv_coord_t>(-(shortcut_h + kMainBottomInset + kHeltecMainButtonBottomInset)));
  lv_obj_add_style(chat_new_btn_, &style_button_, 0);
  lv_obj_add_style(chat_new_btn_, &style_button_focused_, LV_STATE_FOCUSED);
  lv_obj_add_event_cb(chat_new_btn_, onFocusableEvent, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(chat_new_btn_, onFocusableEvent, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(chat_new_btn_, onFocusableEvent, LV_EVENT_FOCUSED, this);

  chat_new_label_ = lv_label_create(chat_new_btn_);
  lv_obj_add_style(chat_new_label_, &style_text_main_, 0);
  lv_label_set_text(chat_new_label_, "NEW");
  lv_obj_center(chat_new_label_);
#else
  chat_advz_btn_ = nullptr;
  chat_advz_label_ = nullptr;
  chat_advf_btn_ = nullptr;
  chat_advf_label_ = nullptr;
  chat_new_btn_ = nullptr;
  chat_new_label_ = nullptr;
#endif

  compose_dialog_ = lv_obj_create(root_);
  lv_obj_add_style(compose_dialog_, &style_panel_, 0);
#if defined(DEVICE_HELTEC_V4_EXPANSION)
  lv_obj_set_size(compose_dialog_, clampCoord(static_cast<lv_coord_t>(main_w - 8), 180, main_w),
                  clampCoord(static_cast<lv_coord_t>(screen_h - 8), 120, screen_h));
  lv_obj_align(compose_dialog_, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_pad_all(compose_dialog_, 4, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(compose_dialog_, 2, LV_PART_MAIN);
  lv_obj_set_style_pad_row(compose_dialog_, 1, LV_PART_MAIN);
  lv_obj_set_flex_flow(compose_dialog_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(compose_dialog_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
#else
  lv_obj_set_size(compose_dialog_,
                  clampCoord(main_w - dialogInsetW(8, 2), kComposeDialogMinW,
                             dialogMaxW(kComposeDialogMaxW, 338)),
                  kComposeDialogH);
  lv_obj_center(compose_dialog_);
#endif
  lv_obj_add_flag(compose_dialog_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(compose_dialog_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(compose_dialog_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(compose_dialog_, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_set_style_border_width(compose_dialog_, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(compose_dialog_, kColorBorder, LV_PART_MAIN);
  lv_obj_set_style_border_opa(compose_dialog_, LV_OPA_40, LV_PART_MAIN);
#if !defined(DEVICE_HELTEC_V4_EXPANSION)
  lv_obj_add_event_cb(compose_dialog_, onFocusableEvent, LV_EVENT_CLICKED, this);
#endif

  compose_title_label_ = lv_label_create(compose_dialog_);
  lv_obj_add_style(compose_title_label_, &style_text_main_, 0);
#if defined(DEVICE_HELTEC_V4_EXPANSION)
  lv_obj_set_width(compose_title_label_, LV_PCT(100));
#else
  lv_obj_align(compose_title_label_, LV_ALIGN_TOP_LEFT, 4, 2);
#endif

  compose_hint_label_ = lv_label_create(compose_dialog_);
  lv_obj_add_style(compose_hint_label_, &style_text_dim_, 0);
#if defined(LV_FONT_MONTSERRAT_10) && LV_FONT_MONTSERRAT_10
  lv_obj_set_style_text_font(compose_hint_label_, &lv_font_montserrat_10, 0);
#endif
  lv_obj_set_width(compose_hint_label_, LV_PCT(composeHintWidthPct()));
  lv_label_set_long_mode(compose_hint_label_, LV_LABEL_LONG_WRAP);
  lv_obj_align(compose_hint_label_, LV_ALIGN_TOP_LEFT, kPagerWideDialogLayout ? 2 : 4, 16);
  lv_label_set_text(compose_hint_label_, "");
#if defined(DEVICE_HELTEC_V4_EXPANSION)
  lv_obj_add_flag(compose_hint_label_, LV_OBJ_FLAG_HIDDEN);
#endif

  compose_input_ = lv_textarea_create(compose_dialog_);
#if defined(DEVICE_HELTEC_V4_EXPANSION)
  lv_obj_set_width(compose_input_, LV_PCT(100));
  lv_obj_set_height(compose_input_, 38);
  lv_obj_set_style_text_color(compose_input_, lv_color_hex(0xE8F1FF), 0);
  lv_obj_set_style_bg_color(compose_input_, lv_color_hex(0x102B61), 0);
  lv_obj_set_style_bg_opa(compose_input_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(compose_input_, 1, 0);
  lv_obj_set_style_border_color(compose_input_, lv_color_hex(0x4C76BA), 0);
  lv_obj_set_style_pad_top(compose_input_, 1, 0);
  lv_obj_set_style_pad_bottom(compose_input_, 1, 0);
  lv_obj_set_style_pad_left(compose_input_, 3, 0);
  lv_obj_set_style_pad_right(compose_input_, 3, 0);
  lv_textarea_set_one_line(compose_input_, true);
#else
  lv_obj_set_size(compose_input_, LV_PCT(composeInputWidthPct()), kComposeInputH);
  lv_obj_align(compose_input_, LV_ALIGN_BOTTOM_MID, 0, composeInputBottomInset());
  lv_textarea_set_one_line(compose_input_, false);
#endif
#if defined(LV_FONT_MONTSERRAT_10) && LV_FONT_MONTSERRAT_10
  lv_obj_set_style_text_font(compose_input_, &lv_font_montserrat_10, 0);
#endif
  lv_textarea_set_max_length(compose_input_, static_cast<uint16_t>(kComposeMessageMaxChars));
  lv_textarea_set_placeholder_text(compose_input_, "Type and press Enter");
#if !defined(DEVICE_HELTEC_V4_EXPANSION)
  lv_obj_add_event_cb(compose_input_, onFocusableEvent, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(compose_input_, onFocusableEvent, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(compose_input_, onFocusableEvent, LV_EVENT_FOCUSED, this);
#endif

#if defined(DEVICE_HELTEC_V4_EXPANSION)
  compose_action_row_ = lv_obj_create(compose_dialog_);
  lv_obj_set_width(compose_action_row_, LV_PCT(100));
  lv_obj_set_height(compose_action_row_, 28);
  lv_obj_clear_flag(compose_action_row_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(compose_action_row_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(compose_action_row_, 0, 0);
  lv_obj_set_style_pad_all(compose_action_row_, 0, 0);
  lv_obj_set_style_pad_column(compose_action_row_, 4, 0);
  lv_obj_set_flex_flow(compose_action_row_, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(compose_action_row_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  compose_cancel_btn_ = lv_btn_create(compose_action_row_);
  lv_obj_set_flex_grow(compose_cancel_btn_, 1);
  lv_obj_set_height(compose_cancel_btn_, LV_PCT(100));
  lv_obj_add_style(compose_cancel_btn_, &style_button_, 0);
  lv_obj_add_style(compose_cancel_btn_, &style_button_focused_, LV_STATE_FOCUSED);
  lv_obj_add_event_cb(compose_cancel_btn_, onComposeActionEvent, LV_EVENT_CLICKED, this);
  compose_cancel_label_ = lv_label_create(compose_cancel_btn_);
  lv_obj_add_style(compose_cancel_label_, &style_text_main_, 0);
  lv_label_set_text(compose_cancel_label_, "Cancel");
  lv_obj_center(compose_cancel_label_);

  compose_send_btn_ = lv_btn_create(compose_action_row_);
  lv_obj_set_flex_grow(compose_send_btn_, 1);
  lv_obj_set_height(compose_send_btn_, LV_PCT(100));
  lv_obj_add_style(compose_send_btn_, &style_button_, 0);
  lv_obj_add_style(compose_send_btn_, &style_button_focused_, LV_STATE_FOCUSED);
  lv_obj_add_event_cb(compose_send_btn_, onComposeActionEvent, LV_EVENT_CLICKED, this);
  compose_send_label_ = lv_label_create(compose_send_btn_);
  lv_obj_add_style(compose_send_label_, &style_text_main_, 0);
  lv_label_set_text(compose_send_label_, "Send");
  lv_obj_center(compose_send_label_);
#else
  compose_action_row_ = nullptr;
  compose_cancel_btn_ = nullptr;
  compose_cancel_label_ = nullptr;
  compose_send_btn_ = nullptr;
  compose_send_label_ = nullptr;
#endif

#if defined(LV_USE_KEYBOARD) && LV_USE_KEYBOARD
  compose_keyboard_ = lv_keyboard_create(compose_dialog_);
  if (compose_keyboard_) {
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_obj_set_width(compose_keyboard_, LV_PCT(100));
    lv_obj_set_flex_grow(compose_keyboard_, 1);
#else
    lv_obj_set_size(compose_keyboard_, LV_PCT(100), clampCoord(static_cast<lv_coord_t>(screen_h / 2), 74, 118));
    lv_obj_align(compose_keyboard_, LV_ALIGN_BOTTOM_MID, 0, 0);
#endif
    lv_obj_add_style(compose_keyboard_, &style_panel_, LV_PART_MAIN);
    lv_obj_clear_flag(compose_keyboard_, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_keyboard_set_textarea(compose_keyboard_, compose_input_);
    lv_obj_add_event_cb(compose_keyboard_, onComposeKeyboardEvent, LV_EVENT_READY, this);
    lv_obj_add_event_cb(compose_keyboard_, onComposeKeyboardEvent, LV_EVENT_CANCEL, this);
    lv_obj_add_flag(compose_keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(compose_keyboard_, LV_OBJ_FLAG_CLICKABLE);
  }
#else
  compose_keyboard_ = nullptr;
#endif

  cfg_dialog_ = lv_obj_create(root_);
  lv_obj_add_style(cfg_dialog_, &style_panel_, 0);
  const lv_coord_t cfg_dialog_w =
      clampCoord(main_w - dialogInsetW(6, 2), 220, dialogMaxW(280, 340));
  const lv_coord_t cfg_dialog_h = clampCoord(main_h - 10, 120, 230);
  lv_obj_set_size(cfg_dialog_, cfg_dialog_w, cfg_dialog_h);
  lv_obj_align(cfg_dialog_, LV_ALIGN_CENTER, 0, kModalVerticalNudgeY);
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
  if (!kPagerWideDialogLayout) {
    lv_obj_set_style_text_font(cfg_status_label_, &lv_font_montserrat_10, 0);
  }
#endif
  lv_obj_set_width(cfg_status_label_, LV_PCT(100));
  lv_obj_align(cfg_status_label_, LV_ALIGN_BOTTOM_LEFT, kPagerWideDialogLayout ? 1 : 4, -2);

#if defined(DEVICE_HELTEC_V4_EXPANSION)
  cfg_close_btn_ = lv_btn_create(cfg_dialog_);
  lv_obj_set_size(cfg_close_btn_, LV_PCT(100), 22);
  lv_obj_align(cfg_close_btn_, LV_ALIGN_BOTTOM_MID, 0, -2);
  lv_obj_add_style(cfg_close_btn_, &style_button_, 0);
  lv_obj_add_style(cfg_close_btn_, &style_button_focused_, LV_STATE_FOCUSED);
  lv_obj_add_event_cb(cfg_close_btn_, onFocusableEvent, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(cfg_close_btn_, onFocusableEvent, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(cfg_close_btn_, onFocusableEvent, LV_EVENT_FOCUSED, this);

  cfg_close_label_ = lv_label_create(cfg_close_btn_);
  lv_obj_add_style(cfg_close_label_, &style_text_main_, 0);
  lv_label_set_text(cfg_close_label_, "CLOSE");
  lv_obj_center(cfg_close_label_);

  lv_obj_add_flag(cfg_status_label_, LV_OBJ_FLAG_HIDDEN);
#else
  cfg_close_btn_ = nullptr;
  cfg_close_label_ = nullptr;
#endif

  const lv_coord_t cfg_row_h = kPagerWideDialogLayout ? 22 : 20;
  const lv_coord_t cfg_row_step = kPagerWideDialogLayout ? 22 : 20;
  const lv_coord_t cfg_rows_x = kPagerWideDialogLayout ? 0 : 2;

  // Wrap the rows in a scrollable content panel so small screens (e.g.
  // Cardputer 240x135) can pan through all rows. The panel sits between the
  // title bar at the top and the status label at the bottom; LVGL's group
  // focus auto-scrolls the focused row into view during keyboard nav.
#if defined(DEVICE_HELTEC_V4_EXPANSION)
  const lv_coord_t cfg_content_top = 18;
  const lv_coord_t cfg_content_bottom_reserved = 26;  // close btn + padding
#else
  const lv_coord_t cfg_content_top = 18;
  const lv_coord_t cfg_content_bottom_reserved = kPagerWideDialogLayout ? 16 : 14;
#endif

  cfg_content_panel_ = lv_obj_create(cfg_dialog_);
  lv_obj_set_pos(cfg_content_panel_, cfg_rows_x, cfg_content_top);
  lv_obj_set_size(cfg_content_panel_,
                  static_cast<lv_coord_t>(LV_PCT(100)),
                  static_cast<lv_coord_t>(cfg_dialog_h - cfg_content_top -
                                          cfg_content_bottom_reserved));
  lv_obj_set_style_bg_opa(cfg_content_panel_, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(cfg_content_panel_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(cfg_content_panel_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(cfg_content_panel_, 0, LV_PART_MAIN);
  lv_obj_add_flag(cfg_content_panel_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(cfg_content_panel_, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(cfg_content_panel_, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_clear_flag(cfg_content_panel_, LV_OBJ_FLAG_CLICKABLE);

  for (uint8_t i = 0; i < kCfgRowCount; i++) {
    cfg_rows_[i] = lv_btn_create(cfg_content_panel_);
    lv_obj_set_size(cfg_rows_[i], LV_PCT(100), cfg_row_h);
    lv_obj_set_pos(cfg_rows_[i], 0, static_cast<lv_coord_t>(i * cfg_row_step));
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
  lv_obj_t* cfg_divider = lv_obj_create(cfg_content_panel_);
  lv_obj_set_size(cfg_divider, LV_PCT(100), 3);
  lv_obj_set_pos(cfg_divider, 0,
                 static_cast<lv_coord_t>((2 * cfg_row_step) - 1));
  lv_obj_set_style_bg_opa(cfg_divider, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(cfg_divider, lv_color_hex(0x2B4A63), 0);
  lv_obj_set_style_border_width(cfg_divider, 0, 0);
  lv_obj_set_style_radius(cfg_divider, 0, 0);
  lv_obj_clear_flag(cfg_divider, LV_OBJ_FLAG_CLICKABLE);

  contacts_dialog_ = nullptr;
  contacts_status_label_ = nullptr;
  contacts_detail_panel_ = nullptr;
  contacts_detail_info_panel_ = nullptr;
  contacts_full_name_label_ = nullptr;
  contacts_lat_lon_label_ = nullptr;
  contacts_last_heard_label_ = nullptr;
  contacts_telemetry_label_ = nullptr;
  contacts_dm_panel_ = nullptr;
  contacts_dm_new_btn_ = nullptr;
  contacts_dm_new_label_ = nullptr;
  contacts_nav_focused_ = false;
  contacts_dm_open_ = false;
  memset(contacts_action_rows_, 0, sizeof(contacts_action_rows_));
  memset(contacts_action_labels_, 0, sizeof(contacts_action_labels_));

  dm_dialog_ = lv_obj_create(root_);
  lv_obj_add_style(dm_dialog_, &style_panel_, 0);
  lv_obj_set_size(dm_dialog_, clampCoord(main_w - 6, 220, dialogMaxW(300, 336)),
                  clampCoord(main_h - 10, 170, 230));
  lv_obj_center(dm_dialog_);
  lv_obj_add_flag(dm_dialog_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(dm_dialog_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(dm_dialog_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(dm_dialog_, onDmEvent, LV_EVENT_PRESSED, this);

  dm_title_label_ = lv_label_create(dm_dialog_);
  lv_obj_add_style(dm_title_label_, &style_text_main_, 0);
  lv_label_set_text(dm_title_label_, "DM");
  lv_obj_align(dm_title_label_, LV_ALIGN_TOP_LEFT, 4, 2);

#if defined(DEVICE_HELTEC_V4_EXPANSION)
  dm_new_btn_ = lv_btn_create(dm_dialog_);
  lv_obj_set_size(dm_new_btn_, 56, 18);
  lv_obj_align(dm_new_btn_, LV_ALIGN_BOTTOM_RIGHT, -2, -30);
  lv_obj_add_style(dm_new_btn_, &style_button_, 0);
  lv_obj_add_style(dm_new_btn_, &style_button_focused_, LV_STATE_FOCUSED);
  lv_obj_clear_flag(dm_new_btn_, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_event_cb(dm_new_btn_, onFocusableEvent, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(dm_new_btn_, onDmEvent, LV_EVENT_PRESSED, this);
  lv_obj_add_event_cb(dm_new_btn_, onFocusableEvent, LV_EVENT_FOCUSED, this);

  dm_new_label_ = lv_label_create(dm_new_btn_);
  lv_obj_add_style(dm_new_label_, &style_text_main_, 0);
  lv_label_set_text(dm_new_label_, "NEW");
  lv_obj_center(dm_new_label_);
#else
  dm_new_btn_ = nullptr;
  dm_new_label_ = nullptr;
#endif

  dm_panel_ = lv_obj_create(dm_dialog_);
  lv_obj_set_pos(dm_panel_, 2, 18);
#if defined(DEVICE_HELTEC_V4_EXPANSION)
  lv_obj_set_size(dm_panel_, LV_PCT(100), static_cast<lv_coord_t>(lv_obj_get_height(dm_dialog_) - 42));
#else
  lv_obj_set_size(dm_panel_, LV_PCT(100), static_cast<lv_coord_t>(lv_obj_get_height(dm_dialog_) - 22));
#endif
  lv_obj_add_style(dm_panel_, &style_chat_, 0);
  lv_obj_set_scroll_dir(dm_panel_, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(dm_panel_, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_layout(dm_panel_, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(dm_panel_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(dm_panel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(dm_panel_, 1, LV_PART_MAIN);
  lv_obj_add_event_cb(dm_panel_, onFocusableEvent, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(dm_panel_, onDmEvent, LV_EVENT_PRESSED, this);
  lv_obj_add_event_cb(dm_panel_, onFocusableEvent, LV_EVENT_FOCUSED, this);

#if defined(DEVICE_HELTEC_V4_EXPANSION)
  dm_close_btn_ = lv_btn_create(dm_dialog_);
  lv_obj_set_size(dm_close_btn_, LV_PCT(100), 22);
  lv_obj_align(dm_close_btn_, LV_ALIGN_BOTTOM_MID, 0, -2);
  lv_obj_add_style(dm_close_btn_, &style_button_, 0);
  lv_obj_add_style(dm_close_btn_, &style_button_focused_, LV_STATE_FOCUSED);
  lv_obj_clear_flag(dm_close_btn_, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_event_cb(dm_close_btn_, onFocusableEvent, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(dm_close_btn_, onDmEvent, LV_EVENT_PRESSED, this);
  lv_obj_add_event_cb(dm_close_btn_, onFocusableEvent, LV_EVENT_FOCUSED, this);

  dm_close_label_ = lv_label_create(dm_close_btn_);
  lv_obj_add_style(dm_close_label_, &style_text_main_, 0);
  lv_label_set_text(dm_close_label_, "CLOSE");
  lv_obj_center(dm_close_label_);

  // Keep overlay controls above the scrollable message panel.
  lv_obj_move_foreground(dm_close_btn_);
  if (dm_new_btn_) {
    lv_obj_move_foreground(dm_new_btn_);
  }
#else
  dm_close_btn_ = nullptr;
  dm_close_label_ = nullptr;
#endif

  memset(dm_rows_, 0, sizeof(dm_rows_));
  dm_row_count_ = 0;
  memset(stored_dm_, 0, sizeof(stored_dm_));
  stored_dm_head_ = 0;
  stored_dm_count_ = 0;
  dm_active_name_[0] = '\0';
  dm_active_key_[0] = '\0';

  const lv_coord_t help_dialog_w = clampCoord(main_w - dialogInsetW(10, 2), 210, dialogMaxW(300, 340));
  const lv_coord_t help_dialog_h = clampCoord(main_h - 16, 150, 220);

  help_dialog_ = lv_obj_create(root_);
  lv_obj_add_style(help_dialog_, &style_panel_, 0);
  lv_obj_set_size(help_dialog_, help_dialog_w, help_dialog_h);
  lv_obj_align(help_dialog_, LV_ALIGN_CENTER, 0, kModalVerticalNudgeY);
  lv_obj_add_flag(help_dialog_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(help_dialog_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(help_dialog_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_border_width(help_dialog_, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(help_dialog_, kColorBorder, LV_PART_MAIN);
  lv_obj_set_style_border_opa(help_dialog_, LV_OPA_40, LV_PART_MAIN);
  lv_obj_add_event_cb(help_dialog_, onFocusableEvent, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(help_dialog_, onFocusableEvent, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(help_dialog_, onFocusableEvent, LV_EVENT_FOCUSED, this);

  help_title_label_ = lv_label_create(help_dialog_);
  lv_obj_add_style(help_title_label_, &style_text_main_, 0);
  lv_label_set_text(help_title_label_, "Help");
  lv_obj_align(help_title_label_, LV_ALIGN_TOP_LEFT, 4, 2);

  const lv_coord_t help_body_y = 18;
#if defined(DEVICE_HELTEC_V4_EXPANSION)
  const lv_coord_t help_body_h = static_cast<lv_coord_t>(help_dialog_h - 44);
#else
  const lv_coord_t help_body_h = static_cast<lv_coord_t>(help_dialog_h - 24);
#endif

  help_body_panel_ = lv_obj_create(help_dialog_);
  lv_obj_set_pos(help_body_panel_, 2, help_body_y);
  lv_obj_set_size(help_body_panel_, static_cast<lv_coord_t>(help_dialog_w - 4), help_body_h);
  lv_obj_set_style_bg_opa(help_body_panel_, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(help_body_panel_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(help_body_panel_, 0, LV_PART_MAIN);
  lv_obj_set_scroll_dir(help_body_panel_, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(help_body_panel_, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_flag(help_body_panel_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(help_body_panel_, onFocusableEvent, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(help_body_panel_, onFocusableEvent, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(help_body_panel_, onFocusableEvent, LV_EVENT_FOCUSED, this);

  help_body_label_ = lv_label_create(help_body_panel_);
  lv_obj_add_style(help_body_label_, &style_text_dim_, 0);
  lv_coord_t help_body_label_w = static_cast<lv_coord_t>(help_dialog_w - (kPagerWideDialogLayout ? 8 : 12));
  if (help_body_label_w < 40) {
    help_body_label_w = 40;
  }
  lv_obj_set_width(help_body_label_, help_body_label_w);
  lv_label_set_long_mode(help_body_label_, LV_LABEL_LONG_WRAP);
#if defined(DEVICE_CARDPUTER_LORA_HAT)
  lv_obj_set_style_text_font(help_body_label_, compactUiFont(), 0);
#elif defined(LV_FONT_MONTSERRAT_10) && LV_FONT_MONTSERRAT_10
  if (!kPagerWideDialogLayout) {
    lv_obj_set_style_text_font(help_body_label_, &lv_font_montserrat_10, 0);
  }
#endif
  lv_label_set_text(help_body_label_, kHelpBodyText);
  lv_obj_align(help_body_label_, LV_ALIGN_TOP_LEFT, kPagerWideDialogLayout ? 1 : 4, 0);

#if defined(DEVICE_HELTEC_V4_EXPANSION)
  help_close_btn_ = lv_btn_create(help_dialog_);
  lv_obj_set_size(help_close_btn_, LV_PCT(100), 22);
  lv_obj_align(help_close_btn_, LV_ALIGN_BOTTOM_MID, 0, -2);
  lv_obj_add_style(help_close_btn_, &style_button_, 0);
  lv_obj_add_style(help_close_btn_, &style_button_focused_, LV_STATE_FOCUSED);
  lv_obj_add_event_cb(help_close_btn_, onFocusableEvent, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(help_close_btn_, onFocusableEvent, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(help_close_btn_, onFocusableEvent, LV_EVENT_FOCUSED, this);

  help_close_label_ = lv_label_create(help_close_btn_);
  lv_obj_add_style(help_close_label_, &style_text_main_, 0);
  lv_label_set_text(help_close_label_, "CLOSE");
  lv_obj_center(help_close_label_);

  // Keep the footer control fixed over the scrollable body panel.
  lv_obj_move_foreground(help_close_btn_);
#else
  help_close_btn_ = nullptr;
  help_close_label_ = nullptr;
#endif

  const lv_coord_t live_dialog_w = clampCoord(main_w - dialogInsetW(10, 2), 210, dialogMaxW(300, 340));
  const lv_coord_t live_dialog_h = clampCoord(main_h - 16, 150, 220);

  live_dialog_ = lv_obj_create(root_);
  lv_obj_add_style(live_dialog_, &style_panel_, 0);
  lv_obj_set_size(live_dialog_, live_dialog_w, live_dialog_h);
  lv_obj_align(live_dialog_, LV_ALIGN_CENTER, 0, kModalVerticalNudgeY);
  lv_obj_add_flag(live_dialog_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(live_dialog_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(live_dialog_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_border_width(live_dialog_, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(live_dialog_, kColorBorder, LV_PART_MAIN);
  lv_obj_set_style_border_opa(live_dialog_, LV_OPA_40, LV_PART_MAIN);
  lv_obj_add_event_cb(live_dialog_, onFocusableEvent, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(live_dialog_, onFocusableEvent, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(live_dialog_, onFocusableEvent, LV_EVENT_FOCUSED, this);

  live_title_label_ = lv_label_create(live_dialog_);
  lv_obj_add_style(live_title_label_, &style_text_main_, 0);
  lv_label_set_text(live_title_label_, "Live Feed");
  lv_obj_align(live_title_label_, LV_ALIGN_TOP_LEFT, 4, 2);

  live_hint_label_ = lv_label_create(live_dialog_);
  lv_obj_add_style(live_hint_label_, &style_text_dim_, 0);
#if defined(LV_FONT_MONTSERRAT_10) && LV_FONT_MONTSERRAT_10
  lv_obj_set_style_text_font(live_hint_label_, &lv_font_montserrat_10, 0);
#endif
  lv_label_set_text(live_hint_label_, "c: clear  u: util  s: snr/rssi");
#if defined(DEVICE_HELTEC_V4_EXPANSION)
  lv_obj_align(live_hint_label_, LV_ALIGN_BOTTOM_LEFT, 4, -26);
#else
  lv_obj_align(live_hint_label_, LV_ALIGN_BOTTOM_RIGHT, -4, -2);
#endif

  const lv_coord_t live_body_y = 18;
#if defined(DEVICE_HELTEC_V4_EXPANSION)
  const lv_coord_t live_body_h = static_cast<lv_coord_t>(live_dialog_h - 56);
#else
  const lv_coord_t live_body_h = static_cast<lv_coord_t>(live_dialog_h - 36);
#endif

  live_body_panel_ = lv_obj_create(live_dialog_);
  lv_obj_set_pos(live_body_panel_, 2, live_body_y);
  lv_obj_set_size(live_body_panel_, static_cast<lv_coord_t>(live_dialog_w - 4), live_body_h);
  lv_obj_set_style_bg_opa(live_body_panel_, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(live_body_panel_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(live_body_panel_, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_row(live_body_panel_, 1, LV_PART_MAIN);
  lv_obj_set_scroll_dir(live_body_panel_, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(live_body_panel_, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_layout(live_body_panel_, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(live_body_panel_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(live_body_panel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_add_flag(live_body_panel_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(live_body_panel_, onFocusableEvent, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(live_body_panel_, onFocusableEvent, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(live_body_panel_, onFocusableEvent, LV_EVENT_FOCUSED, this);

#if defined(DEVICE_HELTEC_V4_EXPANSION)
  live_close_btn_ = lv_btn_create(live_dialog_);
  lv_obj_set_size(live_close_btn_, LV_PCT(100), 22);
  lv_obj_align(live_close_btn_, LV_ALIGN_BOTTOM_MID, 0, -2);
  lv_obj_add_style(live_close_btn_, &style_button_, 0);
  lv_obj_add_style(live_close_btn_, &style_button_focused_, LV_STATE_FOCUSED);
  lv_obj_add_event_cb(live_close_btn_, onFocusableEvent, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(live_close_btn_, onFocusableEvent, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(live_close_btn_, onFocusableEvent, LV_EVENT_FOCUSED, this);

  live_close_label_ = lv_label_create(live_close_btn_);
  lv_obj_add_style(live_close_label_, &style_text_main_, 0);
  lv_label_set_text(live_close_label_, "CLOSE");
  lv_obj_center(live_close_label_);

  // Keep the footer control fixed over the scrollable body panel.
  lv_obj_move_foreground(live_close_btn_);
#else
  live_close_btn_ = nullptr;
  live_close_label_ = nullptr;
#endif

  live_util_dialog_ = lv_obj_create(root_);
  lv_obj_add_style(live_util_dialog_, &style_panel_, 0);
  lv_obj_set_size(live_util_dialog_, live_dialog_w, live_dialog_h);
  lv_obj_align(live_util_dialog_, LV_ALIGN_CENTER, 0, kModalVerticalNudgeY);
  lv_obj_add_flag(live_util_dialog_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(live_util_dialog_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(live_util_dialog_, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(live_util_dialog_, kColorBorder, LV_PART_MAIN);
  lv_obj_set_style_border_opa(live_util_dialog_, LV_OPA_40, LV_PART_MAIN);
  lv_obj_add_event_cb(live_util_dialog_, onFocusableEvent, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(live_util_dialog_, onFocusableEvent, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(live_util_dialog_, onFocusableEvent, LV_EVENT_FOCUSED, this);

  live_util_title_label_ = lv_label_create(live_util_dialog_);
  lv_obj_add_style(live_util_title_label_, &style_text_main_, 0);
  lv_label_set_text(live_util_title_label_, "Channel Utilization");
  lv_obj_align(live_util_title_label_, LV_ALIGN_TOP_LEFT, 4, 2);

  live_util_units_label_ = lv_label_create(live_util_dialog_);
  lv_obj_add_style(live_util_units_label_, &style_text_dim_, 0);
#if defined(LV_FONT_MONTSERRAT_10) && LV_FONT_MONTSERRAT_10
  lv_obj_set_style_text_font(live_util_units_label_, &lv_font_montserrat_10, 0);
#endif
  lv_label_set_text(live_util_units_label_, "Y: %  X: s");
  lv_obj_align(live_util_units_label_, LV_ALIGN_TOP_RIGHT, -4, 2);

  live_util_chart_ = lv_chart_create(live_util_dialog_);
  lv_obj_set_pos(live_util_chart_, 4, 18);
  lv_obj_set_size(live_util_chart_, static_cast<lv_coord_t>(live_dialog_w - 8),
                  static_cast<lv_coord_t>(live_dialog_h - 42));
  lv_obj_set_style_bg_opa(live_util_chart_, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(live_util_chart_, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(live_util_chart_, kColorBorder, LV_PART_MAIN);
  lv_obj_set_style_line_color(live_util_chart_, kColorBorder, LV_PART_ITEMS);
  lv_chart_set_type(live_util_chart_, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(live_util_chart_, kMetricChartPoints);
  lv_chart_set_div_line_count(live_util_chart_, 4, 6);
  lv_chart_set_range(live_util_chart_, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
  lv_obj_add_event_cb(live_util_chart_, onFocusableEvent, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(live_util_chart_, onFocusableEvent, LV_EVENT_FOCUSED, this);
  live_util_series_ = lv_chart_add_series(live_util_chart_, kColorActive, LV_CHART_AXIS_PRIMARY_Y);

  live_util_stats_label_ = lv_label_create(live_util_dialog_);
  lv_obj_add_style(live_util_stats_label_, &style_text_main_, 0);
  lv_label_set_text(live_util_stats_label_, "0%  (0.0 pkt/s)");
  lv_obj_align(live_util_stats_label_, LV_ALIGN_BOTTOM_LEFT, 4, -2);

  live_util_hint_label_ = lv_label_create(live_util_dialog_);
  lv_obj_add_style(live_util_hint_label_, &style_text_dim_, 0);
#if defined(LV_FONT_MONTSERRAT_10) && LV_FONT_MONTSERRAT_10
  lv_obj_set_style_text_font(live_util_hint_label_, &lv_font_montserrat_10, 0);
#endif
  lv_label_set_text(live_util_hint_label_, "backspace: close chart");
  lv_obj_align(live_util_hint_label_, LV_ALIGN_BOTTOM_RIGHT, -4, -2);

  live_snr_dialog_ = lv_obj_create(root_);
  lv_obj_add_style(live_snr_dialog_, &style_panel_, 0);
  lv_obj_set_size(live_snr_dialog_, live_dialog_w, live_dialog_h);
  lv_obj_align(live_snr_dialog_, LV_ALIGN_CENTER, 0, kModalVerticalNudgeY);
  lv_obj_add_flag(live_snr_dialog_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(live_snr_dialog_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(live_snr_dialog_, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(live_snr_dialog_, kColorBorder, LV_PART_MAIN);
  lv_obj_set_style_border_opa(live_snr_dialog_, LV_OPA_40, LV_PART_MAIN);
  lv_obj_add_event_cb(live_snr_dialog_, onFocusableEvent, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(live_snr_dialog_, onFocusableEvent, LV_EVENT_CLICKED, this);
  lv_obj_add_event_cb(live_snr_dialog_, onFocusableEvent, LV_EVENT_FOCUSED, this);

  live_snr_title_label_ = lv_label_create(live_snr_dialog_);
  lv_obj_add_style(live_snr_title_label_, &style_text_main_, 0);
  lv_label_set_text(live_snr_title_label_, "SNR / RSSI");
  lv_obj_align(live_snr_title_label_, LV_ALIGN_TOP_LEFT, 4, 2);

  live_snr_units_label_ = lv_label_create(live_snr_dialog_);
  lv_obj_add_style(live_snr_units_label_, &style_text_dim_, 0);
#if defined(LV_FONT_MONTSERRAT_10) && LV_FONT_MONTSERRAT_10
  lv_obj_set_style_text_font(live_snr_units_label_, &lv_font_montserrat_10, 0);
#endif
  lv_label_set_text(live_snr_units_label_, "Y: dB / dBm  X: s");
  lv_obj_align(live_snr_units_label_, LV_ALIGN_TOP_RIGHT, -4, 2);

  live_snr_chart_ = lv_chart_create(live_snr_dialog_);
  lv_obj_set_pos(live_snr_chart_, 4, 18);
  lv_obj_set_size(live_snr_chart_, static_cast<lv_coord_t>(live_dialog_w - 8),
                  static_cast<lv_coord_t>(live_dialog_h - 42));
  lv_obj_set_style_bg_opa(live_snr_chart_, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(live_snr_chart_, 1, LV_PART_MAIN);
  lv_obj_set_style_border_color(live_snr_chart_, kColorBorder, LV_PART_MAIN);
  lv_obj_set_style_line_color(live_snr_chart_, kColorBorder, LV_PART_ITEMS);
  lv_chart_set_type(live_snr_chart_, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(live_snr_chart_, kMetricChartPoints);
  lv_chart_set_div_line_count(live_snr_chart_, 4, 6);
  lv_chart_set_range(live_snr_chart_, LV_CHART_AXIS_PRIMARY_Y, -130, 20);
  lv_obj_add_event_cb(live_snr_chart_, onFocusableEvent, LV_EVENT_KEY, this);
  lv_obj_add_event_cb(live_snr_chart_, onFocusableEvent, LV_EVENT_FOCUSED, this);
  live_snr_series_ = lv_chart_add_series(live_snr_chart_, kColorAck, LV_CHART_AXIS_PRIMARY_Y);
  live_rssi_series_ = lv_chart_add_series(live_snr_chart_, kColorErr, LV_CHART_AXIS_PRIMARY_Y);

  live_snr_stats_label_ = lv_label_create(live_snr_dialog_);
  lv_obj_add_style(live_snr_stats_label_, &style_text_main_, 0);
  lv_label_set_text(live_snr_stats_label_, "SNR 0 dB  RSSI -120 dBm");
  lv_obj_align(live_snr_stats_label_, LV_ALIGN_BOTTOM_LEFT, 4, -2);

  live_snr_hint_label_ = lv_label_create(live_snr_dialog_);
  lv_obj_add_style(live_snr_hint_label_, &style_text_dim_, 0);
#if defined(LV_FONT_MONTSERRAT_10) && LV_FONT_MONTSERRAT_10
  lv_obj_set_style_text_font(live_snr_hint_label_, &lv_font_montserrat_10, 0);
#endif
  lv_label_set_text(live_snr_hint_label_, "backspace: close chart");
  lv_obj_align(live_snr_hint_label_, LV_ALIGN_BOTTOM_RIGHT, -4, -2);

  memset(live_rows_, 0, sizeof(live_rows_));
  live_row_count_ = 0;
  memset(util_history_, 0, sizeof(util_history_));
  memset(snr_history_, 0, sizeof(snr_history_));
  memset(rssi_history_, 0, sizeof(rssi_history_));
  util_history_head_ = 0;
  util_history_count_ = 0;
  radio_history_head_ = 0;
  radio_history_count_ = 0;

  advert_popup_ = lv_obj_create(root_);
  lv_obj_add_style(advert_popup_, &style_panel_, 0);
  lv_obj_set_size(advert_popup_, clampCoord(main_w - 20, 150, dialogMaxW(220, 270)), 52);
  lv_obj_center(advert_popup_);
  lv_obj_add_flag(advert_popup_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(advert_popup_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(advert_popup_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_border_width(advert_popup_, 3, LV_PART_MAIN);
  lv_obj_set_style_border_color(advert_popup_, kColorFocus, LV_PART_MAIN);
  lv_obj_set_layout(advert_popup_, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(advert_popup_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(advert_popup_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_event_cb(advert_popup_, onFocusableEvent, LV_EVENT_CLICKED, this);

  advert_popup_label_ = lv_label_create(advert_popup_);
  lv_obj_add_style(advert_popup_label_, &style_text_main_, 0);
  lv_obj_set_width(advert_popup_label_, LV_PCT(100));
  lv_label_set_long_mode(advert_popup_label_, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(advert_popup_label_, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(advert_popup_label_, "");
  lv_obj_center(advert_popup_label_);

  shortcut_strip_ = lv_obj_create(main_panel_);
  lv_obj_set_pos(shortcut_strip_, 0, main_h - shortcut_h - kMainBottomInset);
  lv_obj_set_size(shortcut_strip_, LV_PCT(100), shortcut_h);
  lv_obj_add_style(shortcut_strip_, &style_header_, 0);
  lv_obj_clear_flag(shortcut_strip_, LV_OBJ_FLAG_SCROLLABLE);

    const lv_coord_t sc_side_pad =
  #if defined(DEVICE_CARDPUTER_LORA_HAT)
    0;
  #else
    2;
  #endif
    const lv_coord_t sc_gap =
  #if defined(DEVICE_CARDPUTER_LORA_HAT)
    1;
  #else
    3;
  #endif
    const lv_coord_t sc_btn_h =
  #if defined(DEVICE_CARDPUTER_LORA_HAT)
    shortcut_h;
  #else
    clampCoord(static_cast<lv_coord_t>(shortcut_h - 6), 12, shortcut_h);
  #endif
    lv_obj_update_layout(shortcut_strip_);
    const lv_coord_t sc_content_w = lv_obj_get_content_width(shortcut_strip_);
    const lv_coord_t sc_total_w =
      static_cast<lv_coord_t>((sc_content_w > 0 ? sc_content_w : main_w) - (sc_side_pad * 2));
  const lv_coord_t sc_btn_w =
      static_cast<lv_coord_t>((sc_total_w - ((kShortcutCount - 1) * sc_gap)) / kShortcutCount);
  const lv_coord_t sc_row_w = static_cast<lv_coord_t>(sc_btn_w * kShortcutCount + ((kShortcutCount - 1) * sc_gap));
    const lv_coord_t sc_start_x = static_cast<lv_coord_t>((sc_total_w - sc_row_w) / 2 + sc_side_pad);
  for (uint8_t i = 0; i < kShortcutCount; i++) {
    shortcut_btns_[i] = lv_btn_create(shortcut_strip_);
    lv_obj_set_size(shortcut_btns_[i], sc_btn_w, sc_btn_h);
    lv_obj_align(shortcut_btns_[i], LV_ALIGN_LEFT_MID, sc_start_x + i * (sc_btn_w + sc_gap), 0);
    lv_obj_set_style_radius(shortcut_btns_[i], kShortcutButtonRadius, LV_PART_MAIN);
  #if defined(DEVICE_CARDPUTER_LORA_HAT)
    lv_obj_set_style_bg_opa(shortcut_btns_[i], LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(shortcut_btns_[i], 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(shortcut_btns_[i], 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(shortcut_btns_[i], 0, LV_PART_MAIN);
  #else
    lv_obj_add_style(shortcut_btns_[i], &style_button_, 0);
    lv_obj_add_style(shortcut_btns_[i], &style_button_focused_, LV_STATE_FOCUSED);
  #endif
    lv_obj_add_event_cb(shortcut_btns_[i], onFocusableEvent, LV_EVENT_KEY, this);
    lv_obj_add_event_cb(shortcut_btns_[i], onFocusableEvent, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(shortcut_btns_[i], onFocusableEvent, LV_EVENT_FOCUSED, this);

    shortcut_labels_[i] = lv_label_create(shortcut_btns_[i]);
    lv_obj_add_style(shortcut_labels_[i], &style_text_dim_, 0);
    lv_obj_set_style_text_font(shortcut_labels_[i], compactUiFont(), 0);
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
  for (uint8_t i = 0; i < kChannelCount; i++) {
    lv_group_add_obj(key_group_, channel_dropdown_rows_[i]);
  }
  lv_group_add_obj(key_group_, chat_panel_);
  if (chat_advz_btn_) {
    lv_group_add_obj(key_group_, chat_advz_btn_);
  }
  if (chat_advf_btn_) {
    lv_group_add_obj(key_group_, chat_advf_btn_);
  }
  if (chat_new_btn_) {
    lv_group_add_obj(key_group_, chat_new_btn_);
  }
  if (compose_input_ && !kUseOnscreenKeyboard) {
    lv_group_add_obj(key_group_, compose_input_);
  }
  for (uint8_t i = 0; i < kCfgRowCount; i++) {
    if (cfg_rows_[i]) {
      lv_group_add_obj(key_group_, cfg_rows_[i]);
    }
  }
  if (cfg_close_btn_) {
    lv_group_add_obj(key_group_, cfg_close_btn_);
  }
  for (uint8_t i = 0; i < kShortcutCount; i++) {
    lv_group_add_obj(key_group_, shortcut_btns_[i]);
  }
  for (uint8_t i = 0; i < kContactActionCount; i++) {
    if (contacts_action_rows_[i]) {
      lv_group_add_obj(key_group_, contacts_action_rows_[i]);
    }
  }
  if (dm_new_btn_) {
    lv_group_add_obj(key_group_, dm_new_btn_);
  }
  if (dm_panel_) {
    lv_group_add_obj(key_group_, dm_panel_);
  }
  if (dm_close_btn_) {
    lv_group_add_obj(key_group_, dm_close_btn_);
  }
  if (help_dialog_) {
    lv_group_add_obj(key_group_, help_dialog_);
  }
  if (help_body_panel_) {
    lv_group_add_obj(key_group_, help_body_panel_);
  }
  if (help_close_btn_) {
    lv_group_add_obj(key_group_, help_close_btn_);
  }
  if (live_dialog_) {
    lv_group_add_obj(key_group_, live_dialog_);
  }
  if (live_body_panel_) {
    lv_group_add_obj(key_group_, live_body_panel_);
  }
  if (live_close_btn_) {
    lv_group_add_obj(key_group_, live_close_btn_);
  }
  if (live_util_dialog_) {
    lv_group_add_obj(key_group_, live_util_dialog_);
  }
  if (live_util_chart_) {
    lv_group_add_obj(key_group_, live_util_chart_);
  }
  if (live_snr_dialog_) {
    lv_group_add_obj(key_group_, live_snr_dialog_);
  }
  if (live_snr_chart_) {
    lv_group_add_obj(key_group_, live_snr_chart_);
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
  if (false) Serial.println("[KEYUI] debug=1 ui begin");
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
  loadDmHistoryFromFs();
  bindInputGroup();
  active_channel_ = selected_channel_;
  rebuildChatForActiveChannel();
  last_chat_persist_ms_ = millis();
  last_dm_persist_ms_ = last_chat_persist_ms_;
  last_dm_retention_prune_ms_ = last_chat_persist_ms_;

  started_ = true;
  if (first_install_identity_prompt_) {
    first_install_identity_prompt_ = false;
    openIdentityNamePrompt();
  }
  if (splash_overlay_) {
    lv_obj_move_foreground(splash_overlay_);
  }
  return true;
}

namespace {

constexpr uint32_t kSplashBgTop = 0x2C1B47;       // deep violet
constexpr uint32_t kSplashBgBottom = 0x6B2E5C;    // dusk magenta
constexpr uint32_t kSplashCard = 0x1A0F2B;        // near-black violet
constexpr uint32_t kSplashPetal = 0xFFF5E6;       // creamy white
constexpr uint32_t kSplashPetalShade = 0xF6D9B8;  // warm cream shade
constexpr uint32_t kSplashCenter = 0xFFC845;      // plumeria yellow center
constexpr uint32_t kSplashCenterRim = 0xE38B2E;   // amber ring
constexpr uint32_t kSplashAccent = 0xF7B7C9;      // soft pink hint
constexpr uint32_t kSplashTitle = 0xFFF7EE;
constexpr uint32_t kSplashSubtitle = 0xE7BFD1;

inline lv_obj_t* makeCircle(lv_obj_t* parent, lv_coord_t diameter, uint32_t hex,
                            lv_coord_t x_offset, lv_coord_t y_offset, lv_opa_t opa = LV_OPA_COVER) {
  lv_obj_t* dot = lv_obj_create(parent);
  lv_obj_remove_style_all(dot);
  lv_obj_set_size(dot, diameter, diameter);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(dot, lv_color_hex(hex), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(dot, opa, LV_PART_MAIN);
  lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
  lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(dot, LV_ALIGN_CENTER, x_offset, y_offset);
  return dot;
}

inline void drawPlumeriaFlower(lv_obj_t* parent, lv_coord_t cx, lv_coord_t cy,
                               lv_coord_t flower_r, bool detailed) {
  // Plumeria has 5 broad, rounded petals overlapping a yellow center.
  // Detailed flowers also get shading rings and pink accents; corner
  // flowers skip those rings to keep LVGL widget allocations bounded.
  const float kPi = 3.14159265358979323846f;
  const lv_coord_t petal_d = static_cast<lv_coord_t>(flower_r * 1.05f);
  const lv_coord_t petal_offset = static_cast<lv_coord_t>(flower_r * 0.78f);
  const lv_coord_t center_d = static_cast<lv_coord_t>(flower_r * 0.78f);

  for (int i = 0; i < 5; i++) {
    const float angle = -kPi / 2.0f + (static_cast<float>(i) * (2.0f * kPi / 5.0f));
    const lv_coord_t px = cx + static_cast<lv_coord_t>(cosf(angle) * petal_offset);
    const lv_coord_t py = cy + static_cast<lv_coord_t>(sinf(angle) * petal_offset);
    makeCircle(parent, petal_d, kSplashPetal, px, py);
  }

  if (detailed) {
    const lv_coord_t shade_d = static_cast<lv_coord_t>(flower_r * 0.55f);
    const lv_coord_t shade_offset = static_cast<lv_coord_t>(flower_r * 0.62f);
    const lv_coord_t accent_offset = static_cast<lv_coord_t>(flower_r * 0.35f);
    const lv_coord_t accent_d = static_cast<lv_coord_t>(flower_r * 0.32f);
    const lv_coord_t center_rim_d = static_cast<lv_coord_t>(flower_r * 0.90f);

    for (int i = 0; i < 5; i++) {
      const float angle = -kPi / 2.0f + (static_cast<float>(i) * (2.0f * kPi / 5.0f));
      const lv_coord_t sx = cx + static_cast<lv_coord_t>(cosf(angle) * shade_offset);
      const lv_coord_t sy = cy + static_cast<lv_coord_t>(sinf(angle) * shade_offset);
      makeCircle(parent, shade_d, kSplashPetalShade, sx, sy, LV_OPA_60);
    }
    for (int i = 0; i < 5; i++) {
      const float angle = -kPi / 2.0f + kPi / 5.0f +
                          (static_cast<float>(i) * (2.0f * kPi / 5.0f));
      const lv_coord_t ax = cx + static_cast<lv_coord_t>(cosf(angle) * accent_offset);
      const lv_coord_t ay = cy + static_cast<lv_coord_t>(sinf(angle) * accent_offset);
      makeCircle(parent, accent_d, kSplashAccent, ax, ay, LV_OPA_50);
    }
    // Amber rim behind the yellow disc.
    makeCircle(parent, center_rim_d, kSplashCenterRim, cx, cy);
  }
  // Yellow center disc.
  makeCircle(parent, center_d, kSplashCenter, cx, cy);
}

}  // namespace

void StandaloneUi::showSplash(uint32_t duration_ms) {
  showSplash(nullptr, duration_ms);
}

void StandaloneUi::showSplash(const char* node_name, uint32_t duration_ms) {
  if (splash_overlay_) {
    return;
  }

  const lv_coord_t w = lv_disp_get_hor_res(NULL);
  const lv_coord_t h = lv_disp_get_ver_res(NULL);
  const lv_coord_t shortest = w < h ? w : h;
  const bool tiny = shortest < 160;

  lv_obj_t* parent = lv_scr_act();
  if (!parent) {
    return;
  }

  lv_obj_t* overlay = lv_obj_create(parent);
  lv_obj_remove_style_all(overlay);
  lv_obj_set_size(overlay, w, h);
  lv_obj_set_pos(overlay, 0, 0);
  lv_obj_set_style_bg_color(overlay, lv_color_hex(kSplashBgTop), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(overlay, lv_color_hex(kSplashBgBottom), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(overlay, LV_GRAD_DIR_VER, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(overlay, 0, LV_PART_MAIN);
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_CLICKABLE);

  // Card behind the flower, scaled to display.
  const lv_coord_t card_w = static_cast<lv_coord_t>(w - (tiny ? 8 : 24));
  const lv_coord_t card_h = static_cast<lv_coord_t>(h - (tiny ? 8 : 28));
  lv_obj_t* card = lv_obj_create(overlay);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_center(card);
  lv_obj_set_style_radius(card, tiny ? 6 : 14, LV_PART_MAIN);
  lv_obj_set_style_bg_color(card, lv_color_hex(kSplashCard), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_70, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(kSplashAccent), LV_PART_MAIN);
  lv_obj_set_style_border_opa(card, LV_OPA_40, LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  // Flower geometry. `drawPlumeriaFlower` places each petal via
  // LV_ALIGN_CENTER offsets, so cx/cy here are offsets from the card center.
  // One large flower sits slightly above the card center, with smaller
  // flowers tucked into each of the four corners. On tiny screens (e.g.
  // Cardputer 240x135 without PSRAM) corner flowers are skipped to leave
  // enough room in LVGL's default 48 KB pool for the main UI.
  const lv_coord_t main_flower_r = static_cast<lv_coord_t>(shortest * (tiny ? 0.18f : 0.17f));
  const lv_coord_t corner_flower_r =
      static_cast<lv_coord_t>(main_flower_r * 0.50f);
  const lv_coord_t main_cy = static_cast<lv_coord_t>(-card_h * (tiny ? 0.08f : 0.10f));

  if (!tiny) {
    // Approximate full radius of a flower (petal_offset + petal_r) so corners
    // sit fully inside the card.
    const lv_coord_t corner_extent = static_cast<lv_coord_t>(corner_flower_r * 1.4f);
    const lv_coord_t corner_margin = 6;
    const lv_coord_t corner_x =
        static_cast<lv_coord_t>(card_w / 2 - corner_extent - corner_margin);
    const lv_coord_t corner_y =
        static_cast<lv_coord_t>(card_h / 2 - corner_extent - corner_margin);

    drawPlumeriaFlower(card, -corner_x, -corner_y, corner_flower_r, false);
    drawPlumeriaFlower(card, corner_x, -corner_y, corner_flower_r, false);
    drawPlumeriaFlower(card, -corner_x, corner_y, corner_flower_r, false);
    drawPlumeriaFlower(card, corner_x, corner_y, corner_flower_r, false);
  }
  drawPlumeriaFlower(card, 0, main_cy, main_flower_r, !tiny);

  // Title + version.
  lv_obj_t* title = lv_label_create(card);
  lv_label_set_text(title, "PLUMERIA-MC");
  lv_obj_set_style_text_color(title, lv_color_hex(kSplashTitle), LV_PART_MAIN);
  lv_obj_set_style_text_letter_space(title, 2, LV_PART_MAIN);
  const lv_font_t* title_font = nullptr;
#if defined(LV_FONT_MONTSERRAT_18) && LV_FONT_MONTSERRAT_18
  if (!tiny) {
    title_font = &lv_font_montserrat_18;
  }
#endif
#if defined(LV_FONT_MONTSERRAT_16) && LV_FONT_MONTSERRAT_16
  if (!title_font && !tiny) {
    title_font = &lv_font_montserrat_16;
  }
#endif
  if (!title_font) {
    title_font = &lv_font_montserrat_14;
  }
  lv_obj_set_style_text_font(title, title_font, LV_PART_MAIN);
  lv_obj_align(title, LV_ALIGN_CENTER, 0,
               static_cast<lv_coord_t>(main_cy + main_flower_r + (tiny ? 14 : 22)));

#ifdef APP_VERSION
  const char* version_str = APP_VERSION;
#else
  const char* version_str = "dev";
#endif
  lv_obj_t* subtitle = lv_label_create(card);
  lv_label_set_text(subtitle, version_str);
  lv_obj_set_style_text_color(subtitle, lv_color_hex(kSplashSubtitle), LV_PART_MAIN);
  const lv_font_t* subtitle_font = &lv_font_montserrat_14;
#if defined(LV_FONT_MONTSERRAT_12) && LV_FONT_MONTSERRAT_12
  subtitle_font = &lv_font_montserrat_12;
#elif defined(LV_FONT_MONTSERRAT_10) && LV_FONT_MONTSERRAT_10
  subtitle_font = &lv_font_montserrat_10;
#endif
  lv_obj_set_style_text_font(subtitle, subtitle_font, LV_PART_MAIN);
  lv_obj_align_to(subtitle, title, LV_ALIGN_OUT_BOTTOM_MID, 0, tiny ? 2 : 6);

  // Node name (if available) in the same font/size as the version, pinned
  // toward the bottom of the card.
  if (node_name && node_name[0] != '\0') {
    lv_obj_t* node_label = lv_label_create(card);
    lv_label_set_long_mode(node_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(node_label, static_cast<lv_coord_t>(card_w - (tiny ? 12 : 24)));
    lv_obj_set_style_text_color(node_label, lv_color_hex(kSplashSubtitle), LV_PART_MAIN);
    lv_obj_set_style_text_font(node_label, subtitle_font, LV_PART_MAIN);
    lv_obj_set_style_text_align(node_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(node_label, node_name);
    lv_obj_align(node_label, LV_ALIGN_BOTTOM_MID, 0, -(tiny ? 4 : 10));
  }

  splash_overlay_ = overlay;
  splash_duration_ms_ = duration_ms;
  splash_dismiss_ms_ = 0;
  lv_obj_move_foreground(overlay);

  // Render once so the splash is visible during the rest of setup() (mesh
  // radio bring-up, web/AP init), instead of only appearing once loop()
  // starts. The dismissal countdown is started in loop() on the first
  // iteration where the splash is observed, so the user always gets the full
  // duration regardless of how long setup() takes after this point.
  lv_timer_handler();
}

void StandaloneUi::dismissSplash() {
  if (!splash_overlay_) {
    return;
  }
  lv_obj_t* overlay = splash_overlay_;
  splash_overlay_ = nullptr;
  splash_duration_ms_ = 0;
  splash_dismiss_ms_ = 0;
  lv_obj_del(overlay);
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
  if (identity_prompt_open_) {
    snprintf(title, sizeof(title), "Identity Name: ");
  } else if (compose_dm_mode_) {
    snprintf(title, sizeof(title), "DM to %s", compose_target_channel_[0] ? compose_target_channel_ : "-");
  } else {
    snprintf(title, sizeof(title), "Send to %s", compose_target_channel_[0] ? compose_target_channel_ : "-");
  }
  lv_label_set_text(compose_title_label_, title);

  if (compose_send_label_) {
    lv_label_set_text(compose_send_label_, identity_prompt_open_ ? "Save" : "Send");
  }

  if (compose_hint_label_) {
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_label_set_text(compose_hint_label_, "");
    lv_obj_add_flag(compose_hint_label_, LV_OBJ_FLAG_HIDDEN);
#else
    if (kUseOnscreenKeyboard) {
      lv_label_set_text(compose_hint_label_, identity_prompt_open_ ? "Tap OK to save name." : "Tap OK to send.");
    } else {
      lv_label_set_text(compose_hint_label_, identity_prompt_open_ ? "Press enter to commit identity name." : "");
    }
#endif
  }
}

void StandaloneUi::showComposeKeyboard() {
#if defined(LV_USE_KEYBOARD) && LV_USE_KEYBOARD
  if (!kUseOnscreenKeyboard || !compose_input_ || !compose_dialog_) {
    return;
  }

  CTS_TRACE("showComposeKeyboard begin");
  if (!compose_keyboard_ || !lv_obj_is_valid(compose_keyboard_)) {
    CTS_TRACE("showComposeKeyboard missing");
    return;
  }

  const lv_coord_t screen_h = lv_disp_get_ver_res(nullptr);
#if defined(DEVICE_HELTEC_V4_EXPANSION)
  lv_obj_set_width(compose_keyboard_, LV_PCT(100));
  lv_obj_set_flex_grow(compose_keyboard_, 1);
#else
  const lv_coord_t kb_h = clampCoord(static_cast<lv_coord_t>(screen_h / 2), 74, 118);
  lv_obj_set_size(compose_keyboard_, LV_PCT(100), kb_h);
  lv_obj_align(compose_keyboard_, LV_ALIGN_BOTTOM_MID, 0, 0);
#endif
  lv_obj_clear_flag(compose_keyboard_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(compose_keyboard_, LV_OBJ_FLAG_CLICKABLE);
  CTS_TRACE("showComposeKeyboard done hidden=%d", lv_obj_has_flag(compose_keyboard_, LV_OBJ_FLAG_HIDDEN) ? 1 : 0);
#endif
}

void StandaloneUi::hideComposeKeyboard() {
#if defined(LV_USE_KEYBOARD) && LV_USE_KEYBOARD
  if (!compose_keyboard_) {
    return;
  }
  if (lv_obj_is_valid(compose_keyboard_)) {
    lv_obj_add_flag(compose_keyboard_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(compose_keyboard_, LV_OBJ_FLAG_CLICKABLE);
  }
#endif
}

void StandaloneUi::refreshChannelVisuals() {
  refreshSelectorVisuals();
  refreshDropdownVisuals();
}

void StandaloneUi::refreshSelectorVisuals() {
  if (!channel_selector_btn_ || !channel_selector_label_ || !channel_selector_caret_) {
    return;
  }

  const bool contacts_mode = contacts_open_;
  const char* active_name = "-";
  size_t selector_char_cap = 1;

  if (contacts_mode) {
    if (contacts_count_ > 0) {
      const uint8_t selected = contacts_selected_index_ < contacts_count_ ? contacts_selected_index_ : 0;
      active_name = contacts_cache_[selected].name;
      selector_char_cap = channelDisplayLenForDropdown(active_name);
      if (channel_dropdown_open_) {
        selector_char_cap = 1;
        for (uint8_t i = 0; i < contacts_count_; i++) {
          const size_t display_len = channelDisplayLenForDropdown(contacts_cache_[i].name);
          if (display_len > selector_char_cap) {
            selector_char_cap = display_len;
          }
        }
      }
    }
  } else {
    active_name = (configured_channel_count_ > 0) ? configured_channel_names_[active_channel_] : "-";
    selector_char_cap = channelDisplayLenForDropdown(active_name);
    if (channel_dropdown_open_) {
      selector_char_cap = 1;
      for (uint8_t i = 0; i < configured_channel_count_; i++) {
        const size_t display_len = channelDisplayLenForDropdown(configured_channel_names_[i]);
        if (display_len > selector_char_cap) {
          selector_char_cap = display_len;
        }
      }
    }
  }

  char selector_text[48] = {};
  formatChannelLabelForSelector(active_name, selector_char_cap, selector_text, sizeof(selector_text));
  lv_label_set_text(channel_selector_label_, selector_text);
  lv_label_set_text(channel_selector_caret_, "");

  lv_coord_t selector_max_w = kSelectorMaxW;
  if (contacts_mode && header_bar_) {
    const lv_coord_t header_w = lv_obj_get_width(header_bar_);
    const lv_coord_t contacts_cap_w = static_cast<lv_coord_t>((header_w * 3) / 4);
    if (contacts_cap_w > kSelectorMinW) {
      selector_max_w = contacts_cap_w;
    }
  }

  const size_t max_chars_for_width =
      selector_max_w > 16 ? static_cast<size_t>((selector_max_w - 16) / 7) : static_cast<size_t>(1);
  if (selector_char_cap > max_chars_for_width) {
    selector_char_cap = max_chars_for_width;
    formatChannelLabelForSelector(active_name, selector_char_cap, selector_text, sizeof(selector_text));
    lv_label_set_text(channel_selector_label_, selector_text);
  }

  const lv_coord_t selector_w = clampCoord(static_cast<lv_coord_t>(selector_char_cap * 7 + 18),
                                           kSelectorMinW, selector_max_w);
  const lv_coord_t dropdown_min_w =
      static_cast<lv_coord_t>((kDropdownNameMaxChars + 3) * 7 + 16);
  lv_obj_set_width(channel_selector_btn_, selector_w);
  lv_obj_align(channel_selector_btn_, LV_ALIGN_LEFT_MID, 2, 0);
  lv_obj_set_width(channel_dropdown_panel_, clampCoord(dropdown_min_w, selector_w, selector_max_w) + 12);

  bool has_unread_channel = false;
  for (uint8_t i = 0; i < configured_channel_count_; i++) {
    if (unread_channels_[i]) {
      has_unread_channel = true;
      break;
    }
  }

  lv_obj_remove_style(channel_selector_btn_, &style_button_active_, 0);
  lv_obj_remove_style(channel_selector_btn_, &style_unread_edge_, 0);
  if (channel_dropdown_open_) {
    lv_obj_add_style(channel_selector_btn_, &style_button_active_, 0);
  } else if (!contacts_mode && has_unread_channel) {
    lv_obj_add_style(channel_selector_btn_, &style_unread_edge_, 0);
  }
}

void StandaloneUi::refreshDropdownVisuals() {
  if (!channel_dropdown_panel_) {
    return;
  }

  const bool contacts_mode = contacts_open_;
  const uint8_t contacts_option_count = clampOptionCount(contacts_count_, kChannelCount);
  const uint8_t option_count = contacts_mode ? contacts_option_count : configured_channel_count_;
  const uint8_t visible_rows =
      option_count == 0 ? 1
                        : (option_count > kDropdownVisibleRows ? kDropdownVisibleRows : option_count);
  const bool needs_scroll = option_count > kDropdownVisibleRows;
  const lv_coord_t panel_pad_top = lv_obj_get_style_pad_top(channel_dropdown_panel_, LV_PART_MAIN);
  const lv_coord_t panel_pad_bottom = lv_obj_get_style_pad_bottom(channel_dropdown_panel_, LV_PART_MAIN);
  const lv_coord_t panel_border = lv_obj_get_style_border_width(channel_dropdown_panel_, LV_PART_MAIN);
  const lv_coord_t list_pad_top = lv_obj_get_style_pad_top(channel_dropdown_list_, LV_PART_MAIN);
  const lv_coord_t list_pad_bottom = lv_obj_get_style_pad_bottom(channel_dropdown_list_, LV_PART_MAIN);
  const lv_coord_t list_row_gap = lv_obj_get_style_pad_row(channel_dropdown_list_, LV_PART_MAIN);
  const lv_coord_t list_border = lv_obj_get_style_border_width(channel_dropdown_list_, LV_PART_MAIN);
  const lv_coord_t total_row_gaps =
      static_cast<lv_coord_t>((visible_rows > 1 ? (visible_rows - 1) : 0) * list_row_gap);
  const lv_coord_t panel_h =
      static_cast<lv_coord_t>(visible_rows * kDropdownRowH + panel_pad_top + panel_pad_bottom +
                              (panel_border * 2) + list_pad_top + list_pad_bottom + total_row_gaps +
                              (list_border * 2) +
                  kDropdownHeightSafetyPad);
  lv_obj_set_height(channel_dropdown_panel_,
                    panel_h);
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
    if (i >= option_count) {
      lv_obj_add_flag(channel_dropdown_rows_[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    lv_obj_clear_flag(channel_dropdown_rows_[i], LV_OBJ_FLAG_HIDDEN);

    char display_name[48] = {};
    char row_text[48];
    if (contacts_mode) {
      const mesh::MeshContactSummary& contact = contacts_cache_[i];
      formatChannelLabelForDropdown(contact.name, display_name, sizeof(display_name));
      const bool unread_contact =
          has_unread_dm_ &&
          ((last_dm_sender_key_[0] != '\0' && contact.public_key_hex[0] != '\0' &&
            strcmp(last_dm_sender_key_, contact.public_key_hex) == 0) ||
           dmNameLikelyMatch(last_dm_sender_name_, contact.name));
      snprintf(row_text, sizeof(row_text), "%s%s%s", contact.favorite ? "* " : "", display_name,
               unread_contact ? " !" : "");
    } else {
      formatChannelLabelForDropdown(configured_channel_names_[i], display_name, sizeof(display_name));
      snprintf(row_text, sizeof(row_text), "%s%s", display_name, unread_channels_[i] ? " !" : "");
    }
    lv_label_set_text(channel_dropdown_labels_[i], row_text);

    lv_obj_remove_style(channel_dropdown_rows_[i], &style_dropdown_active_, 0);
    lv_obj_remove_style(channel_dropdown_rows_[i], &style_dropdown_highlight_, 0);

    const bool active_row = contacts_mode ? (i == contacts_selected_index_) : (i == active_channel_);
    if (active_row) {
      lv_obj_add_style(channel_dropdown_rows_[i], &style_dropdown_active_, 0);
    }
    if (!contacts_mode && channel_dropdown_open_ && unread_channels_[i]) {
      lv_obj_add_style(channel_dropdown_rows_[i], &style_dropdown_highlight_, 0);
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
  const uint8_t contacts_option_count = clampOptionCount(contacts_count_, kChannelCount);
  const uint8_t option_count = contacts_open_ ? contacts_option_count : configured_channel_count_;
  if (channel_dropdown_open_ || option_count == 0) {
    return;
  }

  channel_dropdown_open_ = true;
  last_dropdown_open_ms_ = millis();
  if (contacts_open_) {
    dropdown_highlight_channel_ =
        contacts_selected_index_ < option_count ? contacts_selected_index_ : static_cast<uint8_t>(option_count - 1);
  } else {
    dropdown_highlight_channel_ =
        selected_channel_ < option_count ? selected_channel_ : static_cast<uint8_t>(option_count - 1);
  }
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
    dropdown_highlight_channel_ = contacts_open_ ? contacts_selected_index_ : selected_channel_;
  }
  refreshChannelVisuals();
}

void StandaloneUi::moveDropdownHighlight(int delta) {
  const uint8_t contacts_option_count = clampOptionCount(contacts_count_, kChannelCount);
  const uint8_t option_count = contacts_open_ ? contacts_option_count : configured_channel_count_;
  if (option_count == 0) {
    return;
  }

  int next = static_cast<int>(dropdown_highlight_channel_) + delta;
  if (next < 0) {
    next = option_count - 1;
  } else if (next >= option_count) {
    next = 0;
  }

  dropdown_highlight_channel_ = static_cast<uint8_t>(next);
  refreshDropdownVisuals();
}

void StandaloneUi::openComposeDialog() {
  CTS_TRACE("openComposeDialog enter open=%d dm_mode=%d return_dm=%d", compose_open_ ? 1 : 0,
            compose_dm_mode_ ? 1 : 0, compose_return_to_dm_ ? 1 : 0);
  if (compose_open_ || !compose_dialog_ || !compose_input_ ||
      (!compose_dm_mode_ && (configured_channel_count_ == 0 || active_channel_ >= configured_channel_count_))) {
    CTS_TRACE("openComposeDialog blocked dialog=%p input=%p", static_cast<void*>(compose_dialog_),
              static_cast<void*>(compose_input_));
    return;
  }

  if (!compose_dm_mode_) {
    strncpy(compose_target_channel_, configured_channel_names_[active_channel_], sizeof(compose_target_channel_) - 1);
    compose_target_channel_[sizeof(compose_target_channel_) - 1] = '\0';
  }

  // Restore regular multi-line compose geometry when not in first-install prompt mode.
  if (kUseOnscreenKeyboard) {
    const lv_coord_t screen_w = lv_disp_get_hor_res(nullptr);
    const lv_coord_t screen_h = lv_disp_get_ver_res(nullptr);
    lv_obj_set_size(compose_dialog_, clampCoord(static_cast<lv_coord_t>(screen_w - 8), 180, screen_w),
                    clampCoord(static_cast<lv_coord_t>(screen_h - 8), 120, screen_h));
    lv_obj_align(compose_dialog_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_width(compose_input_, LV_PCT(100));
    lv_obj_set_height(compose_input_, 38);
    lv_textarea_set_one_line(compose_input_, true);
  } else {
    lv_obj_set_size(compose_dialog_, lv_obj_get_width(compose_dialog_), kComposeDialogH);
    lv_obj_set_size(compose_input_, LV_PCT(composeInputWidthPct()), kComposeInputH);
    lv_obj_align(compose_input_, LV_ALIGN_BOTTOM_MID, 0, composeInputBottomInset());
    lv_textarea_set_one_line(compose_input_, false);
    lv_obj_center(compose_dialog_);
  }
  lv_textarea_set_max_length(compose_input_, static_cast<uint16_t>(kComposeMessageMaxChars));

  lv_textarea_set_text(compose_input_, "");
  lv_textarea_set_placeholder_text(compose_input_, "Type and press Enter");
  refreshComposeDialog();

  compose_open_ = true;
  compose_opened_ms_ = millis();
  if (dm_open_) {
    if (dm_dialog_) {
      lv_obj_clear_flag(dm_dialog_, LV_OBJ_FLAG_CLICKABLE);
      if (compose_return_to_dm_ && !kUseOnscreenKeyboard) {
        lv_obj_add_flag(dm_dialog_, LV_OBJ_FLAG_HIDDEN);
      }
    }
    if (dm_panel_) {
      lv_obj_clear_flag(dm_panel_, LV_OBJ_FLAG_CLICKABLE);
    }
    if (dm_close_btn_) {
      lv_obj_clear_flag(dm_close_btn_, LV_OBJ_FLAG_CLICKABLE);
    }
    if (dm_new_btn_) {
      lv_obj_clear_flag(dm_new_btn_, LV_OBJ_FLAG_CLICKABLE);
    }
  }
  lv_obj_clear_flag(compose_dialog_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(compose_dialog_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_move_foreground(compose_dialog_);
  if (!pending_compose_post_open_) {
    pending_compose_post_open_ = true;
    lv_async_call(onComposePostOpenAsync, this);
  }
  CTS_TRACE("openComposeDialog done dm_mode=%d", compose_dm_mode_ ? 1 : 0);
}

void StandaloneUi::openIdentityNamePrompt() {
  if (!compose_dialog_ || !compose_input_) {
    return;
  }

  identity_prompt_open_ = true;
  compose_dm_mode_ = false;
  compose_target_channel_[0] = '\0';
  compose_target_dm_pubkey_[0] = '\0';

  // Single-line first-install prompt with compact dialog height.
  if (kUseOnscreenKeyboard) {
    const lv_coord_t screen_w = lv_disp_get_hor_res(nullptr);
    const lv_coord_t screen_h = lv_disp_get_ver_res(nullptr);
    lv_obj_set_size(compose_dialog_, clampCoord(static_cast<lv_coord_t>(screen_w - 8), 180, screen_w),
                    clampCoord(static_cast<lv_coord_t>(screen_h - 8), 120, screen_h));
    lv_obj_align(compose_dialog_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_width(compose_input_, LV_PCT(100));
    lv_obj_set_height(compose_input_, 38);
  } else {
    lv_obj_set_size(compose_dialog_, lv_obj_get_width(compose_dialog_), kComposeDialogSingleLineH);
    lv_obj_set_size(compose_input_, LV_PCT(composeInputWidthPct()), kComposeInputSingleLineH);
    lv_obj_align(compose_input_, LV_ALIGN_BOTTOM_MID, 0, composeInputBottomInset());
    lv_obj_center(compose_dialog_);
  }
  lv_textarea_set_one_line(compose_input_, true);

  lv_textarea_set_text(compose_input_, "");
  lv_textarea_set_max_length(compose_input_, 31);
  lv_textarea_set_placeholder_text(compose_input_, "Enter identity name");
  refreshComposeDialog();

  compose_open_ = true;
  compose_opened_ms_ = millis();
  if (dm_open_) {
    if (dm_dialog_) {
      lv_obj_clear_flag(dm_dialog_, LV_OBJ_FLAG_CLICKABLE);
      if (compose_return_to_dm_ && !kUseOnscreenKeyboard) {
        lv_obj_add_flag(dm_dialog_, LV_OBJ_FLAG_HIDDEN);
      }
    }
    if (dm_panel_) {
      lv_obj_clear_flag(dm_panel_, LV_OBJ_FLAG_CLICKABLE);
    }
    if (dm_close_btn_) {
      lv_obj_clear_flag(dm_close_btn_, LV_OBJ_FLAG_CLICKABLE);
    }
    if (dm_new_btn_) {
      lv_obj_clear_flag(dm_new_btn_, LV_OBJ_FLAG_CLICKABLE);
    }
  }
  lv_obj_clear_flag(compose_dialog_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(compose_dialog_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(compose_input_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(compose_input_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_move_foreground(compose_dialog_);
  if (!pending_compose_post_open_) {
    pending_compose_post_open_ = true;
    lv_async_call(onComposePostOpenAsync, this);
  }
}

bool StandaloneUi::applyIdentityNameFromPrompt() {
  if (!compose_input_) {
    return false;
  }

  const char* raw = lv_textarea_get_text(compose_input_);
  if (!raw) {
    return false;
  }

  String name(raw);
  name.trim();
  if (name.length() == 0 || name.length() > 31) {
    appendChatLine("[ERR] Identity name must be 1-31 chars", ChatLineKind::Error);
    return false;
  }

  char err[96] = {};
  if (!plumeria::web::setNodeName(name.c_str(), err, sizeof(err))) {
    appendChatLine(err[0] ? err : "[ERR] Failed to set identity name", ChatLineKind::Error);
    return false;
  }

  if (first_install_auto_export_pending_) {
    first_install_auto_export_pending_ = false;
    exportConfigToSd();
  }

  appendChatLine("[OK] Identity name saved", ChatLineKind::Ack);
  return true;
}

void StandaloneUi::closeComposeDialog(bool restore_chat_focus) {
  if (!compose_open_ || !compose_dialog_ || !compose_input_) {
    return;
  }

  compose_open_ = false;
  compose_opened_ms_ = 0;
  pending_compose_post_open_ = false;
  lv_obj_add_flag(compose_dialog_, LV_OBJ_FLAG_HIDDEN);
  hideComposeKeyboard();
  if (dm_open_) {
    if (dm_dialog_) {
      if (compose_return_to_dm_) {
        lv_obj_clear_flag(dm_dialog_, LV_OBJ_FLAG_HIDDEN);
      }
      lv_obj_add_flag(dm_dialog_, LV_OBJ_FLAG_CLICKABLE);
    }
    if (dm_panel_) {
      lv_obj_add_flag(dm_panel_, LV_OBJ_FLAG_CLICKABLE);
    }
    if (dm_close_btn_) {
      lv_obj_add_flag(dm_close_btn_, LV_OBJ_FLAG_CLICKABLE);
    }
    if (dm_new_btn_) {
      lv_obj_add_flag(dm_new_btn_, LV_OBJ_FLAG_CLICKABLE);
    }
  }
  lv_textarea_set_text(compose_input_, "");
  compose_dm_mode_ = false;
  compose_target_dm_pubkey_[0] = '\0';
  resetPointerInputState();

  if (restore_chat_focus && compose_return_to_dm_ && dm_open_ && dm_panel_) {
    compose_return_to_dm_ = false;
    lv_obj_move_foreground(dm_dialog_);
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    if (dm_close_btn_) {
      lv_obj_move_foreground(dm_close_btn_);
    }
    if (dm_new_btn_) {
      lv_obj_move_foreground(dm_new_btn_);
    }
#endif
    lv_group_focus_obj(dm_panel_);
    return;
  }

  compose_return_to_dm_ = false;
  if (restore_chat_focus) {
    focus_zone_ = FocusZone::Chat;
    focusCurrentZoneObject();
    refreshShortcutVisuals();
  }
}

void StandaloneUi::onComposeKeyboardEvent(lv_event_t* event) {
  auto* ui = static_cast<StandaloneUi*>(lv_event_get_user_data(event));
  if (!ui) {
    return;
  }

  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_READY) {
    if (ui->identity_prompt_open_) {
      if (ui->applyIdentityNameFromPrompt()) {
        ui->identity_prompt_open_ = false;
        ui->closeComposeDialog(true);
      }
    } else if (ui->sendComposeMessage()) {
      ui->closeComposeDialog(true);
    }
    return;
  }

  if (code == LV_EVENT_CANCEL) {
    ui->closeComposeDialog(true);
  }
}

void StandaloneUi::onComposeActionEvent(lv_event_t* event) {
  auto* ui = static_cast<StandaloneUi*>(lv_event_get_user_data(event));
  if (!ui || lv_event_get_code(event) != LV_EVENT_CLICKED) {
    return;
  }

  lv_obj_t* target = lv_event_get_target(event);
  if (target == ui->compose_cancel_btn_) {
    ui->closeComposeDialog(true);
    return;
  }

  if (target != ui->compose_send_btn_) {
    return;
  }

  if (ui->identity_prompt_open_) {
    if (ui->applyIdentityNameFromPrompt()) {
      ui->identity_prompt_open_ = false;
      ui->closeComposeDialog(true);
    }
    return;
  }

  if (ui->sendComposeMessage()) {
    ui->closeComposeDialog(true);
  }
}

void StandaloneUi::onOpenContactsDialogAsync(void* user_data) {
  auto* ui = static_cast<StandaloneUi*>(user_data);
  if (!ui) {
    return;
  }

  ui->pending_contacts_open_ = false;
  CTS_TRACE("openContactsDialog async begin");
  if (ui->openContactsDialog()) {
    CTS_TRACE("openContactsDialog async done");
    return;
  }

  ui->appendChatLine("[INFO] Contacts panel unavailable; listing heard nodes:", ChatLineKind::Normal);
  if (!ui->mesh_adapter_) {
    ui->appendChatLine("[INFO] Mesh adapter unavailable", ChatLineKind::Error);
    return;
  }

  mesh::MeshContactSummary contacts[8]{};
  const int count = ui->mesh_adapter_->exportContacts(contacts, 8);
  if (count <= 0) {
    ui->appendChatLine("[INFO] No heard nodes yet", ChatLineKind::Normal);
    return;
  }

  for (int i = 1; i < count; i++) {
    mesh::MeshContactSummary key = contacts[i];
    int j = i - 1;
    while (j >= 0 && contactSortBefore(key, contacts[j])) {
      contacts[j + 1] = contacts[j];
      j--;
    }
    contacts[j + 1] = key;
  }

  char line[96] = {};
  for (int i = 0; i < count; i++) {
    snprintf(line, sizeof(line), "[CT] %s%s", contacts[i].favorite ? "* " : "", contacts[i].name);
    ui->appendChatLine(line, ChatLineKind::Normal);
  }
}

void StandaloneUi::onContactsPostOpenAsync(void* user_data) {
  auto* ui = static_cast<StandaloneUi*>(user_data);
  if (!ui) {
    return;
  }

  ui->pending_contacts_post_open_ = false;
  if (!ui->contacts_open_ || !ui->contacts_dialog_) {
    CTS_TRACE("openContactsDialog post skip open=%d dialog=%p", ui->contacts_open_ ? 1 : 0,
              static_cast<void*>(ui->contacts_dialog_));
    return;
  }

  CTS_TRACE("openContactsDialog post activate begin");
  lv_obj_add_flag(ui->contacts_dialog_, LV_OBJ_FLAG_CLICKABLE);
  if (ui->contacts_detail_panel_) {
    lv_obj_add_flag(ui->contacts_detail_panel_, LV_OBJ_FLAG_CLICKABLE);
  }
  if (ui->contacts_detail_info_panel_) {
    lv_obj_add_flag(ui->contacts_detail_info_panel_, LV_OBJ_FLAG_CLICKABLE);
  }
  if (ui->contacts_dm_panel_) {
    lv_obj_add_flag(ui->contacts_dm_panel_, LV_OBJ_FLAG_CLICKABLE);
  }
  if (ui->contacts_dm_new_btn_) {
    lv_obj_add_flag(ui->contacts_dm_new_btn_, LV_OBJ_FLAG_CLICKABLE);
  }
  for (uint8_t i = 0; i < kContactActionCount; i++) {
    if (ui->contacts_action_rows_[i]) {
      lv_obj_add_flag(ui->contacts_action_rows_[i], LV_OBJ_FLAG_CLICKABLE);
    }
  }
  resetPointerInputState();
  CTS_TRACE("openContactsDialog post activate done");
}

void StandaloneUi::onOpenComposeDialogAsync(void* user_data) {
  auto* ui = static_cast<StandaloneUi*>(user_data);
  if (!ui) {
    return;
  }
  ui->openComposeDialog();
}

void StandaloneUi::onComposePostOpenAsync(void* user_data) {
  auto* ui = static_cast<StandaloneUi*>(user_data);
  if (!ui) {
    return;
  }
  ui->pending_compose_post_open_ = false;
  if (!ui->compose_open_ || !ui->compose_dialog_ || !ui->compose_input_) {
    CTS_TRACE("openComposeDialog post skip open=%d dialog=%p input=%p", ui->compose_open_ ? 1 : 0,
              static_cast<void*>(ui->compose_dialog_), static_cast<void*>(ui->compose_input_));
    return;
  }
  CTS_TRACE("openComposeDialog post show_kb begin dm_mode=%d", ui->compose_dm_mode_ ? 1 : 0);
  ui->showComposeKeyboard();
  CTS_TRACE("openComposeDialog post show_kb done");
  if (kUseOnscreenKeyboard) {
    CTS_TRACE("openComposeDialog post reset_ptr begin");
    resetPointerInputState();
    CTS_TRACE("openComposeDialog post reset_ptr done");
    CTS_TRACE("openComposeDialog post layout begin");
    lv_obj_update_layout(ui->compose_dialog_);
    CTS_TRACE("openComposeDialog post layout done");
  }
  if (!kUseOnscreenKeyboard && ui->key_group_) {
    CTS_TRACE("openComposeDialog post focus begin");
    lv_group_focus_obj(ui->compose_input_);
    CTS_TRACE("openComposeDialog post focus done");
  }
  CTS_TRACE("openComposeDialog post dm_mode=%d", ui->compose_dm_mode_ ? 1 : 0);
}

bool StandaloneUi::sendComposeMessage() {
  if (!compose_input_) {
    return false;
  }

  const char* text = lv_textarea_get_text(compose_input_);
  if (!text || text[0] == '\0') {
    return false;
  }

  bool sent_any = false;
  bool failed_any = false;

  auto send_single_line = [&](const char* line_text) {
    if (!line_text || line_text[0] == '\0') {
      return;
    }

    char generated_text[kComposeMessageMaxChars + 1] = {};
    const char* send_text = line_text;
    if (strcmp(line_text, "testmessage") == 0) {
      buildDebugTestMessage(generated_text, sizeof(generated_text));
      send_text = generated_text;
    }

    const bool sent = mesh_adapter_ &&
                      (compose_dm_mode_ ? mesh_adapter_->sendDirectMessage(compose_target_dm_pubkey_, send_text)
                                        : mesh_adapter_->sendChannelMessage(compose_target_channel_, send_text));
    if (!sent) {
      failed_any = true;
      return;
    }

    sent_any = true;
    const uint32_t line_epoch = nowEpochSecondsOrZero();

    char display_text[96] = {};
    if (!compose_dm_mode_) {
      char hhmm[8] = {};
      formatUiClockHhMm(hhmm, sizeof(hhmm));
      snprintf(display_text, sizeof(display_text), "[%s] Me: %s", hhmm, send_text);
    }

    strncpy(pending_local_echo_channel_, compose_target_channel_, sizeof(pending_local_echo_channel_) - 1);
    pending_local_echo_channel_[sizeof(pending_local_echo_channel_) - 1] = '\0';
    strncpy(pending_local_echo_text_, send_text, sizeof(pending_local_echo_text_) - 1);
    pending_local_echo_text_[sizeof(pending_local_echo_text_) - 1] = '\0';
    pending_local_echo_deadline_ms_ = millis() + kLocalEchoSuppressMs;

    if (!compose_dm_mode_) {
      pushChannelHistoryLine(compose_target_channel_, display_text, ChatLineKind::Tx, line_epoch);
    }
    if (!compose_dm_mode_ && strcmp(compose_target_channel_, configured_channel_names_[active_channel_]) == 0) {
      appendChatLine(display_text, ChatLineKind::Tx, line_epoch);
    } else if (compose_dm_mode_) {
      char hhmm[8] = {};
      formatUiClockHhMm(hhmm, sizeof(hhmm));
      char dm_line[96] = {};
      snprintf(dm_line, sizeof(dm_line), "[%s] Me: %s", hhmm, send_text);
      const char* dm_contact_name = compose_target_channel_[0] != '\0' ? compose_target_channel_ : dm_active_name_;
      const char* dm_contact_key = compose_target_dm_pubkey_[0] != '\0' ? compose_target_dm_pubkey_ : dm_active_key_;
      appendDmLine(dm_contact_name, dm_contact_key, dm_line, ChatLineKind::Tx, line_epoch);
    }
  };

  const char* cursor = text;
  while (cursor && cursor[0] != '\0') {
    while (cursor[0] == '\r' || cursor[0] == '\n') {
      cursor++;
    }
    if (cursor[0] == '\0') {
      break;
    }

    const char* line_start = cursor;
    while (cursor[0] != '\0' && cursor[0] != '\r' && cursor[0] != '\n') {
      cursor++;
    }

    size_t line_len = static_cast<size_t>(cursor - line_start);
    if (line_len == 0) {
      continue;
    }

    char line_text[kComposeMessageMaxChars + 1] = {};
    if (line_len > kComposeMessageMaxChars) {
      line_len = kComposeMessageMaxChars;
    }
    memcpy(line_text, line_start, line_len);
    line_text[line_len] = '\0';
    send_single_line(line_text);
  }

  if (!sent_any && !failed_any) {
    send_single_line(text);
  }

  if (failed_any) {
    appendChatLine("[ERR] Send failed", ChatLineKind::Error);
  }

  return sent_any;
}

bool StandaloneUi::handleComposeKey(uint32_t key) {
  if (!compose_open_ || !compose_input_) {
    return false;
  }

  const bool is_backspace = (key == LV_KEY_BACKSPACE || key == 8 || key == 127);

  if (identity_prompt_open_) {
    if (key == LV_KEY_ENTER || key == '\n' || key == '\r') {
      if (applyIdentityNameFromPrompt()) {
        identity_prompt_open_ = false;
        closeComposeDialog(true);
      }
      return true;
    }

    // Keep the prompt open until a valid name is submitted.
    return true;
  }

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

void StandaloneUi::refreshContactsDialog(bool reload_from_mesh) {
  if (!contacts_dialog_ || !contacts_status_label_ || !contacts_detail_info_panel_ || !contacts_full_name_label_ ||
      !contacts_lat_lon_label_ || !contacts_last_heard_label_ || !contacts_telemetry_label_ ||
      !contacts_dm_panel_ || !contacts_dm_new_btn_) {
    return;
  }
  for (uint8_t i = 0; i < kContactActionCount; i++) {
    if (!contacts_action_labels_[i]) {
      return;
    }
  }

  if (reload_from_mesh) {
    char previous_selected_key[65] = {};
    char previous_selected_name[32] = {};
    const bool had_previous_selection =
        contacts_count_ > 0 && contacts_selected_index_ < contacts_count_;
    if (had_previous_selection) {
      strncpy(previous_selected_key, contacts_cache_[contacts_selected_index_].public_key_hex,
              sizeof(previous_selected_key) - 1);
      previous_selected_key[sizeof(previous_selected_key) - 1] = '\0';
      strncpy(previous_selected_name, contacts_cache_[contacts_selected_index_].name,
              sizeof(previous_selected_name) - 1);
      previous_selected_name[sizeof(previous_selected_name) - 1] = '\0';
    }

    if (mesh_adapter_) {
      memset(contacts_cache_, 0, sizeof(contacts_cache_));
      const int exported = mesh_adapter_->exportContacts(contacts_cache_, kMaxContactsUi);
      contacts_count_ = exported > 0 ? static_cast<uint8_t>(exported) : static_cast<uint8_t>(0);

      for (uint8_t i = 1; i < contacts_count_; i++) {
        mesh::MeshContactSummary key = contacts_cache_[i];
        int j = static_cast<int>(i) - 1;
        while (j >= 0 && contactSortBefore(key, contacts_cache_[j])) {
          contacts_cache_[j + 1] = contacts_cache_[j];
          j--;
        }
        contacts_cache_[j + 1] = key;
      }
    } else {
      contacts_count_ = 0;
    }

    if (contacts_count_ > 0 && had_previous_selection) {
      const int prev_idx =
          findContactIndexByIdentity(contacts_cache_, contacts_count_, previous_selected_key, previous_selected_name);
      if (prev_idx >= 0) {
        contacts_selected_index_ = static_cast<uint8_t>(prev_idx);
      }
    }
  }

  if (contacts_count_ == 0) {
    contacts_dm_open_ = false;
    contacts_selected_index_ = 0;
  } else if (contacts_selected_index_ >= contacts_count_) {
    contacts_selected_index_ = static_cast<uint8_t>(contacts_count_ - 1);
  }

  if (contacts_count_ == 0) {
    lv_label_set_text(contacts_full_name_label_, "Node: -");
    lv_label_set_text(contacts_lat_lon_label_, "Lat/Lon: -");
    lv_label_set_text(contacts_last_heard_label_, "Last: -");
    lv_label_set_text(contacts_telemetry_label_, "Telemetry: -");
    lv_label_set_text(contacts_action_labels_[0], addFavoriteActionLabel());
  #if defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_label_set_text(contacts_action_labels_[1], "DM");
  #else
    lv_label_set_text(contacts_action_labels_[1], "(D)M");
  #endif
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    lv_label_set_text(contacts_action_labels_[2], "CLOSE");
#endif
    lv_obj_add_flag(contacts_dm_new_btn_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(contacts_dm_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(contacts_status_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(contacts_detail_info_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(contacts_status_label_, "No heard nodes yet");
    refreshChannelVisuals();
    return;
  }

  const mesh::MeshContactSummary& selected = contacts_cache_[contacts_selected_index_];
  char detail[128] = {};
  snprintf(detail, sizeof(detail), "Node: %s", selected.name);
  lv_label_set_text(contacts_full_name_label_, detail);

  if (contactHasCoord(selected.gps_lat_i, selected.gps_lon_i)) {
    const double lat = contactCoordToDouble(selected.gps_lat_i);
    const double lon = contactCoordToDouble(selected.gps_lon_i);
    snprintf(detail, sizeof(detail), "Lat/Lon: %.5f, %.5f", lat, lon);
  } else {
    snprintf(detail, sizeof(detail), "Lat/Lon: -");
  }
  lv_label_set_text(contacts_lat_lon_label_, detail);

  formatContactLastHeard(selected.lastmod, detail, sizeof(detail));
  lv_label_set_text(contacts_last_heard_label_, detail);

  formatContactTelemetry(selected, detail, sizeof(detail));
  lv_label_set_text(contacts_telemetry_label_, detail);

  lv_label_set_text(contacts_action_labels_[0], favoriteActionLabel(selected.favorite));
#if defined(DEVICE_HELTEC_V4_EXPANSION)
  lv_label_set_text(contacts_action_labels_[1], "DM");
#else
  lv_label_set_text(contacts_action_labels_[1], "(D)M");
#endif
#if defined(DEVICE_HELTEC_V4_EXPANSION)
  lv_label_set_text(contacts_action_labels_[2], "CLOSE");
#endif
  for (uint8_t i = 0; i < kContactActionCount; i++) {
    if (!contacts_action_rows_[i] || !contacts_action_labels_[i]) {
      continue;
    }

    if (!contacts_nav_focused_ && !contacts_dm_open_ && i == contacts_action_index_) {
      lv_obj_set_style_bg_color(contacts_action_rows_[i], lv_color_hex(0x1E9ED1), LV_PART_MAIN);
      lv_obj_set_style_border_color(contacts_action_rows_[i], lv_color_hex(0x8DEBFF), LV_PART_MAIN);
      lv_obj_set_style_text_color(contacts_action_rows_[i], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
      lv_obj_set_style_text_color(contacts_action_labels_[i], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    } else {
      lv_obj_set_style_bg_color(contacts_action_rows_[i], lv_color_hex(0x14344B), LV_PART_MAIN);
      lv_obj_set_style_border_color(contacts_action_rows_[i], lv_color_hex(0x3F7292), LV_PART_MAIN);
      lv_obj_set_style_text_color(contacts_action_rows_[i], lv_color_hex(0xD8E7F2), LV_PART_MAIN);
      lv_obj_set_style_text_color(contacts_action_labels_[i], lv_color_hex(0xD8E7F2), LV_PART_MAIN);
    }
    lv_obj_invalidate(contacts_action_rows_[i]);
  }

  const lv_coord_t dialog_h = lv_obj_get_height(contacts_dialog_);
  const lv_coord_t dm_panel_collapsed_h = dialog_h > 66 ? static_cast<lv_coord_t>(dialog_h - 66) : 24;

  if (contacts_dm_open_) {
    lv_obj_add_flag(contacts_status_label_, LV_OBJ_FLAG_HIDDEN);
    if (contacts_detail_panel_) {
      lv_obj_add_flag(contacts_detail_panel_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(contacts_dm_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(contacts_dm_panel_, 0, 0);
    lv_obj_set_size(contacts_dm_panel_, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(contacts_dm_new_btn_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(contacts_dm_new_btn_, LV_ALIGN_BOTTOM_RIGHT, -2, -2);
    lv_obj_move_foreground(contacts_dm_new_btn_);
    lv_obj_add_flag(contacts_detail_info_panel_, LV_OBJ_FLAG_HIDDEN);
    rebuildContactsDmPanel();
  } else {
    lv_obj_add_flag(contacts_status_label_, LV_OBJ_FLAG_HIDDEN);
    if (contacts_detail_panel_) {
      lv_obj_clear_flag(contacts_detail_panel_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(contacts_dm_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(contacts_dm_panel_, 0, 42);
    lv_obj_set_size(contacts_dm_panel_, LV_PCT(100), dm_panel_collapsed_h);
    lv_obj_add_flag(contacts_dm_new_btn_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(contacts_detail_info_panel_, LV_OBJ_FLAG_HIDDEN);
  }

  char status_line[160] = {};
  if (contacts_dm_open_) {
    status_line[0] = '\0';
  } else if (contacts_status_text_[0] != '\0') {
    snprintf(status_line, sizeof(status_line), "%s", contacts_status_text_);
  } else {
    status_line[0] = '\0';
  }
  lv_label_set_text(contacts_status_label_, status_line);
  refreshChannelVisuals();
}

void StandaloneUi::rebuildContactsDmPanel() {
  if (!contacts_dm_panel_) {
    return;
  }

  lv_obj_clean(contacts_dm_panel_);

  if (!contacts_dm_open_ || contacts_count_ == 0 || contacts_selected_index_ >= contacts_count_) {
    return;
  }

  const mesh::MeshContactSummary& selected = contacts_cache_[contacts_selected_index_];
  const char* selected_name = selected.name;
  const char* selected_key = selected.public_key_hex;
  const lv_coord_t row_w = lv_obj_get_content_width(contacts_dm_panel_);
  uint32_t rendered_date_key = 0;

  size_t recent_indices[kDmDialogRecentLimit] = {};
  size_t recent_count = 0;
  for (size_t i = 0; i < stored_dm_count_ && recent_count < kDmDialogRecentLimit; i++) {
    const size_t rev = stored_dm_count_ - 1 - i;
    const size_t idx = (stored_dm_head_ + rev) % kMaxStoredChatRows;
    const StoredDmLine& line = stored_dm_[idx];
    if (line.contact_name[0] == '\0' || line.text[0] == '\0') {
      continue;
    }

    const bool selected_has_key = selected_key && selected_key[0] != '\0';
    const bool line_has_key = line.contact_key[0] != '\0';
    const bool key_match = selected_has_key && line_has_key && strcmp(selected_key, line.contact_key) == 0;
    const bool name_match = dmNameLikelyMatch(selected_name, line.contact_name);
    if (!key_match && !name_match) {
      continue;
    }

    recent_indices[recent_count++] = idx;
  }

  for (size_t i = recent_count; i > 0; i--) {
    const StoredDmLine& line = stored_dm_[recent_indices[i - 1]];
    const uint32_t date_key = dateKeyFromEpoch(line.timestamp_epoch);
    if (line.timestamp_epoch != 0 && date_key != 0 && date_key != rendered_date_key) {
      createDateMarkerRow(contacts_dm_panel_, row_w > 0 ? row_w : LV_PCT(100), line.timestamp_epoch,
                          chatPanelFont());
      rendered_date_key = date_key;
    }

    char display_line[112] = {};
    formatDmDisplayLine(line.text, line.timestamp_epoch, display_line, sizeof(display_line));
    lv_obj_t* row = lv_label_create(contacts_dm_panel_);
    lv_obj_set_width(row, row_w > 0 ? row_w : LV_PCT(100));
    lv_label_set_long_mode(row, LV_LABEL_LONG_WRAP);
    lv_obj_add_style(row, &style_text_main_, 0);
    lv_obj_set_style_text_font(row, chatPanelFont(), 0);

    switch (line.kind) {
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

    lv_label_set_text(row, display_line);
  }

  if (recent_count == 0) {
    lv_obj_t* row = lv_label_create(contacts_dm_panel_);
    lv_obj_set_width(row, row_w > 0 ? row_w : LV_PCT(100));
    lv_label_set_long_mode(row, LV_LABEL_LONG_WRAP);
    lv_obj_add_style(row, &style_text_dim_, 0);
    lv_obj_set_style_text_font(row, chatPanelFont(), 0);
    lv_label_set_text(row, "No messages yet");
  }

  lv_obj_scroll_to_y(contacts_dm_panel_, LV_COORD_MAX, LV_ANIM_OFF);
}

void StandaloneUi::startComposeForSelectedContact() {
  if (!contacts_open_ || contacts_count_ == 0 || contacts_selected_index_ >= contacts_count_) {
    return;
  }

  const mesh::MeshContactSummary& selected = contacts_cache_[contacts_selected_index_];
  compose_dm_mode_ = true;
  compose_return_to_dm_ = false;
  strncpy(compose_target_dm_pubkey_, selected.public_key_hex, sizeof(compose_target_dm_pubkey_) - 1);
  compose_target_dm_pubkey_[sizeof(compose_target_dm_pubkey_) - 1] = '\0';
  strncpy(compose_target_channel_, selected.name, sizeof(compose_target_channel_) - 1);
  compose_target_channel_[sizeof(compose_target_channel_) - 1] = '\0';
  openComposeDialog();
}

bool StandaloneUi::openContactsDialog() {
  if (!kEnableContactsDialog) {
    return false;
  }

  CTS_TRACE("openContactsDialog enter open=%d dialog=%p", contacts_open_ ? 1 : 0, static_cast<void*>(contacts_dialog_));
  if (!ensureContactsDialogBuilt()) {
    CTS_TRACE("openContactsDialog ensure failed");
    return false;
  }
  if (contacts_open_ || !contacts_dialog_ || !contacts_detail_panel_ || !contacts_detail_info_panel_) {
    CTS_TRACE("openContactsDialog blocked open=%d dialog=%p detail=%p info=%p", contacts_open_ ? 1 : 0,
              static_cast<void*>(contacts_dialog_), static_cast<void*>(contacts_detail_panel_),
              static_cast<void*>(contacts_detail_info_panel_));
    return false;
  }

  contacts_open_ = true;
  contacts_nav_focused_ = false;
  contacts_dm_open_ = false;
  contacts_selected_index_ = 0;
  contacts_action_index_ = 0;
  contacts_status_text_[0] = '\0';
  refreshHeaderVisuals();
  closeChannelDropdown(false);

  if (chat_panel_) {
    lv_obj_add_flag(chat_panel_, LV_OBJ_FLAG_HIDDEN);
  }
  if (chat_advz_btn_) {
    lv_obj_add_flag(chat_advz_btn_, LV_OBJ_FLAG_HIDDEN);
  }
  if (chat_advf_btn_) {
    lv_obj_add_flag(chat_advf_btn_, LV_OBJ_FLAG_HIDDEN);
  }
  if (chat_new_btn_) {
    lv_obj_add_flag(chat_new_btn_, LV_OBJ_FLAG_HIDDEN);
  }
  if (shortcut_strip_) {
    lv_obj_add_flag(shortcut_strip_, LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_clear_flag(contacts_dialog_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(contacts_dialog_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_move_foreground(contacts_dialog_);

  if (key_group_ && !kUseOnscreenKeyboard) {
    for (uint8_t i = 0; i < kContactActionCount; i++) {
      if (contacts_action_rows_[i] && lv_obj_get_group(contacts_action_rows_[i]) != key_group_) {
        lv_group_add_obj(key_group_, contacts_action_rows_[i]);
      }
    }
    if (contacts_dm_panel_ && lv_obj_get_group(contacts_dm_panel_) != key_group_) {
      lv_group_add_obj(key_group_, contacts_dm_panel_);
    }
    if (contacts_dm_new_btn_ && lv_obj_get_group(contacts_dm_new_btn_) != key_group_) {
      lv_group_add_obj(key_group_, contacts_dm_new_btn_);
    }
    if (contacts_detail_info_panel_ && lv_obj_get_group(contacts_detail_info_panel_) != key_group_) {
      lv_group_add_obj(key_group_, contacts_detail_info_panel_);
    }
  }

  refreshContactsDialog(true);

  if (has_unread_dm_ && contacts_count_ > 0) {
    const int dm_contact_idx =
        findContactIndexByIdentity(contacts_cache_, contacts_count_, last_dm_sender_key_, last_dm_sender_name_);
    if (dm_contact_idx >= 0) {
      contacts_selected_index_ = static_cast<uint8_t>(dm_contact_idx);
      contacts_nav_focused_ = false;
      contacts_dm_open_ = true;
      has_unread_dm_ = false;
      refreshShortcutVisuals();
      refreshContactsDialog(false);
    }
  }

  pending_contacts_open_ = false;
  pending_contacts_show_ = false;
  pending_contacts_post_open_ = false;
  focus_zone_ = FocusZone::Selector;
  resetPointerInputState();
  focusCurrentZoneObject();

  CTS_TRACE("openContactsDialog done count=%u sel=%u nav=%d dm=%d", static_cast<unsigned>(contacts_count_),
            static_cast<unsigned>(contacts_selected_index_), contacts_nav_focused_ ? 1 : 0,
            contacts_dm_open_ ? 1 : 0);

  return true;
}

void StandaloneUi::closeContactsDialog(bool focus_chat) {
  CTS_TRACE("closeContactsDialog enter focus_chat=%d open=%d", focus_chat ? 1 : 0, contacts_open_ ? 1 : 0);
  if (!contacts_open_) {
    return;
  }

  contacts_open_ = false;
  contacts_nav_focused_ = false;
  contacts_dm_open_ = false;
  pending_contacts_show_ = false;
  pending_contacts_post_open_ = false;
  refreshHeaderVisuals();
  closeChannelDropdown(false);
  if (contacts_dialog_) {
    lv_obj_add_flag(contacts_dialog_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(contacts_dialog_, LV_OBJ_FLAG_CLICKABLE);
  }
  if (chat_panel_) {
    lv_obj_clear_flag(chat_panel_, LV_OBJ_FLAG_HIDDEN);
  }
  if (chat_advz_btn_) {
    lv_obj_clear_flag(chat_advz_btn_, LV_OBJ_FLAG_HIDDEN);
  }
  if (chat_advf_btn_) {
    lv_obj_clear_flag(chat_advf_btn_, LV_OBJ_FLAG_HIDDEN);
  }
  if (chat_new_btn_) {
    lv_obj_clear_flag(chat_new_btn_, LV_OBJ_FLAG_HIDDEN);
  }
  if (shortcut_strip_) {
    lv_obj_clear_flag(shortcut_strip_, LV_OBJ_FLAG_HIDDEN);
  }

  refreshChannelVisuals();
  refreshHeaderVisuals();

  resetPointerInputState();

  if (focus_chat) {
    setFocusZone(FocusZone::Chat);
    CTS_TRACE("closeContactsDialog exit -> chat");
    return;
  }

  focus_zone_ = FocusZone::Shortcuts;
  selected_shortcut_ = kShortcutContacts;
  refreshShortcutVisuals();
  focusCurrentZoneObject();
  CTS_TRACE("closeContactsDialog exit -> shortcuts");
}

void StandaloneUi::moveContactsSelection(int delta) {
  if (contacts_count_ == 0) {
    return;
  }

  int next = static_cast<int>(contacts_selected_index_) + delta;
  if (next < 0) {
    next = static_cast<int>(contacts_count_) - 1;
  } else if (next >= static_cast<int>(contacts_count_)) {
    next = 0;
  }
  contacts_selected_index_ = static_cast<uint8_t>(next);
  contacts_dm_open_ = false;
  refreshContactsDialog(false);
  focusCurrentZoneObject();
}

void StandaloneUi::activateContactsAction(uint8_t action_idx) {
  if (!contacts_open_ || action_idx >= kContactActionCount) {
    return;
  }

#if defined(DEVICE_HELTEC_V4_EXPANSION)
  if (action_idx == 2) {
    closeContactsDialog(true);
    return;
  }
#endif

  if (contacts_count_ == 0) {
    return;
  }

  contacts_action_index_ = action_idx;
  contacts_nav_focused_ = false;
  mesh::MeshContactSummary& selected = contacts_cache_[contacts_selected_index_];

  if (action_idx == 0) {
    if (!mesh_adapter_) {
      strncpy(contacts_status_text_, "Favorite update failed", sizeof(contacts_status_text_) - 1);
      contacts_status_text_[sizeof(contacts_status_text_) - 1] = '\0';
      refreshContactsDialog(false);
      return;
    }

    char selected_key[65] = {};
    char selected_name[32] = {};
    strncpy(selected_key, selected.public_key_hex, sizeof(selected_key) - 1);
    selected_key[sizeof(selected_key) - 1] = '\0';
    strncpy(selected_name, selected.name, sizeof(selected_name) - 1);
    selected_name[sizeof(selected_name) - 1] = '\0';

    const bool next_fav = !selected.favorite;
    if (mesh_adapter_->setContactFavoriteByPublicKeyHex(selected.public_key_hex, next_fav)) {
      selected.favorite = next_fav;
      for (uint8_t i = 1; i < contacts_count_; i++) {
        mesh::MeshContactSummary key = contacts_cache_[i];
        int j = static_cast<int>(i) - 1;
        while (j >= 0 && contactSortBefore(key, contacts_cache_[j])) {
          contacts_cache_[j + 1] = contacts_cache_[j];
          j--;
        }
        contacts_cache_[j + 1] = key;
      }
      const int idx = findContactIndexByIdentity(contacts_cache_, contacts_count_, selected_key, selected_name);
      if (idx >= 0) {
        contacts_selected_index_ = static_cast<uint8_t>(idx);
      }
      snprintf(contacts_status_text_, sizeof(contacts_status_text_), "%s favorite: %s", selected_name,
               next_fav ? "ON" : "OFF");
    } else {
      strncpy(contacts_status_text_, "Favorite update failed", sizeof(contacts_status_text_) - 1);
      contacts_status_text_[sizeof(contacts_status_text_) - 1] = '\0';
    }
    contacts_dm_open_ = false;
    refreshContactsDialog(false);
    return;
  }

  if (action_idx == 1) {
    contacts_dm_open_ = true;
    has_unread_dm_ = false;
    refreshShortcutVisuals();
    refreshContactsDialog(false);
    focusCurrentZoneObject();
    return;
  }

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
  lv_label_set_text(cfg_row_labels_[kCfgRowNodeName], row_text);

  snprintf(row_text, sizeof(row_text), "Radio Preset: %s", radioPresetDisplayName(web_settings.region));
  lv_label_set_text(cfg_row_labels_[kCfgRowRadioPreset], row_text);

  const bool web_on = plumeria::web::running();
  const char* web_ip = plumeria::web::ip();
  if (!web_on) {
    snprintf(row_text, sizeof(row_text), "Web Config: OFF");
  } else if (web_ip && web_ip[0] != '\0') {
    snprintf(row_text, sizeof(row_text), "Web Config: ON (%s)", web_ip);
  } else {
    snprintf(row_text, sizeof(row_text), "Web Config: ON");
  }
  lv_label_set_text(cfg_row_labels_[kCfgRowWebConfig], row_text);

  lv_label_set_text(cfg_row_labels_[kCfgRowGps], web_settings.send_location_in_advert
                                              ? "GPS: OFF (using default lat/long)"
                                              : "GPS: ON");

  const uint8_t multipaths = static_cast<uint8_t>((web_settings.path_hash_mode <= 2 ? web_settings.path_hash_mode : 0) + 1);
  snprintf(row_text, sizeof(row_text), "Multipaths: %u", static_cast<unsigned>(multipaths));
  lv_label_set_text(cfg_row_labels_[kCfgRowMultipaths], row_text);

  snprintf(row_text, sizeof(row_text), "Mesh Region: %s",
           web_settings.mesh_region[0] ? web_settings.mesh_region : "(unfiltered)");
  lv_label_set_text(cfg_row_labels_[kCfgRowMeshRegion], row_text);
#if !defined(DEVICE_HELTEC_V4_EXPANSION)
  lv_label_set_text(cfg_row_labels_[kCfgRowExportConfig], "Export Config to SD");
  lv_label_set_text(cfg_row_labels_[kCfgRowImportConfig], cfg_import_confirm_armed_ ? "Import Config [PRESS ENTER AGAIN]"
                                                                                      : "Import Config to SD");
  lv_label_set_text(cfg_row_labels_[kCfgRowDeleteConfig], cfg_delete_confirm_armed_ ? "Delete Config [PRESS ENTER AGAIN]"
                                                                                      : "Delete Config from SD");
#endif

  for (uint8_t i = 0; i < kCfgRowCount; i++) {
    lv_obj_remove_style(cfg_rows_[i], &style_button_active_, 0);
    if (i == cfg_selected_row_) {
      lv_obj_add_style(cfg_rows_[i], &style_button_active_, 0);
    }
  }

#if defined(DEVICE_HELTEC_V4_EXPANSION)
  lv_label_set_text(cfg_status_label_, "");
#else
  lv_label_set_text(cfg_status_label_, "Enter - Activate, Bcksp - Close");
#endif
  lv_label_set_text(cfg_action_label_, cfg_action_text_);
}

void StandaloneUi::openCfgDialog() {
  if (cfg_open_ || !cfg_dialog_) {
    return;
  }
  cfg_open_ = true;
  cfg_selected_row_ = 0;
  cfg_import_confirm_armed_ = false;
  cfg_delete_confirm_armed_ = false;
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
  cfg_delete_confirm_armed_ = false;
  lv_obj_add_flag(cfg_dialog_, LV_OBJ_FLAG_HIDDEN);
  if (shortcut_strip_) {
    lv_obj_clear_flag(shortcut_strip_, LV_OBJ_FLAG_HIDDEN);
  }
  if (focus_chat) {
    setFocusZone(FocusZone::Chat);
  } else {
    focus_zone_ = FocusZone::Shortcuts;
    selected_shortcut_ = kShortcutCfg;
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
#if !defined(DEVICE_HELTEC_V4_EXPANSION)
  if (cfg_selected_row_ != kCfgRowImportConfig) {
    cfg_import_confirm_armed_ = false;
  }
  if (cfg_selected_row_ != kCfgRowDeleteConfig) {
    cfg_delete_confirm_armed_ = false;
  }
#endif
  refreshCfgDialog();
  if (key_group_ && cfg_rows_[cfg_selected_row_]) {
    lv_group_focus_obj(cfg_rows_[cfg_selected_row_]);
  }
}

bool StandaloneUi::exportConfigToSd() {
  String text;
  if (!plumeria::web::exportConfigText(&text) || text.length() == 0) {
    strncpy(cfg_status_text_, "Export failed: empty config", sizeof(cfg_status_text_) - 1);
    cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
    return false;
  }

  char sd_err[64] = {};
  if (!sdBeginForCurrentBoard(sd_err, sizeof(sd_err))) {
    snprintf(cfg_status_text_, sizeof(cfg_status_text_), "Export failed: %s", sd_err[0] ? sd_err : "SD init failed");
    cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
    return false;
  }

  bool dir_ready = SD.exists(kCfgSdDir);
  if (!dir_ready) {
    dir_ready = SD.mkdir(kCfgSdDir);
  }

  const char* target_path = dir_ready ? kCfgSdPath : kCfgSdPathFallback;

  if (SD.exists(target_path)) {
    SD.remove(target_path);
  }

  File file = SD.open(target_path, FILE_WRITE);
  if (!file) {
    snprintf(cfg_status_text_, sizeof(cfg_status_text_), "Export failed: open %s", target_path);
    cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
    return false;
  }

  const size_t wrote = file.print(text);
  file.close();
  if (wrote != static_cast<size_t>(text.length())) {
    snprintf(cfg_status_text_, sizeof(cfg_status_text_), "Export failed: wrote %u/%u bytes",
             static_cast<unsigned>(wrote), static_cast<unsigned>(text.length()));
    cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
    return false;
  }

  snprintf(cfg_status_text_, sizeof(cfg_status_text_), "Exported to %s", target_path);
  cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
  return true;
}

bool StandaloneUi::setGpsEnabled(bool enabled) {
  plumeria::web::WebSettings web_settings{};
  plumeria::web::loadSettings(&web_settings);
  // In this screen, GPS ON means use live GPS; GPS OFF means use default coords.
  const bool use_default_location = !enabled;
  if (web_settings.send_location_in_advert == use_default_location) {
    return true;
  }

  // Apply immediately to runtime mesh even when web config is currently disabled.
  if (mesh_adapter_ &&
      !mesh_adapter_->setAdvertLocation(use_default_location, web_settings.node_latitude, web_settings.node_longitude)) {
    return false;
  }
  if (mesh_adapter_) {
    mesh_adapter_->setGpsEnabled(enabled);
  }
  if (mesh_adapter_) {
    mesh_adapter_->broadcastSelfAdvertNow();
  }

  char err[96] = {};
  const bool ok = plumeria::web::setSendLocationInAdvert(use_default_location, err, sizeof(err));
  if (ok && started_) {
    refreshHeaderVisuals();
  }
  return ok;
}

bool StandaloneUi::importConfigFromSd() {
  char sd_err[64] = {};
  if (!sdBeginForCurrentBoard(sd_err, sizeof(sd_err))) {
    snprintf(cfg_status_text_, sizeof(cfg_status_text_), "Import failed: %s", sd_err[0] ? sd_err : "SD init failed");
    cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
    return false;
  }

  File file = SD.open(kCfgSdPath, FILE_READ);
  const char* read_path = kCfgSdPath;
  if (!file) {
    file = SD.open(kCfgSdPathFallback, FILE_READ);
    read_path = kCfgSdPathFallback;
  }
  if (!file) {
    strncpy(cfg_status_text_, "Import failed: config file missing", sizeof(cfg_status_text_) - 1);
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

  snprintf(cfg_status_text_, sizeof(cfg_status_text_), "Import applied from %s", read_path);
  cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';

  return true;
}

bool StandaloneUi::deleteConfigFromSd() {
  char sd_err[64] = {};
  if (!sdBeginForCurrentBoard(sd_err, sizeof(sd_err))) {
    snprintf(cfg_status_text_, sizeof(cfg_status_text_), "Delete failed: %s", sd_err[0] ? sd_err : "SD init failed");
    cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
    return false;
  }

  const bool has_primary = SD.exists(kCfgSdPath);
  const bool has_fallback = SD.exists(kCfgSdPathFallback);
  if (!has_primary && !has_fallback) {
    strncpy(cfg_status_text_, "Delete skipped: config not found", sizeof(cfg_status_text_) - 1);
    cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
    return true;
  }

  bool ok = true;
  if (has_primary) {
    ok = ok && SD.remove(kCfgSdPath);
  }
  if (has_fallback) {
    ok = ok && SD.remove(kCfgSdPathFallback);
  }

  if (!ok) {
    strncpy(cfg_status_text_, "Delete failed", sizeof(cfg_status_text_) - 1);
    cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
    return false;
  }

  strncpy(cfg_status_text_, "Deleted config file(s)", sizeof(cfg_status_text_) - 1);
  cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
  return true;
}

void StandaloneUi::activateCfgSelection() {
#if !defined(DEVICE_HELTEC_V4_EXPANSION)
  if (cfg_selected_row_ != kCfgRowImportConfig) {
    cfg_import_confirm_armed_ = false;
  }
  if (cfg_selected_row_ != kCfgRowDeleteConfig) {
    cfg_delete_confirm_armed_ = false;
  }
#endif

  switch (cfg_selected_row_) {
    case kCfgRowNodeName:
    case kCfgRowRadioPreset:
    case kCfgRowMeshRegion:
      strncpy(cfg_status_text_, "Read-only row. Use web config to edit settings.", sizeof(cfg_status_text_) - 1);
      cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
      strncpy(cfg_action_text_, "No change", sizeof(cfg_action_text_) - 1);
      cfg_action_text_[sizeof(cfg_action_text_) - 1] = '\0';
      break;
    case kCfgRowWebConfig: {
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
    case kCfgRowGps: {
      plumeria::web::WebSettings web_settings{};
      plumeria::web::loadSettings(&web_settings);
      const bool gps_enabled = !web_settings.send_location_in_advert;
      const bool next_enabled = !gps_enabled;
      if (setGpsEnabled(next_enabled)) {
        strncpy(cfg_status_text_, next_enabled ? "GPS enabled" : "GPS disabled (using default location)",
                sizeof(cfg_status_text_) - 1);
        strncpy(cfg_action_text_, next_enabled ? "GPS turned on" : "GPS turned off",
                sizeof(cfg_action_text_) - 1);
      } else {
        strncpy(cfg_status_text_, "GPS toggle failed", sizeof(cfg_status_text_) - 1);
        strncpy(cfg_action_text_, "GPS toggle failed", sizeof(cfg_action_text_) - 1);
      }
      cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
      cfg_action_text_[sizeof(cfg_action_text_) - 1] = '\0';
      break;
    }
    case kCfgRowMultipaths: {
      plumeria::web::WebSettings web_settings{};
      plumeria::web::loadSettings(&web_settings);
      const uint8_t current_mode = web_settings.path_hash_mode <= 2 ? web_settings.path_hash_mode : 0;
      const uint8_t next_mode = static_cast<uint8_t>((current_mode + 1) % 3);

      char err[96] = {};
      if (plumeria::web::setPathHashMode(next_mode, err, sizeof(err))) {
        snprintf(cfg_status_text_, sizeof(cfg_status_text_), "Multipaths set to %u",
                 static_cast<unsigned>(next_mode + 1));
        snprintf(cfg_action_text_, sizeof(cfg_action_text_), "Multipaths %u",
                 static_cast<unsigned>(next_mode + 1));
      } else {
        strncpy(cfg_status_text_, err[0] ? err : "Multipaths change failed", sizeof(cfg_status_text_) - 1);
        cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
        strncpy(cfg_action_text_, "Multipaths failed", sizeof(cfg_action_text_) - 1);
        cfg_action_text_[sizeof(cfg_action_text_) - 1] = '\0';
      }
      break;
    }
#if !defined(DEVICE_HELTEC_V4_EXPANSION)
    case kCfgRowExportConfig:
      if (exportConfigToSd()) {
        strncpy(cfg_action_text_, "Config exported", sizeof(cfg_action_text_) - 1);
      } else {
        if (cfg_status_text_[0] == '\0') {
          strncpy(cfg_status_text_, "Export failed", sizeof(cfg_status_text_) - 1);
        }
        strncpy(cfg_action_text_, "Export failed", sizeof(cfg_action_text_) - 1);
      }
      cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
      cfg_action_text_[sizeof(cfg_action_text_) - 1] = '\0';
      break;
    case kCfgRowImportConfig:
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
      break;
    case kCfgRowDeleteConfig:
      if (!cfg_delete_confirm_armed_) {
        cfg_delete_confirm_armed_ = true;
        strncpy(cfg_status_text_, "Press Enter again to confirm delete", sizeof(cfg_status_text_) - 1);
        cfg_status_text_[sizeof(cfg_status_text_) - 1] = '\0';
        strncpy(cfg_action_text_, "Delete armed", sizeof(cfg_action_text_) - 1);
        cfg_action_text_[sizeof(cfg_action_text_) - 1] = '\0';
      } else {
        cfg_delete_confirm_armed_ = false;
        if (deleteConfigFromSd()) {
          strncpy(cfg_action_text_, "Config deleted", sizeof(cfg_action_text_) - 1);
        } else {
          strncpy(cfg_action_text_, "Delete failed", sizeof(cfg_action_text_) - 1);
        }
        cfg_action_text_[sizeof(cfg_action_text_) - 1] = '\0';
      }
      break;
#endif
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
    lv_obj_remove_style(shortcut_btns_[i], &style_button_active_, 0);
    lv_obj_remove_style(shortcut_btns_[i], &style_shortcut_active_, 0);
    lv_obj_remove_style(shortcut_btns_[i], &style_unread_edge_, 0);

    if (shortcut_labels_[i]) {
      const bool unread_dm = (i == kShortcutContacts && has_unread_dm_);
      const bool selected = (focus_zone_ == FocusZone::Shortcuts && i == selected_shortcut_);

#if defined(DEVICE_CARDPUTER_LORA_HAT)
      char label_text[20] = {};
      snprintf(label_text, sizeof(label_text), "%s%s", kShortcutNames[i], unread_dm ? "*" : "");
      lv_label_set_text(shortcut_labels_[i], label_text);
      lv_obj_set_style_text_color(shortcut_labels_[i],
                                  selected ? kColorFocus : (unread_dm ? kColorUnread : kColorTextDim),
                                  0);
#else
      lv_label_set_text(shortcut_labels_[i], kShortcutNames[i]);
      lv_obj_set_style_text_color(shortcut_labels_[i], unread_dm ? kColorUnread : kColorTextDim, 0);
#endif
    }

    if (i == kShortcutContacts && has_unread_dm_) {
#if !defined(DEVICE_CARDPUTER_LORA_HAT)
      lv_obj_add_style(shortcut_btns_[i], &style_unread_edge_, 0);
#endif
    }

    if (focus_zone_ == FocusZone::Shortcuts && i == selected_shortcut_) {
#if !defined(DEVICE_CARDPUTER_LORA_HAT)
      lv_obj_add_style(shortcut_btns_[i], &style_shortcut_active_, 0);
#endif
    }
  }
}

void StandaloneUi::refreshUnreadPulse(uint32_t now_ms) {
  if (now_ms - last_unread_pulse_ms_ < 40) {
    return;
  }
  last_unread_pulse_ms_ = now_ms;

  bool has_unread_channel = false;
  for (uint8_t i = 0; i < configured_channel_count_; i++) {
    if (unread_channels_[i]) {
      has_unread_channel = true;
      break;
    }
  }

  if (!has_unread_dm_ && !has_unread_channel) {
    return;
  }

  const uint32_t phase = (now_ms / 45U) % 24U;
  const uint32_t tri = phase <= 12U ? phase : (24U - phase);
  const lv_opa_t border_opa =
      static_cast<lv_opa_t>(LV_OPA_30 + ((tri * (LV_OPA_COVER - LV_OPA_30)) / 12U));
  const lv_opa_t outline_opa =
      static_cast<lv_opa_t>(LV_OPA_10 + ((tri * (LV_OPA_80 - LV_OPA_10)) / 12U));

  lv_style_set_border_opa(&style_unread_edge_, border_opa);
  lv_style_set_outline_opa(&style_unread_edge_, outline_opa);

  if (has_unread_dm_ && shortcut_btns_[kShortcutContacts]) {
    lv_obj_invalidate(shortcut_btns_[kShortcutContacts]);
  }
  if (has_unread_channel && !channel_dropdown_open_ && channel_selector_btn_) {
    lv_obj_invalidate(channel_selector_btn_);
  }
}

void StandaloneUi::refreshHeaderVisuals() {
  if (!channel_selector_btn_ || !gps_label_ || !wifi_label_ || !time_label_ || !battery_pct_label_ ||
      !battery_bar_ || !wifi_ap_badge_label_) {
    return;
  }

  plumeria::web::WebSettings web_settings{};
  plumeria::web::loadSettings(&web_settings);
  gps_ok_ = !web_settings.send_location_in_advert;

  int sat_count = -1;
  if (mesh_adapter_) {
    sat_count = mesh_adapter_->getGpsSatelliteCount();
  }

  char gps_text[24] = {};
  if (gps_ok_ && sat_count >= 0) {
    snprintf(gps_text, sizeof(gps_text), "%s %d", LV_SYMBOL_GPS, sat_count);
  } else {
    snprintf(gps_text, sizeof(gps_text), "%s", LV_SYMBOL_GPS);
  }
  lv_label_set_text(gps_label_, gps_text);
  lv_label_set_text(wifi_label_, LV_SYMBOL_WIFI);
  char batt_text[8] = {};
  snprintf(batt_text, sizeof(batt_text), "%u%%", static_cast<unsigned>(battery_pct_));
  lv_label_set_text(battery_pct_label_, batt_text);

  const bool hide_wireless_icons = contacts_open_;
  const bool hide_time_label = contacts_open_;
#if defined(DEVICE_CARDPUTER_LORA_HAT)
  const bool hide_battery_pct = true;
#else
  const bool hide_battery_pct = contacts_open_;
#endif

  if (hide_battery_pct) {
    lv_obj_add_flag(battery_pct_label_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(battery_pct_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(battery_pct_label_, LV_ALIGN_RIGHT_MID, kHeaderBatteryTextX, 0);
  }

  if (hide_wireless_icons) {
    lv_obj_add_flag(gps_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifi_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifi_ap_badge_label_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(gps_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(wifi_label_, LV_OBJ_FLAG_HIDDEN);
  }

  if (hide_time_label) {
    lv_obj_add_flag(time_label_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(time_label_, LV_OBJ_FLAG_HIDDEN);
  }

  if (!hide_wireless_icons) {
    lv_obj_align_to(gps_label_, channel_selector_btn_, LV_ALIGN_OUT_RIGHT_MID, kHeaderTimeGap, 0);
    lv_obj_align_to(wifi_label_, gps_label_, LV_ALIGN_OUT_RIGHT_MID, kHeaderIconsGap, 0);
    lv_obj_align_to(wifi_ap_badge_label_, wifi_label_, LV_ALIGN_TOP_RIGHT, 4, -4);
  }
  lv_obj_align(battery_bar_, LV_ALIGN_RIGHT_MID, kHeaderBatteryBarX, 0);
  if (!hide_time_label) {
    const lv_coord_t left_anchor_right_x = hide_wireless_icons
        ? static_cast<lv_coord_t>(lv_obj_get_x(channel_selector_btn_) + lv_obj_get_width(channel_selector_btn_))
        : static_cast<lv_coord_t>(lv_obj_get_x(wifi_label_) + lv_obj_get_width(wifi_label_));
    const lv_coord_t batt_left_x = static_cast<lv_coord_t>(lv_obj_get_x(battery_bar_));
    const lv_coord_t time_center_x = static_cast<lv_coord_t>((left_anchor_right_x + batt_left_x) / 2);
    const lv_coord_t time_x = static_cast<lv_coord_t>(time_center_x - (lv_obj_get_width(time_label_) / 2));
    lv_obj_align(time_label_, LV_ALIGN_LEFT_MID, time_x, 0);
  }

  lv_obj_set_style_text_color(gps_label_, gps_ok_ ? kColorWifiOn : kColorTextDim, 0);
  if (hide_wireless_icons || !wifi_config_server_on_) {
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
    if (!kUseOnscreenKeyboard) {
      lv_group_focus_obj(compose_input_);
    }
    return;
  }

  if (dm_open_ && dm_panel_) {
    lv_group_focus_obj(dm_panel_);
    return;
  }

  if (contacts_open_) {
    const uint8_t contacts_option_count = clampOptionCount(contacts_count_, kChannelCount);
    if (channel_dropdown_open_ && contacts_option_count > 0) {
      if (dropdown_highlight_channel_ >= contacts_option_count) {
        dropdown_highlight_channel_ = static_cast<uint8_t>(contacts_option_count - 1);
      }
      lv_group_focus_obj(channel_dropdown_rows_[dropdown_highlight_channel_]);
      return;
    }

    if (contacts_nav_focused_) {
      lv_group_focus_obj(channel_selector_btn_);
      return;
    }

    if (contacts_dm_open_) {
      if (contacts_dm_panel_) {
        lv_group_focus_obj(contacts_dm_panel_);
      }
      return;
    }

    if (contacts_action_rows_[contacts_action_index_]) {
      lv_group_focus_obj(contacts_action_rows_[contacts_action_index_]);
      return;
    }

    if (channel_selector_btn_) {
      lv_group_focus_obj(channel_selector_btn_);
      return;
    }
  }

  if (channel_dropdown_open_ && configured_channel_count_ > 0) {
    if (dropdown_highlight_channel_ >= configured_channel_count_) {
      dropdown_highlight_channel_ = static_cast<uint8_t>(configured_channel_count_ - 1);
    }
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

  if (index == kShortcutCfg) {
    openCfgDialog();
    return;
  }

  if (index == kShortcutContacts) {
    openContactsDialog();
    return;
  }

  if (index == kShortcutLive) {
    openLiveDialog();
    return;
  }

  if (index == kShortcutHelp) {
    openHelpDialog();
    return;
  }

}

void StandaloneUi::triggerAdvertZeroHop() {
  if (mesh_adapter_ && mesh_adapter_->broadcastSelfAdvertNow()) {
    openAdvertPopup("Advert sent (zero-hop)", false);
  } else {
    openAdvertPopup("Advert failed (zero-hop)", true);
  }
}

void StandaloneUi::triggerAdvertFlood() {
  if (mesh_adapter_ && mesh_adapter_->broadcastSelfAdvertFloodNow()) {
    openAdvertPopup("Advert sent (flood)", false);
  } else {
    openAdvertPopup("Advert failed (flood)", true);
  }
}

void StandaloneUi::appendChatLine(const char* text, ChatLineKind kind, uint32_t timestamp_epoch) {
  if (!text || text[0] == '\0' || !chat_panel_) {
    return;
  }

  const bool at_bottom = lv_obj_get_scroll_bottom(chat_panel_) <= 2;
  const lv_coord_t scroll_y = lv_obj_get_scroll_y(chat_panel_);
  lv_coord_t row_w = lv_obj_get_content_width(chat_panel_);
  if (row_w <= 0) {
    row_w = lv_obj_get_width(chat_panel_);
  }
  const uint32_t date_key = dateKeyFromEpoch(timestamp_epoch);

  auto ensure_capacity = [&]() {
    if (chat_row_count_ < kMaxChatRows) {
      return;
    }
    lv_obj_del(chat_rows_[0]);
    for (size_t i = 1; i < chat_row_count_; i++) {
      chat_rows_[i - 1] = chat_rows_[i];
    }
    chat_row_count_--;
  };

  if (date_key != 0 && date_key != chat_last_date_key_) {
    ensure_capacity();
    lv_obj_t* marker = createDateMarkerRow(chat_panel_, row_w > 0 ? row_w : LV_PCT(100), timestamp_epoch,
                                           chatPanelFont());
    if (marker) {
      chat_rows_[chat_row_count_++] = marker;
      chat_last_date_key_ = date_key;
    }
  }

  ensure_capacity();

  lv_obj_t* row = lv_label_create(chat_panel_);
  lv_obj_set_width(row, row_w > 0 ? row_w : LV_PCT(100));
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
  if (date_key != 0) {
    chat_last_date_key_ = date_key;
  }

  if (at_bottom) {
    lv_obj_scroll_to_y(chat_panel_, LV_COORD_MAX, LV_ANIM_OFF);
  } else {
    lv_obj_scroll_to_y(chat_panel_, scroll_y, LV_ANIM_OFF);
  }
}

void StandaloneUi::clearDmPanel() {
  for (size_t i = 0; i < dm_row_count_; i++) {
    if (dm_rows_[i]) {
      lv_obj_del(dm_rows_[i]);
      dm_rows_[i] = nullptr;
    }
  }
  dm_row_count_ = 0;
  dm_last_date_key_ = 0;
}

void StandaloneUi::appendDmLine(const char* contact_name, const char* contact_key, const char* text,
                                ChatLineKind kind, uint32_t timestamp_epoch) {
  if (!contact_name || contact_name[0] == '\0' || !text || text[0] == '\0') {
    return;
  }

  const uint32_t now_epoch = nowEpochSecondsOrZero();
  const uint32_t line_epoch = timestamp_epoch != 0 ? timestamp_epoch : now_epoch;
  pruneStoredDmByRetention(now_epoch);

  if (stored_dm_count_ < kMaxStoredChatRows) {
    size_t write_index = (stored_dm_head_ + stored_dm_count_) % kMaxStoredChatRows;
    strncpy(stored_dm_[write_index].contact_name, contact_name, sizeof(stored_dm_[write_index].contact_name) - 1);
    stored_dm_[write_index].contact_name[sizeof(stored_dm_[write_index].contact_name) - 1] = '\0';
    if (contact_key && contact_key[0] != '\0') {
      strncpy(stored_dm_[write_index].contact_key, contact_key, sizeof(stored_dm_[write_index].contact_key) - 1);
      stored_dm_[write_index].contact_key[sizeof(stored_dm_[write_index].contact_key) - 1] = '\0';
    } else {
      stored_dm_[write_index].contact_key[0] = '\0';
    }
    strncpy(stored_dm_[write_index].text, text, sizeof(stored_dm_[write_index].text) - 1);
    stored_dm_[write_index].text[sizeof(stored_dm_[write_index].text) - 1] = '\0';
    stored_dm_[write_index].kind = kind;
    stored_dm_[write_index].timestamp_epoch = line_epoch;
    stored_dm_count_++;
  } else {
    strncpy(stored_dm_[stored_dm_head_].contact_name, contact_name,
            sizeof(stored_dm_[stored_dm_head_].contact_name) - 1);
    stored_dm_[stored_dm_head_].contact_name[sizeof(stored_dm_[stored_dm_head_].contact_name) - 1] = '\0';
    if (contact_key && contact_key[0] != '\0') {
      strncpy(stored_dm_[stored_dm_head_].contact_key, contact_key,
              sizeof(stored_dm_[stored_dm_head_].contact_key) - 1);
      stored_dm_[stored_dm_head_].contact_key[sizeof(stored_dm_[stored_dm_head_].contact_key) - 1] = '\0';
    } else {
      stored_dm_[stored_dm_head_].contact_key[0] = '\0';
    }
    strncpy(stored_dm_[stored_dm_head_].text, text, sizeof(stored_dm_[stored_dm_head_].text) - 1);
    stored_dm_[stored_dm_head_].text[sizeof(stored_dm_[stored_dm_head_].text) - 1] = '\0';
    stored_dm_[stored_dm_head_].kind = kind;
    stored_dm_[stored_dm_head_].timestamp_epoch = line_epoch;
    stored_dm_head_ = (stored_dm_head_ + 1) % kMaxStoredChatRows;
  }

  dm_history_dirty_ = true;

  const bool dm_has_key = dm_active_key_[0] != '\0';
  const bool line_has_key = contact_key && contact_key[0] != '\0';
  const bool key_match = dm_has_key && line_has_key && strcmp(dm_active_key_, contact_key) == 0;
  const bool name_match = dmNameLikelyMatch(dm_active_name_, contact_name);
  const bool line_matches_active = key_match || name_match;

  bool line_matches_contacts_dm = false;
  if (contacts_open_ && contacts_dm_open_ && contacts_count_ > 0 && contacts_selected_index_ < contacts_count_) {
    const mesh::MeshContactSummary& selected = contacts_cache_[contacts_selected_index_];
    const bool selected_has_key = selected.public_key_hex[0] != '\0';
    const bool contacts_key_match =
        selected_has_key && line_has_key && strcmp(selected.public_key_hex, contact_key) == 0;
    const bool contacts_name_match = dmNameLikelyMatch(selected.name, contact_name);
    line_matches_contacts_dm = contacts_key_match || contacts_name_match;
    if (line_matches_contacts_dm) {
      rebuildContactsDmPanel();
    }
  }

  if (dm_open_ && !line_matches_active) {
    if (false) Serial.printf("[UI][DM] skip line active_name=%s line_name=%s dm_key=%d line_key=%d\n",
                  dm_active_name_, contact_name, dm_has_key ? 1 : 0, line_has_key ? 1 : 0);
  }
  if (!dm_open_ || !dm_panel_ || !line_matches_active) {
    return;
  }

  const bool at_bottom = lv_obj_get_scroll_bottom(dm_panel_) <= 2;
  const lv_coord_t scroll_y = lv_obj_get_scroll_y(dm_panel_);
  lv_coord_t dm_w = lv_obj_get_content_width(dm_panel_);
  if (dm_w <= 0) {
    dm_w = lv_obj_get_width(dm_panel_);
  }
  const lv_coord_t row_w = dm_w > 4 ? static_cast<lv_coord_t>(dm_w - 2) : dm_w;
  const uint32_t date_key = dateKeyFromEpoch(line_epoch);

  auto ensure_capacity = [&]() {
    if (dm_row_count_ < kMaxChatRows) {
      return;
    }
    lv_obj_del(dm_rows_[0]);
    for (size_t i = 1; i < dm_row_count_; i++) {
      dm_rows_[i - 1] = dm_rows_[i];
    }
    dm_row_count_--;
  };

  if (date_key != 0 && date_key != dm_last_date_key_) {
    ensure_capacity();
    lv_obj_t* marker = createDateMarkerRow(dm_panel_, row_w > 0 ? row_w : LV_PCT(100), line_epoch,
                                           chatPanelFont());
    if (marker) {
      dm_rows_[dm_row_count_++] = marker;
      dm_last_date_key_ = date_key;
    }
  }

  ensure_capacity();

  lv_obj_t* row = lv_label_create(dm_panel_);
  lv_obj_set_width(row, row_w > 0 ? row_w : LV_PCT(100));
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
  dm_rows_[dm_row_count_++] = row;
  if (date_key != 0) {
    dm_last_date_key_ = date_key;
  }

  if (at_bottom) {
    lv_obj_scroll_to_y(dm_panel_, LV_COORD_MAX, LV_ANIM_OFF);
  } else {
    lv_obj_scroll_to_y(dm_panel_, scroll_y, LV_ANIM_OFF);
  }
}

void StandaloneUi::rebuildDmDialog() {
  if (!dm_dialog_ || !dm_title_label_ || !dm_panel_) {
    return;
  }

  char title[48] = {};
  snprintf(title, sizeof(title), "%s", dm_active_name_[0] ? dm_active_name_ : "DM");
  lv_label_set_text(dm_title_label_, title);

  // Some LVGL paths can leave the panel with 0 height; enforce usable geometry before drawing rows.
  lv_coord_t dlg_w = lv_obj_get_width(dm_dialog_);
  lv_coord_t dlg_h = lv_obj_get_height(dm_dialog_);
  if (dlg_w <= 80) {
    dlg_w = dialogMaxW(220, 260);
  }
  if (dlg_h <= 40) {
    dlg_h = 180;
  }
  lv_obj_set_size(dm_dialog_, dlg_w, dlg_h);
  lv_obj_set_pos(dm_panel_, 2, 18);
#if defined(DEVICE_HELTEC_V4_EXPANSION)
  if (dm_new_btn_) {
    lv_obj_set_size(dm_new_btn_, 56, 18);
    lv_obj_align(dm_new_btn_, LV_ALIGN_BOTTOM_RIGHT, -2, -30);
  }
  if (dm_close_btn_) {
    lv_obj_set_size(dm_close_btn_, LV_PCT(100), 22);
    lv_obj_align(dm_close_btn_, LV_ALIGN_BOTTOM_MID, 0, -2);
  }
  lv_coord_t panel_h = static_cast<lv_coord_t>(dlg_h - 42);
#else
  lv_coord_t panel_h = static_cast<lv_coord_t>(dlg_h - 22);
#endif
  if (panel_h < 40) {
    panel_h = 40;
  }
  lv_obj_set_size(dm_panel_, LV_PCT(100), panel_h);

#if defined(DEVICE_HELTEC_V4_EXPANSION)
  if (dm_close_btn_) {
    lv_obj_move_foreground(dm_close_btn_);
  }
  if (dm_new_btn_) {
    lv_obj_move_foreground(dm_new_btn_);
  }
#endif

  lv_obj_update_layout(dm_dialog_);
  lv_obj_update_layout(dm_panel_);
  lv_coord_t dm_w = lv_obj_get_content_width(dm_panel_);
  if (dm_w <= 0) {
    dm_w = lv_obj_get_width(dm_panel_);
  }
  const lv_coord_t row_w = dm_w > 4 ? static_cast<lv_coord_t>(dm_w - 2) : dm_w;

  clearDmPanel();
  size_t matched_rows = 0;
  size_t recent_indices[kDmDialogRecentLimit] = {};
  size_t recent_count = 0;

  // Select up to the most recent matching DM lines first.
  for (size_t i = 0; i < stored_dm_count_ && recent_count < kDmDialogRecentLimit; i++) {
    const size_t rev = stored_dm_count_ - 1 - i;
    const size_t idx = (stored_dm_head_ + rev) % kMaxStoredChatRows;
    const StoredDmLine& line = stored_dm_[idx];
    if (line.contact_name[0] == '\0' || line.text[0] == '\0') {
      continue;
    }

    const bool dm_has_key = dm_active_key_[0] != '\0';
    const bool line_has_key = line.contact_key[0] != '\0';
    const bool key_match = dm_has_key && line_has_key && strcmp(dm_active_key_, line.contact_key) == 0;
    const bool name_match = dmNameLikelyMatch(dm_active_name_, line.contact_name);
    const bool line_matches_active = key_match || name_match;
    if (!line_matches_active) {
      continue;
    }

    matched_rows++;
    recent_indices[recent_count++] = idx;
  }

  // Render selected messages in chronological order.
  for (size_t i = recent_count; i > 0; i--) {
    const size_t idx = recent_indices[i - 1];
    const StoredDmLine& line = stored_dm_[idx];
    const uint32_t date_key = dateKeyFromEpoch(line.timestamp_epoch);
    if (line.timestamp_epoch != 0 && date_key != 0 && date_key != dm_last_date_key_) {
      if (dm_row_count_ >= kMaxChatRows) {
        break;
      }
      lv_obj_t* marker = createDateMarkerRow(dm_panel_, row_w > 0 ? row_w : LV_PCT(100), line.timestamp_epoch,
                                             chatPanelFont());
      if (marker) {
        dm_rows_[dm_row_count_++] = marker;
      }
      dm_last_date_key_ = date_key;
    }

    char display_line[112] = {};
    formatDmDisplayLine(line.text, line.timestamp_epoch, display_line, sizeof(display_line));

    if (dm_row_count_ >= kMaxChatRows) {
      break;
    }

    lv_obj_t* row = lv_label_create(dm_panel_);
    lv_obj_set_width(row, row_w > 0 ? row_w : LV_PCT(100));
    lv_label_set_long_mode(row, LV_LABEL_LONG_WRAP);
    lv_obj_add_style(row, &style_text_main_, 0);
    lv_obj_set_style_text_font(row, chatPanelFont(), 0);

    switch (line.kind) {
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

    lv_label_set_text(row, display_line);
    dm_rows_[dm_row_count_++] = row;
  }

  if (dm_row_count_ == 0) {
    lv_obj_t* row = lv_label_create(dm_panel_);
    lv_obj_set_width(row, row_w > 0 ? row_w : LV_PCT(100));
    lv_label_set_long_mode(row, LV_LABEL_LONG_WRAP);
    lv_obj_add_style(row, &style_text_dim_, 0);
    lv_obj_set_style_text_font(row, chatPanelFont(), 0);
    lv_label_set_text(row, "No messages yet");
    dm_rows_[dm_row_count_++] = row;
  }

  if (false) Serial.printf("[UI][DM] rebuild active=%s key=%d stored=%u matched=%u drawn=%u panel_w=%d panel_h=%d\n",
                dm_active_name_,
                dm_active_key_[0] != '\0' ? 1 : 0, static_cast<unsigned>(stored_dm_count_),
                static_cast<unsigned>(matched_rows), static_cast<unsigned>(dm_row_count_),
                static_cast<int>(lv_obj_get_width(dm_panel_)), static_cast<int>(lv_obj_get_height(dm_panel_)));

  lv_obj_update_layout(dm_panel_);
  lv_obj_invalidate(dm_panel_);
  lv_obj_invalidate(dm_dialog_);
  lv_obj_scroll_to_y(dm_panel_, LV_COORD_MAX, LV_ANIM_OFF);
}

void StandaloneUi::openDmDialog(const char* contact_name, const char* contact_key) {
  CTS_TRACE("openDmDialog enter name=%s open=%d", contact_name ? contact_name : "(null)", dm_open_ ? 1 : 0);
  if (!contact_name || contact_name[0] == '\0' || !dm_dialog_ || !contacts_dialog_) {
    CTS_TRACE("openDmDialog blocked dialog=%p contacts=%p", static_cast<void*>(dm_dialog_),
              static_cast<void*>(contacts_dialog_));
    return;
  }

  if (!dm_title_label_) {
    dm_title_label_ = lv_label_create(dm_dialog_);
    if (dm_title_label_) {
      lv_obj_add_style(dm_title_label_, &style_text_main_, 0);
      lv_label_set_text(dm_title_label_, "DM");
      lv_obj_align(dm_title_label_, LV_ALIGN_TOP_LEFT, 4, 2);
      if (false) Serial.println("[UI][DM] recreated title label");
    }
  }

  if (!dm_panel_) {
    dm_panel_ = lv_obj_create(dm_dialog_);
    if (dm_panel_) {
      lv_coord_t dlg_h = lv_obj_get_height(dm_dialog_);
      if (dlg_h <= 40) {
        dlg_h = 180;
      }
      lv_obj_set_pos(dm_panel_, 2, 18);
    #if defined(DEVICE_HELTEC_V4_EXPANSION)
      lv_obj_set_size(dm_panel_, LV_PCT(100), static_cast<lv_coord_t>(dlg_h - 42));
    #else
      lv_obj_set_size(dm_panel_, LV_PCT(100), static_cast<lv_coord_t>(dlg_h - 22));
    #endif
      lv_obj_add_style(dm_panel_, &style_chat_, 0);
      lv_obj_set_scroll_dir(dm_panel_, LV_DIR_VER);
      lv_obj_set_scrollbar_mode(dm_panel_, LV_SCROLLBAR_MODE_OFF);
      lv_obj_set_layout(dm_panel_, LV_LAYOUT_FLEX);
      lv_obj_set_flex_flow(dm_panel_, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_flex_align(dm_panel_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
      lv_obj_set_style_pad_row(dm_panel_, 1, LV_PART_MAIN);
      lv_obj_add_event_cb(dm_panel_, onFocusableEvent, LV_EVENT_KEY, this);
      lv_obj_add_event_cb(dm_panel_, onDmEvent, LV_EVENT_PRESSED, this);
      lv_obj_add_event_cb(dm_panel_, onFocusableEvent, LV_EVENT_FOCUSED, this);
      if (key_group_) {
        lv_group_add_obj(key_group_, dm_panel_);
      }
      if (false) Serial.println("[UI][DM] recreated dm panel");
    }
  }

  if (!dm_title_label_ || !dm_panel_) {
    if (false) Serial.println("[UI][DM] open aborted: DM UI incomplete");
    return;
  }

#if defined(DEVICE_HELTEC_V4_EXPANSION)
  if (!dm_close_btn_) {
    dm_close_btn_ = lv_btn_create(dm_dialog_);
    if (dm_close_btn_) {
      lv_obj_set_size(dm_close_btn_, LV_PCT(100), 22);
      lv_obj_align(dm_close_btn_, LV_ALIGN_BOTTOM_MID, 0, -2);
      lv_obj_add_style(dm_close_btn_, &style_button_, 0);
      lv_obj_add_style(dm_close_btn_, &style_button_focused_, LV_STATE_FOCUSED);
      lv_obj_clear_flag(dm_close_btn_, LV_OBJ_FLAG_EVENT_BUBBLE);
      lv_obj_add_event_cb(dm_close_btn_, onFocusableEvent, LV_EVENT_KEY, this);
      lv_obj_add_event_cb(dm_close_btn_, onDmEvent, LV_EVENT_PRESSED, this);
      lv_obj_add_event_cb(dm_close_btn_, onFocusableEvent, LV_EVENT_FOCUSED, this);
      if (key_group_) {
        lv_group_add_obj(key_group_, dm_close_btn_);
      }
    }
  }
  if (dm_close_btn_ && !dm_close_label_) {
    dm_close_label_ = lv_label_create(dm_close_btn_);
    if (dm_close_label_) {
      lv_obj_add_style(dm_close_label_, &style_text_main_, 0);
      lv_label_set_text(dm_close_label_, "CLOSE");
      lv_obj_center(dm_close_label_);
    }
  }
#endif

  strncpy(dm_active_name_, contact_name, sizeof(dm_active_name_) - 1);
  dm_active_name_[sizeof(dm_active_name_) - 1] = '\0';
  if (contact_key && contact_key[0] != '\0') {
    strncpy(dm_active_key_, contact_key, sizeof(dm_active_key_) - 1);
    dm_active_key_[sizeof(dm_active_key_) - 1] = '\0';
  } else {
    dm_active_key_[0] = '\0';
    // If contact export missed a key, recover from recent DM history for this name.
    for (size_t i = 0; i < stored_dm_count_; i++) {
      const size_t rev = stored_dm_count_ - 1 - i;
      const size_t idx = (stored_dm_head_ + rev) % kMaxStoredChatRows;
      const StoredDmLine& line = stored_dm_[idx];
      if (line.contact_key[0] == '\0' || line.contact_name[0] == '\0') {
        continue;
      }
      if (dmNameLikelyMatch(dm_active_name_, line.contact_name)) {
        strncpy(dm_active_key_, line.contact_key, sizeof(dm_active_key_) - 1);
        dm_active_key_[sizeof(dm_active_key_) - 1] = '\0';
        break;
      }
    }
  }

  dm_open_ = true;
  has_unread_dm_ = false;
  if (false) Serial.printf("[UI][DM] open name=%s key=%d stored=%u\n", dm_active_name_, dm_active_key_[0] != '\0' ? 1 : 0,
                static_cast<unsigned>(stored_dm_count_));
  refreshShortcutVisuals();
  lv_obj_add_flag(contacts_dialog_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(dm_dialog_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(dm_dialog_, LV_OBJ_FLAG_CLICKABLE);
  if (dm_panel_) {
    lv_obj_add_flag(dm_panel_, LV_OBJ_FLAG_CLICKABLE);
  }
  if (dm_close_btn_) {
    lv_obj_clear_flag(dm_close_btn_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(dm_close_btn_, LV_OBJ_FLAG_CLICKABLE);
  }
  if (dm_new_btn_) {
    lv_obj_clear_flag(dm_new_btn_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(dm_new_btn_, LV_OBJ_FLAG_CLICKABLE);
  }
  lv_obj_move_foreground(dm_dialog_);
#if defined(DEVICE_HELTEC_V4_EXPANSION)
  if (dm_close_btn_) {
    lv_obj_move_foreground(dm_close_btn_);
  }
  if (dm_new_btn_) {
    lv_obj_move_foreground(dm_new_btn_);
  }
#endif
  lv_obj_update_layout(dm_dialog_);
  rebuildDmDialog();

  if (key_group_ && dm_panel_) {
    lv_group_focus_obj(dm_panel_);
  }
  CTS_TRACE("openDmDialog done dm_open=%d key_present=%d", dm_open_ ? 1 : 0,
            dm_active_key_[0] != '\0' ? 1 : 0);
}

void StandaloneUi::closeDmDialog(bool focus_chat) {
  CTS_TRACE("closeDmDialog enter focus_chat=%d open=%d", focus_chat ? 1 : 0, dm_open_ ? 1 : 0);
  if (!dm_open_ || !dm_dialog_) {
    return;
  }

  dm_open_ = false;
  lv_obj_add_flag(dm_dialog_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(dm_dialog_, LV_OBJ_FLAG_CLICKABLE);
  if (dm_panel_) {
    lv_obj_clear_flag(dm_panel_, LV_OBJ_FLAG_CLICKABLE);
  }
  if (dm_close_btn_) {
    lv_obj_clear_flag(dm_close_btn_, LV_OBJ_FLAG_CLICKABLE);
  }
  if (dm_new_btn_) {
    lv_obj_clear_flag(dm_new_btn_, LV_OBJ_FLAG_CLICKABLE);
  }
  lv_obj_move_background(dm_dialog_);

  if (focus_chat) {
    closeContactsDialog(true);
    CTS_TRACE("closeDmDialog exit -> chat");
    return;
  }

  if (contacts_dialog_) {
    contacts_open_ = true;
    lv_obj_clear_flag(contacts_dialog_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(contacts_dialog_, LV_OBJ_FLAG_CLICKABLE);
    if (contacts_detail_panel_) {
      lv_obj_add_flag(contacts_detail_panel_, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_move_foreground(contacts_dialog_);
    if (contacts_count_ > 0 && contacts_selected_index_ >= contacts_count_) {
      contacts_selected_index_ = static_cast<uint8_t>(contacts_count_ - 1);
    }
    contacts_nav_focused_ = false;
    contacts_dm_open_ = false;
    contacts_action_index_ = 1;
    refreshContactsDialog(false);
    focusCurrentZoneObject();
  } else {
    contacts_open_ = false;
    setFocusZone(FocusZone::Chat);
  }
  refreshHeaderVisuals();
  resetPointerInputState();
  CTS_TRACE("closeDmDialog exit -> contacts_open=%d", contacts_open_ ? 1 : 0);
}

void StandaloneUi::openHelpDialog() {
  if (live_open_) {
    closeLiveDialog();
  }

  if (help_open_ || !help_dialog_) {
    return;
  }

  help_open_ = true;
  selected_shortcut_ = kShortcutHelp;
  focus_zone_ = FocusZone::Shortcuts;
  refreshShortcutVisuals();
  lv_obj_clear_flag(help_dialog_, LV_OBJ_FLAG_HIDDEN);
  if (help_body_panel_) {
    lv_obj_scroll_to_y(help_body_panel_, 0, LV_ANIM_OFF);
  }
  lv_obj_move_foreground(help_dialog_);
  if (key_group_) {
    if (help_body_panel_) {
      lv_group_focus_obj(help_body_panel_);
    } else {
      lv_group_focus_obj(help_dialog_);
    }
  }
}

void StandaloneUi::closeHelpDialog() {
  if (!help_open_ || !help_dialog_) {
    return;
  }

  help_open_ = false;
  lv_obj_add_flag(help_dialog_, LV_OBJ_FLAG_HIDDEN);
  focusCurrentZoneObject();
}

void StandaloneUi::clearLivePanel() {
  for (size_t i = 0; i < live_row_count_; i++) {
    if (live_rows_[i]) {
      lv_obj_del(live_rows_[i]);
      live_rows_[i] = nullptr;
    }
  }
  live_row_count_ = 0;
}

void StandaloneUi::appendLiveFeedLine(const char* text, ChatLineKind kind) {
  if (!text || text[0] == '\0') {
    return;
  }

  if (stored_live_count_ < kMaxStoredChatRows) {
    const size_t write_index = (stored_live_head_ + stored_live_count_) % kMaxStoredChatRows;
    strncpy(stored_live_[write_index].channel_name, "LIVE", sizeof(stored_live_[write_index].channel_name) - 1);
    stored_live_[write_index].channel_name[sizeof(stored_live_[write_index].channel_name) - 1] = '\0';
    strncpy(stored_live_[write_index].text, text, sizeof(stored_live_[write_index].text) - 1);
    stored_live_[write_index].text[sizeof(stored_live_[write_index].text) - 1] = '\0';
    stored_live_[write_index].kind = kind;
    stored_live_count_++;
  } else {
    strncpy(stored_live_[stored_live_head_].channel_name, "LIVE",
            sizeof(stored_live_[stored_live_head_].channel_name) - 1);
    stored_live_[stored_live_head_].channel_name[sizeof(stored_live_[stored_live_head_].channel_name) - 1] = '\0';
    strncpy(stored_live_[stored_live_head_].text, text, sizeof(stored_live_[stored_live_head_].text) - 1);
    stored_live_[stored_live_head_].text[sizeof(stored_live_[stored_live_head_].text) - 1] = '\0';
    stored_live_[stored_live_head_].kind = kind;
    stored_live_head_ = (stored_live_head_ + 1) % kMaxStoredChatRows;
  }

  if (!live_open_ || !live_body_panel_) {
    return;
  }

  if (stored_live_count_ == 1) {
    rebuildLiveDialog();
    return;
  }

  const bool at_bottom = lv_obj_get_scroll_bottom(live_body_panel_) <= 2;
  const lv_coord_t scroll_y = lv_obj_get_scroll_y(live_body_panel_);

  if (live_row_count_ >= kMaxChatRows) {
    lv_obj_del(live_rows_[0]);
    for (size_t i = 1; i < live_row_count_; i++) {
      live_rows_[i - 1] = live_rows_[i];
    }
    live_row_count_--;
  }

  lv_obj_t* row = lv_label_create(live_body_panel_);
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
  lv_obj_set_style_text_color(row, liveFeedTextColor(text, kind == ChatLineKind::Error), 0);
  live_rows_[live_row_count_++] = row;

  if (at_bottom) {
    lv_obj_scroll_to_y(live_body_panel_, LV_COORD_MAX, LV_ANIM_OFF);
  } else {
    lv_obj_scroll_to_y(live_body_panel_, scroll_y, LV_ANIM_OFF);
  }
}

void StandaloneUi::rebuildLiveDialog() {
  if (!live_body_panel_) {
    return;
  }

  clearLivePanel();

  if (stored_live_count_ == 0) {
    lv_obj_t* row = lv_label_create(live_body_panel_);
    lv_obj_set_width(row, LV_PCT(100));
    lv_label_set_long_mode(row, LV_LABEL_LONG_WRAP);
    lv_obj_add_style(row, &style_text_dim_, 0);
    lv_obj_set_style_text_font(row, chatPanelFont(), 0);
    lv_label_set_text(row, "(no mesh traffic yet)");
    live_rows_[live_row_count_++] = row;
    return;
  }

  const size_t first =
      (stored_live_count_ > kMaxChatRows) ? (stored_live_count_ - kMaxChatRows) : static_cast<size_t>(0);
  for (size_t i = first; i < stored_live_count_; i++) {
    const size_t idx = (stored_live_head_ + i) % kMaxStoredChatRows;
    lv_obj_t* row = lv_label_create(live_body_panel_);
    lv_obj_set_width(row, LV_PCT(100));
    lv_label_set_long_mode(row, LV_LABEL_LONG_WRAP);
    lv_obj_add_style(row, &style_text_main_, 0);
    lv_obj_set_style_text_font(row, chatPanelFont(), 0);

    switch (stored_live_[idx].kind) {
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

    lv_label_set_text(row, stored_live_[idx].text);
    lv_obj_set_style_text_color(row,
                                liveFeedTextColor(stored_live_[idx].text,
                                                  stored_live_[idx].kind == ChatLineKind::Error),
                                0);
    live_rows_[live_row_count_++] = row;
  }

  lv_obj_scroll_to_y(live_body_panel_, LV_COORD_MAX, LV_ANIM_OFF);
}

void StandaloneUi::openLiveDialog() {
  if (help_open_) {
    closeHelpDialog();
  }

  if (live_open_ || !live_dialog_) {
    return;
  }

  live_open_ = true;
  selected_shortcut_ = kShortcutLive;
  focus_zone_ = FocusZone::Shortcuts;
  refreshShortcutVisuals();
  lv_obj_clear_flag(live_dialog_, LV_OBJ_FLAG_HIDDEN);
  rebuildLiveDialog();
  lv_obj_move_foreground(live_dialog_);
  if (key_group_) {
    if (live_body_panel_) {
      lv_group_focus_obj(live_body_panel_);
    } else {
      lv_group_focus_obj(live_dialog_);
    }
  }
}

void StandaloneUi::closeLiveDialog() {
  if (!live_open_ || !live_dialog_) {
    return;
  }

  closeLiveUtilDialog();
  closeLiveSnrDialog();
  live_open_ = false;
  lv_obj_add_flag(live_dialog_, LV_OBJ_FLAG_HIDDEN);
  focusCurrentZoneObject();
}

void StandaloneUi::appendUtilizationSample(uint16_t util_percent, uint16_t packets_per_sec_x10) {
  util_latest_percent_ = util_percent;
  util_latest_pps_x10_ = packets_per_sec_x10;

  const size_t write_index = (util_history_count_ < kMetricChartPoints)
                                 ? ((util_history_head_ + util_history_count_) % kMetricChartPoints)
                                 : util_history_head_;
  util_history_[write_index] = util_percent;
  if (util_history_count_ < kMetricChartPoints) {
    util_history_count_++;
  } else {
    util_history_head_ = (util_history_head_ + 1) % kMetricChartPoints;
  }

  if (!live_util_open_ || !live_util_chart_ || !live_util_series_) {
    return;
  }

  lv_chart_set_next_value(live_util_chart_, live_util_series_, util_percent);
  lv_chart_refresh(live_util_chart_);
  if (live_util_stats_label_) {
    char stats[48] = {};
    snprintf(stats, sizeof(stats), "%u%%  (%u.%u pkt/s)", static_cast<unsigned>(util_latest_percent_),
             static_cast<unsigned>(util_latest_pps_x10_ / 10), static_cast<unsigned>(util_latest_pps_x10_ % 10));
    lv_label_set_text(live_util_stats_label_, stats);
  }
}

void StandaloneUi::appendSnrRssiSample(int16_t snr_db, int16_t rssi_dbm) {
  last_snr_db_ = snr_db;
  last_rssi_dbm_ = rssi_dbm;

  const size_t write_index = (radio_history_count_ < kMetricChartPoints)
                                 ? ((radio_history_head_ + radio_history_count_) % kMetricChartPoints)
                                 : radio_history_head_;
  snr_history_[write_index] = snr_db;
  rssi_history_[write_index] = rssi_dbm;
  if (radio_history_count_ < kMetricChartPoints) {
    radio_history_count_++;
  } else {
    radio_history_head_ = (radio_history_head_ + 1) % kMetricChartPoints;
  }

  if (!live_snr_open_ || !live_snr_chart_ || !live_snr_series_ || !live_rssi_series_) {
    return;
  }

  lv_chart_set_next_value(live_snr_chart_, live_snr_series_, snr_db);
  lv_chart_set_next_value(live_snr_chart_, live_rssi_series_, rssi_dbm);
  lv_chart_refresh(live_snr_chart_);
  if (live_snr_stats_label_) {
    char stats[64] = {};
    snprintf(stats, sizeof(stats), "SNR %d dB  RSSI %d dBm", static_cast<int>(last_snr_db_),
             static_cast<int>(last_rssi_dbm_));
    lv_label_set_text(live_snr_stats_label_, stats);
  }
}

void StandaloneUi::rebuildLiveUtilChart() {
  if (!live_util_chart_ || !live_util_series_) {
    return;
  }

  lv_chart_set_point_count(live_util_chart_, kMetricChartPoints);
  lv_chart_set_all_value(live_util_chart_, live_util_series_, 0);

  for (size_t i = 0; i < util_history_count_; i++) {
    const size_t idx = (util_history_head_ + i) % kMetricChartPoints;
    lv_chart_set_next_value(live_util_chart_, live_util_series_, util_history_[idx]);
  }
  lv_chart_refresh(live_util_chart_);

  if (live_util_stats_label_) {
    char stats[48] = {};
    snprintf(stats, sizeof(stats), "%u%%  (%u.%u pkt/s)", static_cast<unsigned>(util_latest_percent_),
             static_cast<unsigned>(util_latest_pps_x10_ / 10), static_cast<unsigned>(util_latest_pps_x10_ % 10));
    lv_label_set_text(live_util_stats_label_, stats);
  }
}

void StandaloneUi::rebuildLiveSnrChart() {
  if (!live_snr_chart_ || !live_snr_series_ || !live_rssi_series_) {
    return;
  }

  lv_chart_set_point_count(live_snr_chart_, kMetricChartPoints);
  lv_chart_set_all_value(live_snr_chart_, live_snr_series_, 0);
  lv_chart_set_all_value(live_snr_chart_, live_rssi_series_, -120);

  for (size_t i = 0; i < radio_history_count_; i++) {
    const size_t idx = (radio_history_head_ + i) % kMetricChartPoints;
    lv_chart_set_next_value(live_snr_chart_, live_snr_series_, snr_history_[idx]);
    lv_chart_set_next_value(live_snr_chart_, live_rssi_series_, rssi_history_[idx]);
  }
  lv_chart_refresh(live_snr_chart_);

  if (live_snr_stats_label_) {
    char stats[64] = {};
    snprintf(stats, sizeof(stats), "SNR %d dB  RSSI %d dBm", static_cast<int>(last_snr_db_),
             static_cast<int>(last_rssi_dbm_));
    lv_label_set_text(live_snr_stats_label_, stats);
  }
}

void StandaloneUi::openLiveUtilDialog() {
  if (!live_util_dialog_) {
    return;
  }

  closeLiveSnrDialog();
  live_util_open_ = true;
  lv_obj_clear_flag(live_util_dialog_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(live_util_dialog_);
  rebuildLiveUtilChart();
  if (key_group_ && live_util_chart_) {
    lv_group_focus_obj(live_util_chart_);
  }
}

void StandaloneUi::closeLiveUtilDialog() {
  if (!live_util_open_ || !live_util_dialog_) {
    return;
  }

  live_util_open_ = false;
  lv_obj_add_flag(live_util_dialog_, LV_OBJ_FLAG_HIDDEN);
  if (live_open_ && key_group_ && live_body_panel_) {
    lv_group_focus_obj(live_body_panel_);
  }
}

void StandaloneUi::openLiveSnrDialog() {
  if (!live_snr_dialog_) {
    return;
  }

  closeLiveUtilDialog();
  live_snr_open_ = true;
  lv_obj_clear_flag(live_snr_dialog_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(live_snr_dialog_);
  rebuildLiveSnrChart();
  if (key_group_ && live_snr_chart_) {
    lv_group_focus_obj(live_snr_chart_);
  }
}

void StandaloneUi::closeLiveSnrDialog() {
  if (!live_snr_open_ || !live_snr_dialog_) {
    return;
  }

  live_snr_open_ = false;
  lv_obj_add_flag(live_snr_dialog_, LV_OBJ_FLAG_HIDDEN);
  if (live_open_ && key_group_ && live_body_panel_) {
    lv_group_focus_obj(live_body_panel_);
  }
}

void StandaloneUi::sampleLiveMetrics(uint32_t now_ms) {
  if (!mesh_adapter_) {
    return;
  }

  mesh::MeshRadioStats stats{};
  mesh_adapter_->getRadioStats(&stats);

  if (last_util_sample_ms_ == 0) {
    last_util_sample_ms_ = now_ms;
    last_util_raw_rx_count_ = stats.rx_raw_count;
    return;
  }

  const uint32_t elapsed = now_ms - last_util_sample_ms_;
  if (elapsed < 1000) {
    return;
  }

  uint32_t delta_raw = 0;
  if (stats.rx_raw_count >= last_util_raw_rx_count_) {
    delta_raw = stats.rx_raw_count - last_util_raw_rx_count_;
  }

  const uint32_t packets_per_sec_x10 = (delta_raw * 10000UL) / elapsed;
  const uint32_t util_estimate = (delta_raw * 5000UL) / elapsed;
  const uint16_t util_percent = static_cast<uint16_t>(util_estimate > 100UL ? 100UL : util_estimate);

  appendUtilizationSample(util_percent, static_cast<uint16_t>(packets_per_sec_x10));
  last_util_sample_ms_ = now_ms;
  last_util_raw_rx_count_ = stats.rx_raw_count;
}

void StandaloneUi::openAdvertPopup(const char* text, bool is_error) {
  if (!advert_popup_ || !advert_popup_label_) {
    return;
  }

  lv_label_set_text(advert_popup_label_, (text && text[0] != '\0') ? text : "Advert");
  lv_obj_set_style_text_color(advert_popup_label_, is_error ? kColorErr : kColorAck, 0);
  lv_obj_clear_flag(advert_popup_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(advert_popup_);
  advert_popup_open_ = true;
  advert_popup_deadline_ms_ = millis() + kAdvertPopupAutoCloseMs;
}

void StandaloneUi::closeAdvertPopup() {
  if (!advert_popup_ || !advert_popup_open_) {
    return;
  }

  advert_popup_open_ = false;
  advert_popup_deadline_ms_ = 0;
  lv_obj_add_flag(advert_popup_, LV_OBJ_FLAG_HIDDEN);
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

void StandaloneUi::pushChannelHistoryLine(const char* channel_name, const char* text, ChatLineKind kind,
                                          uint32_t timestamp_epoch) {
  if (!channel_name || channel_name[0] == '\0' || !text || text[0] == '\0') {
    return;
  }

  const uint32_t stored_epoch = timestamp_epoch != 0 ? timestamp_epoch : nowEpochSecondsOrZero();

  if (stored_chat_count_ < kMaxStoredChatRows) {
    size_t write_index = (stored_chat_head_ + stored_chat_count_) % kMaxStoredChatRows;
    strncpy(stored_chat_[write_index].channel_name, channel_name, sizeof(stored_chat_[write_index].channel_name) - 1);
    stored_chat_[write_index].channel_name[sizeof(stored_chat_[write_index].channel_name) - 1] = '\0';
    strncpy(stored_chat_[write_index].text, text, sizeof(stored_chat_[write_index].text) - 1);
    stored_chat_[write_index].text[sizeof(stored_chat_[write_index].text) - 1] = '\0';
    stored_chat_[write_index].kind = kind;
    stored_chat_[write_index].timestamp_epoch = stored_epoch;
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
  stored_chat_[stored_chat_head_].timestamp_epoch = stored_epoch;
  stored_chat_head_ = (stored_chat_head_ + 1) % kMaxStoredChatRows;
  chat_history_dirty_ = true;
}

bool StandaloneUi::loadChatHistoryFromFs() {
  Preferences prefs;
  if (!prefs.begin(kUiPrefsNs, false)) {
    return false;
  }

  if (!prefs.isKey(kChatHistoryBlobKey)) {
    prefs.end();
    return false;
  }

  const size_t blob_len = prefs.getBytesLength(kChatHistoryBlobKey);
  if (blob_len < sizeof(PersistedChatLineLegacy)) {
    prefs.end();
    return false;
  }

  size_t record_size = 0;
  bool has_timestamps = false;
  if ((blob_len % sizeof(PersistedChatLine)) == 0) {
    record_size = sizeof(PersistedChatLine);
    has_timestamps = true;
  } else if ((blob_len % sizeof(PersistedChatLineLegacy)) == 0) {
    record_size = sizeof(PersistedChatLineLegacy);
    has_timestamps = false;
  } else {
    prefs.end();
    return false;
  }

  int count = static_cast<int>(blob_len / record_size);
  const uint16_t declared = prefs.getUShort(kChatHistoryCountKey, static_cast<uint16_t>(count));
  if (declared > 0 && declared < static_cast<uint16_t>(count)) {
    count = declared;
  }

  if (count > static_cast<int>(kMaxStoredChatRows)) {
    count = static_cast<int>(kMaxStoredChatRows);
  }

  const size_t to_read = static_cast<size_t>(count) * record_size;
  auto* persisted_buf = static_cast<uint8_t*>(malloc(to_read));
  if (!persisted_buf) {
    prefs.end();
    return false;
  }
  memset(persisted_buf, 0, to_read);
  const size_t got = prefs.getBytes(kChatHistoryBlobKey, persisted_buf, to_read);
  prefs.end();

  const int loaded_count = static_cast<int>(got / record_size);
  if (loaded_count <= 0) {
    free(persisted_buf);
    return false;
  }

  stored_chat_head_ = 0;
  stored_chat_count_ = 0;
  memset(stored_chat_, 0, sizeof(stored_chat_));

  for (int i = 0; i < loaded_count && stored_chat_count_ < kMaxStoredChatRows; i++) {
    PersistedChatLine persisted{};
    if (has_timestamps) {
      const auto* rec = reinterpret_cast<const PersistedChatLine*>(persisted_buf + (i * record_size));
      persisted = *rec;
    } else {
      const auto* legacy = reinterpret_cast<const PersistedChatLineLegacy*>(persisted_buf + (i * record_size));
      strncpy(persisted.channel_name, legacy->channel_name, sizeof(persisted.channel_name) - 1);
      persisted.channel_name[sizeof(persisted.channel_name) - 1] = '\0';
      strncpy(persisted.text, legacy->text, sizeof(persisted.text) - 1);
      persisted.text[sizeof(persisted.text) - 1] = '\0';
      persisted.kind = legacy->kind;
      persisted.timestamp_epoch = 0;
    }

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
    stored_chat_[write_index].timestamp_epoch = persisted.timestamp_epoch;
    stored_chat_count_++;
  }

  free(persisted_buf);

  chat_history_dirty_ = false;
  return stored_chat_count_ > 0;
}

bool StandaloneUi::saveChatHistoryToFs() {
  Preferences prefs;
  if (!prefs.begin(kUiPrefsNs, false)) {
    return false;
  }

  const size_t count = stored_chat_count_ > kMaxStoredChatRows ? kMaxStoredChatRows : stored_chat_count_;
  const size_t blob_len = count * sizeof(PersistedChatLine);
  PersistedChatLine* persisted_buf = nullptr;
  if (count > 0) {
    persisted_buf = static_cast<PersistedChatLine*>(malloc(blob_len));
    if (!persisted_buf) {
      prefs.end();
      return false;
    }
    memset(persisted_buf, 0, blob_len);
  }

  for (size_t i = 0; i < count; i++) {
    size_t idx = (stored_chat_head_ + i) % kMaxStoredChatRows;
    PersistedChatLine& persisted = persisted_buf[i];
    strncpy(persisted.channel_name, stored_chat_[idx].channel_name, sizeof(persisted.channel_name) - 1);
    persisted.channel_name[sizeof(persisted.channel_name) - 1] = '\0';
    strncpy(persisted.text, stored_chat_[idx].text, sizeof(persisted.text) - 1);
    persisted.text[sizeof(persisted.text) - 1] = '\0';
    persisted.kind = static_cast<uint8_t>(stored_chat_[idx].kind);
    persisted.timestamp_epoch = stored_chat_[idx].timestamp_epoch;
  }

  bool ok = true;
  if (count > 0) {
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
  if (persisted_buf) {
    free(persisted_buf);
  }
  if (!ok) {
    return false;
  }

  chat_history_dirty_ = false;
  return true;
}

bool StandaloneUi::pruneStoredDmByRetention(uint32_t now_epoch) {
  if (stored_dm_count_ == 0 || now_epoch == 0) {
    return false;
  }

  const uint32_t cutoff = now_epoch > kDmRetentionSeconds ? (now_epoch - kDmRetentionSeconds) : 0;
  const size_t temp_len = stored_dm_count_ * sizeof(StoredDmLine);
  auto* retained = static_cast<StoredDmLine*>(malloc(temp_len));
  if (!retained) {
    return false;
  }
  memset(retained, 0, temp_len);
  size_t retained_count = 0;

  for (size_t i = 0; i < stored_dm_count_ && retained_count < kMaxStoredChatRows; i++) {
    const size_t idx = (stored_dm_head_ + i) % kMaxStoredChatRows;
    const StoredDmLine& line = stored_dm_[idx];
    if (line.contact_name[0] == '\0' || line.text[0] == '\0') {
      continue;
    }

    // Keep entries without a valid epoch and all entries newer than retention cutoff.
    if (line.timestamp_epoch != 0 && line.timestamp_epoch < cutoff) {
      continue;
    }

    retained[retained_count++] = line;
  }

  if (retained_count == stored_dm_count_) {
    free(retained);
    return false;
  }

  memset(stored_dm_, 0, sizeof(stored_dm_));
  stored_dm_head_ = 0;
  stored_dm_count_ = retained_count;
  for (size_t i = 0; i < retained_count; i++) {
    stored_dm_[i] = retained[i];
  }
  free(retained);
  dm_history_dirty_ = true;
  return true;
}

bool StandaloneUi::loadDmHistoryFromFs() {
  Preferences prefs;
  if (!prefs.begin(kUiPrefsNs, false)) {
    return false;
  }

  if (!prefs.isKey(kDmHistoryBlobKey)) {
    prefs.end();
    return false;
  }

  const size_t blob_len = prefs.getBytesLength(kDmHistoryBlobKey);
  if (blob_len < sizeof(PersistedDmLine)) {
    prefs.end();
    return false;
  }

  int count = static_cast<int>(blob_len / sizeof(PersistedDmLine));
  const uint16_t declared = prefs.getUShort(kDmHistoryCountKey, static_cast<uint16_t>(count));
  if (declared > 0 && declared < static_cast<uint16_t>(count)) {
    count = declared;
  }

  if (count > static_cast<int>(kMaxStoredChatRows)) {
    count = static_cast<int>(kMaxStoredChatRows);
  }

  const size_t to_read = static_cast<size_t>(count) * sizeof(PersistedDmLine);
  auto* persisted_buf = static_cast<PersistedDmLine*>(malloc(to_read));
  if (!persisted_buf) {
    prefs.end();
    return false;
  }
  memset(persisted_buf, 0, to_read);
  const size_t got = prefs.getBytes(kDmHistoryBlobKey, persisted_buf, to_read);
  prefs.end();

  const int loaded_count = static_cast<int>(got / sizeof(PersistedDmLine));
  if (loaded_count <= 0) {
    free(persisted_buf);
    return false;
  }

  memset(stored_dm_, 0, sizeof(stored_dm_));
  stored_dm_head_ = 0;
  stored_dm_count_ = 0;

  for (int i = 0; i < loaded_count && stored_dm_count_ < kMaxStoredChatRows; i++) {
    const PersistedDmLine& persisted = persisted_buf[i];
    if (persisted.contact_name[0] == '\0' || persisted.text[0] == '\0') {
      continue;
    }

    size_t write_index = (stored_dm_head_ + stored_dm_count_) % kMaxStoredChatRows;
    StoredDmLine& out = stored_dm_[write_index];
    strncpy(out.contact_name, persisted.contact_name, sizeof(out.contact_name) - 1);
    out.contact_name[sizeof(out.contact_name) - 1] = '\0';
    strncpy(out.contact_key, persisted.contact_key, sizeof(out.contact_key) - 1);
    out.contact_key[sizeof(out.contact_key) - 1] = '\0';
    strncpy(out.text, persisted.text, sizeof(out.text) - 1);
    out.text[sizeof(out.text) - 1] = '\0';
    out.kind = (persisted.kind <= static_cast<uint8_t>(ChatLineKind::Error))
                   ? static_cast<ChatLineKind>(persisted.kind)
                   : ChatLineKind::Normal;
    out.timestamp_epoch = persisted.timestamp_epoch;
    stored_dm_count_++;
  }

  free(persisted_buf);

  const bool pruned = pruneStoredDmByRetention(nowEpochSecondsOrZero());
  if (!pruned) {
    dm_history_dirty_ = false;
  }
  return stored_dm_count_ > 0;
}

bool StandaloneUi::saveDmHistoryToFs() {
  Preferences prefs;
  if (!prefs.begin(kUiPrefsNs, false)) {
    return false;
  }

  const size_t count = stored_dm_count_ > kMaxStoredChatRows ? kMaxStoredChatRows : stored_dm_count_;
  const size_t blob_len = count * sizeof(PersistedDmLine);
  PersistedDmLine* persisted_buf = nullptr;
  if (count > 0) {
    persisted_buf = static_cast<PersistedDmLine*>(malloc(blob_len));
    if (!persisted_buf) {
      prefs.end();
      return false;
    }
    memset(persisted_buf, 0, blob_len);
  }

  for (size_t i = 0; i < count; i++) {
    size_t idx = (stored_dm_head_ + i) % kMaxStoredChatRows;
    PersistedDmLine& persisted = persisted_buf[i];
    strncpy(persisted.contact_name, stored_dm_[idx].contact_name, sizeof(persisted.contact_name) - 1);
    persisted.contact_name[sizeof(persisted.contact_name) - 1] = '\0';
    strncpy(persisted.contact_key, stored_dm_[idx].contact_key, sizeof(persisted.contact_key) - 1);
    persisted.contact_key[sizeof(persisted.contact_key) - 1] = '\0';
    strncpy(persisted.text, stored_dm_[idx].text, sizeof(persisted.text) - 1);
    persisted.text[sizeof(persisted.text) - 1] = '\0';
    persisted.kind = static_cast<uint8_t>(stored_dm_[idx].kind);
    persisted.timestamp_epoch = stored_dm_[idx].timestamp_epoch;
  }

  bool ok = true;
  if (count > 0) {
    const size_t wrote = prefs.putBytes(kDmHistoryBlobKey, persisted_buf, blob_len);
    if (wrote != blob_len) {
      ok = false;
    }
  } else if (prefs.isKey(kDmHistoryBlobKey)) {
    if (!prefs.remove(kDmHistoryBlobKey)) {
      ok = false;
    }
  }

  prefs.putUShort(kDmHistoryCountKey, static_cast<uint16_t>(count));
  prefs.end();
  if (persisted_buf) {
    free(persisted_buf);
  }
  if (!ok) {
    return false;
  }

  dm_history_dirty_ = false;
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
  chat_last_date_key_ = 0;
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
      appendChatLine(stored_chat_[idx].text, stored_chat_[idx].kind, stored_chat_[idx].timestamp_epoch);
    }
  }
}

void StandaloneUi::handleKey(uint32_t key) {
  // Self-heal stale modal flags so hidden/deleted dialogs cannot swallow input globally.
  if (compose_open_ && (!compose_dialog_ || lv_obj_has_flag(compose_dialog_, LV_OBJ_FLAG_HIDDEN))) {
    compose_open_ = false;
    compose_dm_mode_ = false;
    compose_return_to_dm_ = false;
  }
  if (cfg_open_ && (!cfg_dialog_ || lv_obj_has_flag(cfg_dialog_, LV_OBJ_FLAG_HIDDEN))) {
    cfg_open_ = false;
  }
  if (help_open_ && (!help_dialog_ || lv_obj_has_flag(help_dialog_, LV_OBJ_FLAG_HIDDEN))) {
    help_open_ = false;
  }
  if (live_open_ && (!live_dialog_ || lv_obj_has_flag(live_dialog_, LV_OBJ_FLAG_HIDDEN))) {
    live_open_ = false;
  }
  if (advert_popup_open_ && (!advert_popup_ || lv_obj_has_flag(advert_popup_, LV_OBJ_FLAG_HIDDEN))) {
    advert_popup_open_ = false;
    advert_popup_deadline_ms_ = 0;
  }
  if (contacts_open_ && !dm_open_ && (!contacts_dialog_ || lv_obj_has_flag(contacts_dialog_, LV_OBJ_FLAG_HIDDEN))) {
    contacts_open_ = false;
    contacts_nav_focused_ = false;
    contacts_dm_open_ = false;
  }
  if (dm_open_ && !compose_open_ && (!dm_dialog_ || lv_obj_has_flag(dm_dialog_, LV_OBJ_FLAG_HIDDEN))) {
    dm_open_ = false;
  }
  // Keyboard visibility is managed explicitly by compose open/close flows.

  uint32_t norm_key = key;
  const bool is_lvgl_special =
      (norm_key == LV_KEY_UP || norm_key == LV_KEY_DOWN || norm_key == LV_KEY_LEFT || norm_key == LV_KEY_RIGHT ||
       norm_key == LV_KEY_ENTER || norm_key == LV_KEY_ESC || norm_key == LV_KEY_BACKSPACE ||
       norm_key == LV_KEY_DEL || norm_key == LV_KEY_HOME || norm_key == LV_KEY_END || norm_key == LV_KEY_NEXT ||
       norm_key == LV_KEY_PREV);
  if (kKeyboardNavEnabled && !is_lvgl_special && norm_key >= 1 && norm_key <= 26) {
    // Support Ctrl+A..Ctrl+Z style values for shortcut keys without breaking LVGL key constants.
    norm_key = static_cast<uint32_t>('a' + (norm_key - 1));
  }
  if (kKeyboardNavEnabled && norm_key >= 'A' && norm_key <= 'Z') {
    norm_key = norm_key - 'A' + 'a';
  }

#if defined(DEVICE_CARDPUTER_LORA_HAT)
  norm_key = remapCardputerNavKey(norm_key, !compose_open_);
#endif

#if PLUMERIA_KEY_DEBUG
  if (false) Serial.printf("[KEYUI] handleKey raw=%lu norm=%lu cfg=%d compose=%d dropdown=%d focus=%u\n",
                static_cast<unsigned long>(key), static_cast<unsigned long>(norm_key), cfg_open_ ? 1 : 0,
                compose_open_ ? 1 : 0, channel_dropdown_open_ ? 1 : 0, static_cast<unsigned>(focus_zone_));
#endif

  const uint32_t now = millis();
  const bool is_escape = (norm_key == LV_KEY_ESC || norm_key == 27 || norm_key == 8);
  lv_obj_t* focused = key_group_ ? lv_group_get_focused(key_group_) : nullptr;
  const bool chat_focused = (focused == chat_panel_);

#if defined(DEVICE_CARDPUTER_LORA_HAT)
  if (compose_open_ && (key == '`' || key == '~')) {
    handleComposeKey(LV_KEY_ESC);
    return;
  }
#endif

  if (compose_open_) {
    handleComposeKey(key);
    return;
  }

  // Global shortcut: open/close Help from any screen except compose.
  if (kKeyboardNavEnabled && norm_key == 'h') {
    if (help_open_) {
      closeHelpDialog();
    } else {
      openHelpDialog();
    }
    return;
  }

  // Global shortcut: open/close Live feed from any screen except compose.
  if (kKeyboardNavEnabled && norm_key == 'l') {
    if (live_open_) {
      closeLiveDialog();
    } else {
      openLiveDialog();
    }
    return;
  }

  if (live_open_) {
    if (kKeyboardNavEnabled && norm_key == 'c') {
      stored_live_head_ = 0;
      stored_live_count_ = 0;
      rebuildLiveDialog();
      return;
    }
    if (kKeyboardNavEnabled && norm_key == 'u') {
      if (live_util_open_) {
        closeLiveUtilDialog();
      } else {
        openLiveUtilDialog();
      }
      return;
    }
    if (kKeyboardNavEnabled && norm_key == 's') {
      if (live_snr_open_) {
        closeLiveSnrDialog();
      } else {
        openLiveSnrDialog();
      }
      return;
    }
    if (live_util_open_ || live_snr_open_) {
      if (norm_key == LV_KEY_BACKSPACE || norm_key == 8 || norm_key == 127 || norm_key == LV_KEY_ESC) {
        closeLiveUtilDialog();
        closeLiveSnrDialog();
      }
      return;
    }
    if (focused == live_close_btn_ &&
        (norm_key == LV_KEY_ENTER || norm_key == '\n' || norm_key == '\r' || norm_key == LV_KEY_ESC ||
         norm_key == LV_KEY_BACKSPACE || norm_key == 8 || norm_key == 127)) {
      closeLiveDialog();
      return;
    }
    if (norm_key == LV_KEY_UP || (kKeyboardNavEnabled && norm_key == 'j')) {
      if (live_body_panel_) {
        lv_obj_scroll_by(live_body_panel_, 0, -kMsgScrollStep, LV_ANIM_OFF);
      }
      return;
    }
    if (norm_key == LV_KEY_DOWN || (kKeyboardNavEnabled && norm_key == 'k')) {
      if (live_body_panel_) {
        lv_obj_scroll_by(live_body_panel_, 0, kMsgScrollStep, LV_ANIM_OFF);
      }
      return;
    }
    if (norm_key == LV_KEY_BACKSPACE || norm_key == 8 || norm_key == 127 || norm_key == LV_KEY_ESC) {
      closeLiveDialog();
      return;
    }
    return;
  }

  if (help_open_) {
    if (focused == help_close_btn_ &&
        (norm_key == LV_KEY_ENTER || norm_key == '\n' || norm_key == '\r' || norm_key == LV_KEY_ESC ||
         norm_key == LV_KEY_BACKSPACE || norm_key == 8 || norm_key == 127)) {
      closeHelpDialog();
      return;
    }
    if (norm_key == LV_KEY_UP || (kKeyboardNavEnabled && norm_key == 'j')) {
      if (help_body_panel_) {
        lv_obj_scroll_by(help_body_panel_, 0, -kMsgScrollStep, LV_ANIM_OFF);
      }
      return;
    }
    if (norm_key == LV_KEY_DOWN || (kKeyboardNavEnabled && norm_key == 'k')) {
      if (help_body_panel_) {
        lv_obj_scroll_by(help_body_panel_, 0, kMsgScrollStep, LV_ANIM_OFF);
      }
      return;
    }
    if (norm_key == LV_KEY_BACKSPACE || norm_key == 8 || norm_key == 127 || norm_key == LV_KEY_ESC ||
        norm_key == LV_KEY_ENTER || norm_key == '\n' || norm_key == '\r') {
      closeHelpDialog();
      return;
    }
    return;
  }

  if (advert_popup_open_) {
    if (norm_key == LV_KEY_BACKSPACE || norm_key == 8 || norm_key == 127 || norm_key == LV_KEY_ESC) {
      closeAdvertPopup();
      return;
    }
    return;
  }

  // Global shortcut: open CFG from any non-compose screen.
  if (!cfg_open_ && !contacts_open_ && !dm_open_ && kKeyboardNavEnabled && norm_key == 'c') {
    selected_shortcut_ = kShortcutCfg;
    setFocusZone(FocusZone::Shortcuts);
    triggerShortcut(kShortcutCfg);
    return;
  }

  // Global shortcut: open Contacts from any non-compose screen.
  if (!cfg_open_ && !contacts_open_ && !dm_open_ && kKeyboardNavEnabled && norm_key == 'o') {
    selected_shortcut_ = kShortcutContacts;
    setFocusZone(FocusZone::Shortcuts);
    triggerShortcut(kShortcutContacts);
    return;
  }

  // Global shortcut: open compose from any non-compose screen.
  if (!cfg_open_ && !contacts_open_ && !dm_open_ && kKeyboardNavEnabled && norm_key == 'm') {
    setFocusZone(FocusZone::Chat);
    openComposeDialog();
    return;
  }

  // Global shortcut: broadcast advert zero-hop from main screen only.
  if (!cfg_open_ && !contacts_open_ && !dm_open_ && kKeyboardNavEnabled && norm_key == 'z') {
    triggerAdvertZeroHop();
    return;
  }

  // Global shortcut: broadcast advert flood from main screen only.
  if (!cfg_open_ && !contacts_open_ && !dm_open_ && kKeyboardNavEnabled && norm_key == 'f') {
    triggerAdvertFlood();
    return;
  }

  if (cfg_open_) {
    lv_obj_t* focused = key_group_ ? lv_group_get_focused(key_group_) : nullptr;
    if (focused == cfg_close_btn_ &&
        (norm_key == LV_KEY_ENTER || norm_key == '\n' || norm_key == '\r' || norm_key == LV_KEY_ESC ||
         norm_key == LV_KEY_BACKSPACE || norm_key == 8 || norm_key == 127)) {
      closeCfgDialog(true);
      return;
    }

    if (norm_key == LV_KEY_UP || (kKeyboardNavEnabled && norm_key == 'j')) {
      moveCfgSelection(-1);
      return;
    }
    if (norm_key == LV_KEY_DOWN || (kKeyboardNavEnabled && norm_key == 'k')) {
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

  if (dm_open_) {
    if (focused == dm_close_btn_ &&
        (norm_key == LV_KEY_ENTER || norm_key == '\n' || norm_key == '\r' || norm_key == LV_KEY_ESC ||
         norm_key == LV_KEY_BACKSPACE || norm_key == 8 || norm_key == 127)) {
      closeDmDialog(false);
      return;
    }
    if (focused == dm_new_btn_ && (norm_key == LV_KEY_ENTER || norm_key == '\n' || norm_key == '\r')) {
      compose_dm_mode_ = true;
      compose_return_to_dm_ = true;
      strncpy(compose_target_dm_pubkey_, dm_active_key_, sizeof(compose_target_dm_pubkey_) - 1);
      compose_target_dm_pubkey_[sizeof(compose_target_dm_pubkey_) - 1] = '\0';
      strncpy(compose_target_channel_, dm_active_name_, sizeof(compose_target_channel_) - 1);
      compose_target_channel_[sizeof(compose_target_channel_) - 1] = '\0';
      openComposeDialog();
      return;
    }

    if (norm_key == LV_KEY_BACKSPACE || norm_key == 8 || norm_key == 127) {
      closeDmDialog(false);
      return;
    }
    if (norm_key == LV_KEY_ENTER || norm_key == '\n' || norm_key == '\r') {
      compose_dm_mode_ = true;
      compose_return_to_dm_ = true;
      strncpy(compose_target_dm_pubkey_, dm_active_key_, sizeof(compose_target_dm_pubkey_) - 1);
      compose_target_dm_pubkey_[sizeof(compose_target_dm_pubkey_) - 1] = '\0';
      strncpy(compose_target_channel_, dm_active_name_, sizeof(compose_target_channel_) - 1);
      compose_target_channel_[sizeof(compose_target_channel_) - 1] = '\0';
      openComposeDialog();
      return;
    }
    if (norm_key == LV_KEY_UP || (kKeyboardNavEnabled && norm_key == 'j')) {
      lv_obj_scroll_by(dm_panel_, 0, -kMsgScrollStep, LV_ANIM_OFF);
      return;
    }
    if (norm_key == LV_KEY_DOWN || (kKeyboardNavEnabled && norm_key == 'k')) {
      lv_obj_scroll_by(dm_panel_, 0, kMsgScrollStep, LV_ANIM_OFF);
      return;
    }
    return;
  }

  if (contacts_open_) {
    const uint8_t contacts_option_count = clampOptionCount(contacts_count_, kChannelCount);

    if (kKeyboardNavEnabled && norm_key == 'f') {
      activateContactsAction(0);
      return;
    }
    if (kKeyboardNavEnabled && (norm_key == 'd' || norm_key == 'm')) {
      activateContactsAction(1);
      return;
    }
#if defined(DEVICE_HELTEC_V4_EXPANSION)
    if (kKeyboardNavEnabled && norm_key == 'c') {
      activateContactsAction(2);
      return;
    }
#endif

    if (norm_key == LV_KEY_ESC) {
      closeContactsDialog(true);
      return;
    }

    if (norm_key == LV_KEY_BACKSPACE || norm_key == 8 || norm_key == 127) {
      if (channel_dropdown_open_) {
        closeChannelDropdown(false);
        contacts_nav_focused_ = true;
        refreshContactsDialog(false);
        focusCurrentZoneObject();
        return;
      }
      if (!contacts_nav_focused_) {
        contacts_nav_focused_ = true;
        contacts_dm_open_ = false;
        refreshContactsDialog(false);
        focusCurrentZoneObject();
        return;
      }
      closeContactsDialog(true);
      return;
    }

    if (channel_dropdown_open_) {
      if (norm_key == LV_KEY_UP || (kKeyboardNavEnabled && norm_key == 'j')) {
        moveDropdownHighlight(-1);
        return;
      }
      if (norm_key == LV_KEY_DOWN || (kKeyboardNavEnabled && norm_key == 'k')) {
        moveDropdownHighlight(1);
        return;
      }
      if (norm_key == LV_KEY_ENTER || norm_key == '\n' || norm_key == '\r') {
#if defined(DEVICE_TLORA_PAGER_TFT)
        if (contacts_dropdown_guard_until_ms_ != 0 && now < contacts_dropdown_guard_until_ms_) {
          return;
        }
#endif
        if (now - last_dropdown_open_ms_ < kContactsDropdownEnterGuardMs) {
          return;
        }
        if (now - last_selector_action_ms_ < kContactsDropdownEnterGuardMs) {
          return;
        }
        last_selector_action_ms_ = now;
        contacts_dropdown_guard_until_ms_ = 0;
        if (contacts_option_count > 0 && dropdown_highlight_channel_ < contacts_option_count) {
          contacts_selected_index_ = dropdown_highlight_channel_;
        }
        contacts_nav_focused_ = false;
        contacts_dm_open_ = false;
        closeChannelDropdown(true);
        refreshContactsDialog(false);
        focusCurrentZoneObject();
        return;
      }
      if (norm_key == LV_KEY_LEFT || norm_key == LV_KEY_RIGHT) {
        closeChannelDropdown(false);
        contacts_nav_focused_ = true;
        refreshContactsDialog(false);
        focusCurrentZoneObject();
        return;
      }
      return;
    }

    if (contacts_nav_focused_) {
      if (norm_key == LV_KEY_ENTER || norm_key == '\n' || norm_key == '\r' || norm_key == LV_KEY_RIGHT) {
        if (contacts_count_ > 0) {
          if (now - last_selector_action_ms_ < kNavDebounceMs) {
            return;
          }
          last_selector_action_ms_ = now;
          contacts_dropdown_guard_until_ms_ = now + kContactsDropdownEnterGuardMs;
          openChannelDropdown();
        }
        return;
      }
      if (norm_key == LV_KEY_UP || (kKeyboardNavEnabled && norm_key == 'j')) {
        moveContactsSelection(-1);
        return;
      }
      if (norm_key == LV_KEY_DOWN || (kKeyboardNavEnabled && norm_key == 'k')) {
        moveContactsSelection(1);
        return;
      }
      return;
    }

    if (contacts_dm_open_) {
      if (norm_key == LV_KEY_ENTER || norm_key == '\n' || norm_key == '\r') {
        startComposeForSelectedContact();
        return;
      }
      if (norm_key == LV_KEY_UP || (kKeyboardNavEnabled && norm_key == 'j')) {
        if (contacts_dm_panel_) {
          lv_obj_scroll_by(contacts_dm_panel_, 0, -kMsgScrollStep, LV_ANIM_OFF);
        }
        return;
      }
      if (norm_key == LV_KEY_DOWN || (kKeyboardNavEnabled && norm_key == 'k')) {
        if (contacts_dm_panel_) {
          lv_obj_scroll_by(contacts_dm_panel_, 0, kMsgScrollStep, LV_ANIM_OFF);
        }
        return;
      }
      if (norm_key == LV_KEY_LEFT) {
        contacts_nav_focused_ = true;
        refreshContactsDialog(false);
        focusCurrentZoneObject();
        return;
      }
      return;
    }

    if (norm_key == LV_KEY_LEFT) {
      contacts_nav_focused_ = true;
      refreshContactsDialog(false);
      focusCurrentZoneObject();
      return;
    }
    if (norm_key == LV_KEY_RIGHT) {
      contacts_dm_open_ = true;
      refreshContactsDialog(false);
      focusCurrentZoneObject();
      return;
    }
    if (norm_key == LV_KEY_UP || (kKeyboardNavEnabled && norm_key == 'j')) {
      if (contacts_action_index_ == 0) {
        contacts_action_index_ = kContactActionCount - 1;
      } else {
        contacts_action_index_--;
      }
      refreshContactsDialog(false);
      focusCurrentZoneObject();
      return;
    }
    if (norm_key == LV_KEY_DOWN || (kKeyboardNavEnabled && norm_key == 'k')) {
      contacts_action_index_ = static_cast<uint8_t>((contacts_action_index_ + 1) % kContactActionCount);
      refreshContactsDialog(false);
      focusCurrentZoneObject();
      return;
    }
    if (norm_key == LV_KEY_ENTER || norm_key == '\n' || norm_key == '\r') {
      activateContactsAction(contacts_action_index_);
      return;
    }
    return;
  }

  if (channel_dropdown_open_) {
    if (norm_key == LV_KEY_UP || (kKeyboardNavEnabled && norm_key == 'j')) {
      moveDropdownHighlight(-1);
      return;
    }
    if (norm_key == LV_KEY_DOWN || (kKeyboardNavEnabled && norm_key == 'k')) {
      moveDropdownHighlight(1);
      return;
    }
    if (norm_key == LV_KEY_ENTER) {
      if (now - last_dropdown_open_ms_ < kNavDebounceMs) {
        return;
      }
      if (now - last_selector_action_ms_ < kNavDebounceMs) {
        return;
      }
      last_selector_action_ms_ = now;
      if (contacts_open_) {
        const uint8_t contacts_option_count = clampOptionCount(contacts_count_, kChannelCount);
        if (contacts_option_count > 0 && dropdown_highlight_channel_ < contacts_option_count) {
          contacts_selected_index_ = dropdown_highlight_channel_;
        }
        contacts_nav_focused_ = false;
        contacts_dm_open_ = false;
        refreshContactsDialog(false);
      } else {
        selectChannel(dropdown_highlight_channel_, true);
      }
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

  if (focused == chat_advz_btn_ && (norm_key == LV_KEY_ENTER || norm_key == '\n' || norm_key == '\r')) {
    triggerAdvertZeroHop();
    return;
  }

  if (focused == chat_advf_btn_ && (norm_key == LV_KEY_ENTER || norm_key == '\n' || norm_key == '\r')) {
    triggerAdvertFlood();
    return;
  }

  if (focused == chat_new_btn_ && (norm_key == LV_KEY_ENTER || norm_key == '\n' || norm_key == '\r')) {
    compose_dm_mode_ = false;
    compose_return_to_dm_ = false;
    setFocusZone(FocusZone::Chat);
    openComposeDialog();
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
      if (kKeyboardNavEnabled && (focus_zone_ == FocusZone::Chat || chat_focused)) {
        scrollChatDown();
      }
      return;
    case 'k':
      if (kKeyboardNavEnabled && (focus_zone_ == FocusZone::Chat || chat_focused)) {
        scrollChatUp();
      }
      return;
    case 'c':
      return;
    case 'n':
      if (!kKeyboardNavEnabled) {
        return;
      }
      selected_shortcut_ = kShortcutContacts;
      setFocusZone(FocusZone::Shortcuts);
      triggerShortcut(kShortcutContacts);
      return;
    default:
      if (is_escape && focus_zone_ != FocusZone::Selector) {
        setFocusZone(FocusZone::Selector);
      }
      return;
  }
}

void StandaloneUi::handleClick(lv_obj_t* target) {
  // Self-heal stale modal flags so hidden/deleted dialogs cannot swallow taps globally.
  if (compose_open_ && (!compose_dialog_ || lv_obj_has_flag(compose_dialog_, LV_OBJ_FLAG_HIDDEN))) {
    compose_open_ = false;
    compose_dm_mode_ = false;
    compose_return_to_dm_ = false;
  }
  if (cfg_open_ && (!cfg_dialog_ || lv_obj_has_flag(cfg_dialog_, LV_OBJ_FLAG_HIDDEN))) {
    cfg_open_ = false;
  }
  if (help_open_ && (!help_dialog_ || lv_obj_has_flag(help_dialog_, LV_OBJ_FLAG_HIDDEN))) {
    help_open_ = false;
  }
  if (live_open_ && (!live_dialog_ || lv_obj_has_flag(live_dialog_, LV_OBJ_FLAG_HIDDEN))) {
    live_open_ = false;
  }
  if (advert_popup_open_ && (!advert_popup_ || lv_obj_has_flag(advert_popup_, LV_OBJ_FLAG_HIDDEN))) {
    advert_popup_open_ = false;
    advert_popup_deadline_ms_ = 0;
  }
  if (contacts_open_ && !dm_open_ && (!contacts_dialog_ || lv_obj_has_flag(contacts_dialog_, LV_OBJ_FLAG_HIDDEN))) {
    contacts_open_ = false;
    contacts_nav_focused_ = false;
    contacts_dm_open_ = false;
  }
  if (dm_open_ && !compose_open_ && (!dm_dialog_ || lv_obj_has_flag(dm_dialog_, LV_OBJ_FLAG_HIDDEN))) {
    dm_open_ = false;
  }
  // Keyboard visibility is managed explicitly by compose open/close flows.

  if (help_open_) {
    if ((target == help_close_btn_) || (target == help_close_label_) || hasAncestor(target, help_close_btn_)) {
      closeHelpDialog();
    }
    return;
  }

  if (live_open_) {
    if ((target == live_close_btn_) || (target == live_close_label_) || hasAncestor(target, live_close_btn_)) {
      closeLiveDialog();
    }
    return;
  }

  if (advert_popup_open_) {
    if ((target == advert_popup_) || (target == advert_popup_label_) || hasAncestor(target, advert_popup_)) {
      closeAdvertPopup();
      return;
    }
    closeAdvertPopup();
  }

  if (cfg_open_) {
    if ((target == cfg_close_btn_) || (target == cfg_close_label_) || hasAncestor(target, cfg_close_btn_)) {
      closeCfgDialog(true);
      return;
    }

    for (uint8_t i = 0; i < kCfgRowCount; i++) {
      if (target == cfg_rows_[i] || target == cfg_row_labels_[i]) {
        cfg_selected_row_ = i;
#if !defined(DEVICE_HELTEC_V4_EXPANSION)
        if (cfg_selected_row_ != kCfgRowImportConfig) {
          cfg_import_confirm_armed_ = false;
        }
        if (cfg_selected_row_ != kCfgRowDeleteConfig) {
          cfg_delete_confirm_armed_ = false;
        }
#endif
        refreshCfgDialog();
        if (kTouchDirectActivate) {
          activateCfgSelection();
        }
        return;
      }
    }
    if (hasAncestor(target, cfg_dialog_)) {
      return;
    }
  }

  if (compose_open_) {
    lv_indev_t* indev = lv_indev_get_act();
    if (indev && compose_keyboard_ && !lv_obj_has_flag(compose_keyboard_, LV_OBJ_FLAG_HIDDEN)) {
      lv_point_t pt{};
      lv_indev_get_point(indev, &pt);
      lv_area_t kb_area{};
      lv_obj_get_coords(compose_keyboard_, &kb_area);
      if (pt.x >= kb_area.x1 && pt.x <= kb_area.x2 && pt.y >= kb_area.y1 && pt.y <= kb_area.y2) {
        // Coordinate fallback: treat taps in keyboard bounds as keyboard interaction.
        return;
      }
    }
    if (compose_keyboard_ && hasAncestor(target, compose_keyboard_)) {
      // Let LVGL keyboard process key taps.
      return;
    }
    const uint32_t now = millis();
    if (compose_opened_ms_ != 0 && static_cast<uint32_t>(now - compose_opened_ms_) < kComposeOpenTapIgnoreMs) {
      // Ignore follow-on click events from the same gesture that opened compose.
      return;
    }
    if (hasAncestor(target, compose_dialog_)) {
      if (!kUseOnscreenKeyboard && compose_input_ && key_group_) {
        lv_group_focus_obj(compose_input_);
      }
      return;
    }
    if (kUseOnscreenKeyboard) {
      // Ignore outside taps while compose is open to avoid stale underlying targets.
      return;
    }
    // Tap outside compose closes modal instead of trapping the UI.
    closeComposeDialog(true);
    return;
  }

  if (dm_open_) {
    lv_point_t trace_pt{};
    int trace_x = -1;
    int trace_y = -1;
    lv_indev_t* trace_indev = lv_indev_get_act();
    if (trace_indev) {
      lv_indev_get_point(trace_indev, &trace_pt);
      trace_x = trace_pt.x;
      trace_y = trace_pt.y;
    }
    CTS_TRACE("dm_click target=%p pt=%d,%d", static_cast<void*>(target), trace_x, trace_y);

    // Coordinate-first DM touch routing for Heltec stability.
    if (dm_dialog_) {
      lv_indev_t* indev = lv_indev_get_act();
      if (indev) {
        lv_point_t pt{};
        lv_indev_get_point(indev, &pt);

        if (dm_close_btn_) {
          lv_area_t close_area{};
          lv_obj_get_coords(dm_close_btn_, &close_area);
          if (pt.x >= close_area.x1 && pt.x <= close_area.x2 && pt.y >= close_area.y1 && pt.y <= close_area.y2) {
            closeDmDialog(false);
            return;
          }
        }

        if (dm_new_btn_) {
          lv_area_t new_area{};
          lv_obj_get_coords(dm_new_btn_, &new_area);
          if (pt.x >= new_area.x1 && pt.x <= new_area.x2 && pt.y >= new_area.y1 && pt.y <= new_area.y2) {
            compose_dm_mode_ = true;
            compose_return_to_dm_ = true;
            strncpy(compose_target_dm_pubkey_, dm_active_key_, sizeof(compose_target_dm_pubkey_) - 1);
            compose_target_dm_pubkey_[sizeof(compose_target_dm_pubkey_) - 1] = '\0';
            strncpy(compose_target_channel_, dm_active_name_, sizeof(compose_target_channel_) - 1);
            compose_target_channel_[sizeof(compose_target_channel_) - 1] = '\0';
            lv_async_call(onOpenComposeDialogAsync, this);
            return;
          }
        }

        lv_area_t dm_area{};
        lv_obj_get_coords(dm_dialog_, &dm_area);
        if (pt.x >= dm_area.x1 && pt.x <= dm_area.x2 && pt.y >= dm_area.y1 && pt.y <= dm_area.y2) {
          if (key_group_ && dm_panel_) {
            lv_group_focus_obj(dm_panel_);
          }
          return;
        }
      }
    }

    if ((target == dm_close_btn_) || (target == dm_close_label_) || hasAncestor(target, dm_close_btn_)) {
      closeDmDialog(false);
      return;
    }
    if ((target == dm_new_btn_) || (target == dm_new_label_) || hasAncestor(target, dm_new_btn_)) {
      compose_dm_mode_ = true;
      compose_return_to_dm_ = true;
      strncpy(compose_target_dm_pubkey_, dm_active_key_, sizeof(compose_target_dm_pubkey_) - 1);
      compose_target_dm_pubkey_[sizeof(compose_target_dm_pubkey_) - 1] = '\0';
      strncpy(compose_target_channel_, dm_active_name_, sizeof(compose_target_channel_) - 1);
      compose_target_channel_[sizeof(compose_target_channel_) - 1] = '\0';
      lv_async_call(onOpenComposeDialogAsync, this);
      return;
    }

    if (hasAncestor(target, dm_dialog_)) {
      if (key_group_ && dm_panel_) {
        lv_group_focus_obj(dm_panel_);
      }
      return;
    }
    return;
  }

  if (contacts_open_) {
    const uint8_t contacts_option_count = clampOptionCount(contacts_count_, kChannelCount);
    const bool in_selector = hasAncestor(target, channel_selector_btn_);
    const bool in_dropdown = hasAncestor(target, channel_dropdown_panel_);
    int dropdown_row_idx = -1;
    for (uint8_t i = 0; i < contacts_option_count; i++) {
      if (target == channel_dropdown_rows_[i] || target == channel_dropdown_labels_[i] ||
          hasAncestor(target, channel_dropdown_rows_[i])) {
        dropdown_row_idx = static_cast<int>(i);
        break;
      }
    }

    if (in_selector) {
      const uint32_t now = millis();
#if defined(DEVICE_TLORA_PAGER_TFT)
      if (contacts_dropdown_guard_until_ms_ != 0 && now < contacts_dropdown_guard_until_ms_) {
        return;
      }
#endif
      if (now - last_selector_action_ms_ < kContactsDropdownEnterGuardMs) {
        return;
      }
      if (channel_dropdown_open_ && (now - last_dropdown_open_ms_ < kContactsDropdownEnterGuardMs)) {
        return;
      }
      last_selector_action_ms_ = now;

      contacts_nav_focused_ = true;
      if (channel_dropdown_open_) {
        contacts_dropdown_guard_until_ms_ = 0;
        closeChannelDropdown(false);
      } else {
        contacts_dropdown_guard_until_ms_ = now + kContactsDropdownEnterGuardMs;
        openChannelDropdown();
      }
      refreshContactsDialog(false);
      focusCurrentZoneObject();
      return;
    }

    if (channel_dropdown_open_) {
#if defined(DEVICE_TLORA_PAGER_TFT)
      const uint32_t now = millis();
      if (contacts_dropdown_guard_until_ms_ != 0 && now < contacts_dropdown_guard_until_ms_) {
        return;
      }
#endif
      if (dropdown_row_idx >= 0) {
        contacts_selected_index_ = static_cast<uint8_t>(dropdown_row_idx);
        contacts_nav_focused_ = false;
        contacts_dm_open_ = false;
        contacts_dropdown_guard_until_ms_ = 0;
        closeChannelDropdown(true);
        refreshContactsDialog(false);
        focusCurrentZoneObject();
        return;
      }
      if (!in_dropdown) {
        closeChannelDropdown(false);
        contacts_nav_focused_ = true;
        refreshContactsDialog(false);
        focusCurrentZoneObject();
      }
      return;
    }

    if ((target == contacts_dm_new_btn_) || (target == contacts_dm_new_label_) || hasAncestor(target, contacts_dm_new_btn_)) {
      contacts_nav_focused_ = false;
      contacts_dm_open_ = true;
      refreshContactsDialog(false);
      startComposeForSelectedContact();
      return;
    }

    if (hasAncestor(target, contacts_dm_panel_)) {
      contacts_nav_focused_ = false;
      contacts_dm_open_ = true;
      refreshContactsDialog(false);
      if (key_group_ && contacts_dm_panel_) {
        lv_group_focus_obj(contacts_dm_panel_);
      }
      return;
    }

    for (uint8_t i = 0; i < kContactActionCount; i++) {
      if ((target == contacts_action_rows_[i]) || (target == contacts_action_labels_[i]) ||
          hasAncestor(target, contacts_action_rows_[i])) {
        contacts_action_index_ = i;
        contacts_nav_focused_ = false;
        contacts_dm_open_ = false;
        if (kTouchDirectActivate) {
          activateContactsAction(i);
        } else {
          refreshContactsDialog(false);
          focusCurrentZoneObject();
        }
        return;
      }
    }

    if (hasAncestor(target, contacts_detail_info_panel_)) {
      contacts_nav_focused_ = false;
      contacts_dm_open_ = false;
      refreshContactsDialog(false);
      return;
    }

    if (hasAncestor(target, contacts_dialog_)) {
      return;
    }
    return;
  }

  if ((target == chat_advz_btn_) || (target == chat_advz_label_) || hasAncestor(target, chat_advz_btn_)) {
    triggerAdvertZeroHop();
    return;
  }

  if ((target == chat_advf_btn_) || (target == chat_advf_label_) || hasAncestor(target, chat_advf_btn_)) {
    triggerAdvertFlood();
    return;
  }

  if ((target == chat_new_btn_) || (target == chat_new_label_) || hasAncestor(target, chat_new_btn_)) {
    compose_dm_mode_ = false;
    compose_return_to_dm_ = false;
    setFocusZone(FocusZone::Chat);
    openComposeDialog();
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
    const uint32_t now = millis();
    if (now - last_dropdown_open_ms_ >= kNavDebounceMs) {
      closeChannelDropdown(false);
    }
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
    const uint32_t now = millis();
    if (now - last_dropdown_open_ms_ < kNavDebounceMs) {
      dropdown_highlight_channel_ = static_cast<uint8_t>(dropdown_row_idx);
      refreshDropdownVisuals();
      return;
    }
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
      if (false) Serial.printf("[KEYUI] LV_EVENT_KEY event_key=%lu target=%p\n", static_cast<unsigned long>(event_key),
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
        if (false) Serial.printf("[KEYUI] LV_EVENT_KEY indev_key=%lu\n", static_cast<unsigned long>(indev_key));
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
        if (ui->contacts_open_) {
          ui->contacts_nav_focused_ = true;
        }
      }
      if (target == ui->chat_panel_) {
        ui->focus_zone_ = FocusZone::Chat;
      }
      if (target == ui->chat_advz_btn_ || target == ui->chat_advz_label_ || target == ui->chat_advf_btn_ ||
          target == ui->chat_advf_label_) {
        ui->focus_zone_ = FocusZone::Chat;
      }
      if (target == ui->chat_new_btn_ || target == ui->chat_new_label_) {
        ui->focus_zone_ = FocusZone::Chat;
      }
      if (target == ui->compose_input_) {
        ui->focus_zone_ = FocusZone::Chat;
      }
      if (target == ui->dm_panel_) {
        ui->focus_zone_ = FocusZone::Shortcuts;
        ui->selected_shortcut_ = kShortcutContacts;
      }
      if (target == ui->dm_new_btn_ || target == ui->dm_new_label_ || target == ui->dm_close_btn_ ||
          target == ui->dm_close_label_) {
        ui->focus_zone_ = FocusZone::Shortcuts;
        ui->selected_shortcut_ = kShortcutContacts;
      }
      if (target == ui->live_dialog_ || target == ui->live_body_panel_ || target == ui->live_title_label_ ||
          hasAncestor(target, ui->live_body_panel_)) {
        ui->focus_zone_ = FocusZone::Shortcuts;
        ui->selected_shortcut_ = kShortcutLive;
      }
      if (target == ui->live_util_dialog_ || target == ui->live_util_chart_ || target == ui->live_util_title_label_ ||
          target == ui->live_util_units_label_ || target == ui->live_util_hint_label_ ||
          target == ui->live_util_stats_label_) {
        ui->focus_zone_ = FocusZone::Shortcuts;
        ui->selected_shortcut_ = kShortcutLive;
      }
      if (target == ui->live_snr_dialog_ || target == ui->live_snr_chart_ || target == ui->live_snr_title_label_ ||
          target == ui->live_snr_units_label_ || target == ui->live_snr_hint_label_ ||
          target == ui->live_snr_stats_label_) {
        ui->focus_zone_ = FocusZone::Shortcuts;
        ui->selected_shortcut_ = kShortcutLive;
      }
      if (target == ui->live_close_btn_ || target == ui->live_close_label_) {
        ui->focus_zone_ = FocusZone::Shortcuts;
        ui->selected_shortcut_ = kShortcutLive;
      }
      if (target == ui->help_dialog_ || target == ui->help_body_panel_ || target == ui->help_title_label_ ||
          target == ui->help_body_label_ || hasAncestor(target, ui->help_body_panel_)) {
        ui->focus_zone_ = FocusZone::Shortcuts;
        ui->selected_shortcut_ = kShortcutHelp;
      }
      if (target == ui->help_close_btn_ || target == ui->help_close_label_) {
        ui->focus_zone_ = FocusZone::Shortcuts;
        ui->selected_shortcut_ = kShortcutHelp;
      }
      for (uint8_t i = 0; i < ui->configured_channel_count_; i++) {
        if (target == ui->channel_dropdown_rows_[i] || target == ui->channel_dropdown_labels_[i]) {
          ui->focus_zone_ = FocusZone::Selector;
          ui->dropdown_highlight_channel_ = i;
          ui->refreshDropdownVisuals();
          break;
        }
      }
      if (ui->contacts_open_) {
        const uint8_t contacts_option_count = clampOptionCount(ui->contacts_count_, kChannelCount);
        for (uint8_t i = 0; i < contacts_option_count; i++) {
          if (target == ui->channel_dropdown_rows_[i] || target == ui->channel_dropdown_labels_[i]) {
            ui->focus_zone_ = FocusZone::Selector;
            ui->dropdown_highlight_channel_ = i;
            ui->contacts_nav_focused_ = true;
            ui->refreshDropdownVisuals();
            break;
          }
        }
      }
      for (uint8_t i = 0; i < kCfgRowCount; i++) {
        if (target == ui->cfg_rows_[i] || target == ui->cfg_row_labels_[i]) {
          ui->cfg_selected_row_ = i;
#if !defined(DEVICE_HELTEC_V4_EXPANSION)
          if (ui->cfg_selected_row_ != kCfgRowImportConfig) {
            ui->cfg_import_confirm_armed_ = false;
          }
          if (ui->cfg_selected_row_ != kCfgRowDeleteConfig) {
            ui->cfg_delete_confirm_armed_ = false;
          }
#endif
          break;
        }
      }
      for (uint8_t i = 0; i < kContactActionCount; i++) {
        if ((target == ui->contacts_action_rows_[i]) || (target == ui->contacts_action_labels_[i]) ||
            hasAncestor(target, ui->contacts_action_rows_[i])) {
          ui->contacts_action_index_ = i;
          ui->contacts_nav_focused_ = false;
          ui->contacts_dm_open_ = false;
          break;
        }
      }
      if ((target == ui->contacts_dm_panel_) || hasAncestor(target, ui->contacts_dm_panel_) ||
          (target == ui->contacts_dm_new_btn_) || (target == ui->contacts_dm_new_label_) ||
          hasAncestor(target, ui->contacts_dm_new_btn_)) {
        ui->contacts_nav_focused_ = false;
        ui->contacts_dm_open_ = true;
      }
      if ((target == ui->contacts_detail_info_panel_) || hasAncestor(target, ui->contacts_detail_info_panel_)) {
        ui->contacts_nav_focused_ = false;
        ui->contacts_dm_open_ = false;
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
    case LV_EVENT_PRESSED:
      ui->handleClick(target);
      break;
    case LV_EVENT_CLICKED:
      // Contacts has its own callback path; ignore generic CLICKED there to avoid double-dispatch.
      if (ui->contacts_open_ && ui->contacts_dialog_ && hasAncestor(target, ui->contacts_dialog_)) {
        break;
      }
      ui->handleClick(target);
      break;
    default:
      break;
  }
}

void StandaloneUi::onContactsEvent(lv_event_t* event) {
  auto* ui = static_cast<StandaloneUi*>(lv_event_get_user_data(event));
  if (!ui) {
    return;
  }

  const lv_event_code_t code = lv_event_get_code(event);
  if (code != LV_EVENT_CLICKED) {
    return;
  }

  lv_obj_t* target = lv_event_get_target(event);
  CTS_TRACE("onContactsEvent click target=%p", static_cast<void*>(target));
  ui->handleClick(target);
}

void StandaloneUi::onDmEvent(lv_event_t* event) {
  auto* ui = static_cast<StandaloneUi*>(lv_event_get_user_data(event));
  if (!ui) {
    return;
  }

  const lv_event_code_t code = lv_event_get_code(event);
  if (code != LV_EVENT_PRESSED && code != LV_EVENT_CLICKED) {
    return;
  }

  lv_obj_t* target = lv_event_get_target(event);
  CTS_TRACE("onDmEvent code=%d target=%p", static_cast<int>(code), static_cast<void*>(target));
  ui->handleClick(target);
}

void StandaloneUi::loop() {
  if (splash_overlay_) {
    if (splash_dismiss_ms_ == 0) {
      // First loop tick after the splash was built. The framebuffer hasn't
      // been flushed yet (we intentionally don't pump LVGL during setup to
      // avoid interleaving display SPI with the LoRa radio bring-up), so
      // start the dismissal countdown now and let this iteration's
      // lv_timer_handler render the splash.
      splash_dismiss_ms_ = millis() + splash_duration_ms_;
    } else if (millis() >= splash_dismiss_ms_) {
      dismissSplash();
    }
  }
  if (!started_) {
    return;
  }

  const uint32_t now = millis();
  if (pending_contacts_open_ && kUseOnscreenKeyboard) {
    CTS_TRACE("openContactsDialog loop_open");
    onOpenContactsDialogAsync(this);
  } else if (pending_contacts_show_ && kUseOnscreenKeyboard) {
    pending_contacts_show_ = false;
    CTS_TRACE("openContactsDialog loop_show");
    if (contacts_dialog_) {
      lv_obj_clear_flag(contacts_dialog_, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(contacts_dialog_);
    }
    if (shortcut_strip_) {
      lv_obj_add_flag(shortcut_strip_, LV_OBJ_FLAG_HIDDEN);
    }
    pending_contacts_post_open_ = true;
  } else if (pending_contacts_post_open_ && kUseOnscreenKeyboard) {
    CTS_TRACE("openContactsDialog loop_post_activate");
    onContactsPostOpenAsync(this);
  }

  syncChannelsFromMeshIfNeeded(now);
  sampleLiveMetrics(now);
  refreshClockIfNeeded(now);
  refreshUnreadPulse(now);

  if (now - last_dm_retention_prune_ms_ >= kDmRetentionPruneMs) {
    last_dm_retention_prune_ms_ = now;
    if (pruneStoredDmByRetention(nowEpochSecondsOrZero())) {
      if (dm_open_) {
        rebuildDmDialog();
      }
      if (contacts_open_ && contacts_dm_open_) {
        rebuildContactsDmPanel();
      }
    }
  }

  if (chat_history_dirty_ && now - last_chat_persist_ms_ >= kChatPersistFlushMs) {
    saveChatHistoryToFs();
    last_chat_persist_ms_ = now;
  }

  if (dm_history_dirty_ && now - last_dm_persist_ms_ >= kDmPersistFlushMs) {
    saveDmHistoryToFs();
    last_dm_persist_ms_ = now;
  }

  if (advert_popup_open_ && advert_popup_deadline_ms_ != 0 &&
      static_cast<int32_t>(now - advert_popup_deadline_ms_) >= 0) {
    closeAdvertPopup();
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
    saveDmHistoryToFs();
    last_chat_persist_ms_ = millis();
    last_dm_persist_ms_ = last_chat_persist_ms_;
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

  char hhmm[8] = {};
  formatUiClockHhMm(hhmm, sizeof(hhmm));

  if (event.type == mesh::MeshEventType::Info) {
    int16_t snr_db = 0;
    int16_t rssi_dbm = 0;
    if (parseRadioInfoSample(event.text, &snr_db, &rssi_dbm)) {
      appendSnrRssiSample(snr_db, rssi_dbm);
      return;
    }

    char live_line[128] = {};
    snprintf(live_line, sizeof(live_line), "[%s] INFO %s", hhmm, event.text);

    ChatLineKind info_kind = ChatLineKind::Ack;
    if (strstr(event.text, "fail") || strstr(event.text, "Fail") || strstr(event.text, "dropped") ||
        strstr(event.text, "timeout")) {
      info_kind = ChatLineKind::Error;
    }
    appendLiveFeedLine(live_line, info_kind);
    return;
  }

  if (event.type == mesh::MeshEventType::DirectMessage) {
    const uint32_t event_epoch = nowEpochSecondsOrZero();
    char live_line[128] = {};
    snprintf(live_line, sizeof(live_line), "[%s] DM %s: %s", hhmm,
             event.channel_name[0] != '\0' ? event.channel_name : "(unnamed)", event.text);
    appendLiveFeedLine(live_line, ChatLineKind::Rx);

    strncpy(last_dm_sender_name_, event.channel_name, sizeof(last_dm_sender_name_) - 1);
    last_dm_sender_name_[sizeof(last_dm_sender_name_) - 1] = '\0';
    if (event.peer_key[0] != '\0') {
      strncpy(last_dm_sender_key_, event.peer_key, sizeof(last_dm_sender_key_) - 1);
      last_dm_sender_key_[sizeof(last_dm_sender_key_) - 1] = '\0';
    } else {
      last_dm_sender_key_[0] = '\0';
    }

    const bool dm_has_key = dm_active_key_[0] != '\0';
    const bool line_has_key = event.peer_key[0] != '\0';
    const bool key_match = dm_has_key && line_has_key && strcmp(dm_active_key_, event.peer_key) == 0;
    const bool name_match = dmNameLikelyMatch(dm_active_name_, event.channel_name);
    bool contacts_match = false;
    if (contacts_open_ && contacts_dm_open_ && contacts_count_ > 0 && contacts_selected_index_ < contacts_count_) {
      const mesh::MeshContactSummary& selected = contacts_cache_[contacts_selected_index_];
      const bool selected_has_key = selected.public_key_hex[0] != '\0';
      const bool contacts_key_match =
          selected_has_key && line_has_key && strcmp(selected.public_key_hex, event.peer_key) == 0;
      const bool contacts_name_match = dmNameLikelyMatch(selected.name, event.channel_name);
      contacts_match = contacts_key_match || contacts_name_match;
    }
    const bool matches_active_dm = (dm_open_ && (key_match || name_match)) || contacts_match;

    if (!matches_active_dm) {
      has_unread_dm_ = true;
      refreshShortcutVisuals();
    } else if (has_unread_dm_) {
      has_unread_dm_ = false;
      refreshShortcutVisuals();
    }
    char initials[12] = {};
    abbreviateContactName(event.channel_name, initials, sizeof(initials));
    char dm_line[112] = {};
    snprintf(dm_line, sizeof(dm_line), "[%s] %s: %s", hhmm, initials, event.text);
    appendDmLine(event.channel_name, event.peer_key, dm_line, ChatLineKind::Rx, event_epoch);
    return;
  }

  if (event.type != mesh::MeshEventType::ChannelMessage) {
    return;
  }

  char live_line[128] = {};
  snprintf(live_line, sizeof(live_line), "[%s] CH %s: %s", hhmm,
           event.channel_name[0] != '\0' ? event.channel_name : "(unknown)", event.text);
  appendLiveFeedLine(live_line, ChatLineKind::Rx);

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

  char display_text[96] = {};
  snprintf(display_text, sizeof(display_text), "[%s] %s", hhmm, event.text);
  const uint32_t event_epoch = nowEpochSecondsOrZero();

  pushChannelHistoryLine(event.channel_name, display_text, ChatLineKind::Rx, event_epoch);

  if (static_cast<uint8_t>(channel_index) == active_channel_) {
    appendChatLine(display_text, ChatLineKind::Rx, event_epoch);
  } else {
    unread_channels_[channel_index] = true;
    refreshChannelVisuals();
  }
}

}  // namespace ui
}  // namespace plumeria
