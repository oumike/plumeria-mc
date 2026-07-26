#pragma once

// UI theme palette for the on-device LVGL interface.
//
// Every color the UI draws with resolves through themedColorHex(). The rest of
// standalone_ui.cpp keeps writing plain hex literals (and the kColor* names);
// a `#define lv_color_hex` in that translation unit reroutes them here, so a
// theme switch repaints the whole UI without touching hundreds of call sites.
//
// A theme is authored as four base colors (background, panel, alt panel,
// accent) plus a light/dark mode; every other role is derived from those. The
// stock Plumeria Dark theme is the exception: it restores the hand-tuned
// palette the UI shipped with, so the default look is byte-identical.

#include <Arduino.h>
#include <lvgl.h>

namespace plumeria {
namespace ui {

enum UiThemeFamily : uint8_t {
  UI_THEME_PLUMERIA = 0,
  UI_THEME_EVERGREEN = 1,
  UI_THEME_EARTHEN = 2,
  UI_THEME_SOLARIZED = 3,
  UI_THEME_CRIMSON = 4,
  UI_THEME_SCARLET_POP = 5,
  UI_THEME_INK_WASH = 6,
  UI_THEME_LAVENDAR_FIELDS = 7,
  UI_THEME_WILD_FLOWERS = 8,
  UI_THEME_QUIET_LUXURY = 9,
  UI_THEME_MORNING_DEW = 10,
  UI_THEME_WINTER_CHILL = 11,
  UI_THEME_COUNT = 12,
};

enum UiThemeMode : uint8_t {
  UI_MODE_DARK = 0,
  UI_MODE_LIGHT = 1,
};

// Authoring form of a theme: four base colors as 24-bit RGB plus a mode.
struct UiThemePreset {
  uint8_t theme;
  uint8_t mode;
  uint32_t bg_main;
  uint32_t panel_bg;
  uint32_t panel_alt;
  uint32_t accent;
  const char* name;
};

// Every family ships a dark and a light variant.
constexpr int kUiThemePresetCount = UI_THEME_COUNT * 2;

extern const UiThemePreset kUiThemePresets[kUiThemePresetCount];

// Resolved colors for one theme. Roles map 1:1 onto the hex literals the UI
// used to hardcode; see kThemedColorMap in ui_theme.cpp.
struct UiPalette {
  lv_color_t bg_root;
  lv_color_t panel;
  lv_color_t panel_alt;
  lv_color_t panel_strong;
  lv_color_t sub_panel;
  lv_color_t sub_panel_border;
  lv_color_t border;
  lv_color_t divider;
  lv_color_t focus;
  lv_color_t active;
  lv_color_t active_border;
  lv_color_t unread;
  lv_color_t text_main;
  lv_color_t text_dim;
  lv_color_t text_bright;
  lv_color_t text_on_accent;
  lv_color_t msg_rx;
  lv_color_t msg_tx;
  lv_color_t msg_ack;
  lv_color_t msg_err;
  lv_color_t live_direct;
  lv_color_t wifi_on;
  lv_color_t wifi_off;
  lv_color_t chat_bg;
  lv_color_t button_focus_bg;
  lv_color_t selector_bg;
  lv_color_t dropdown_bg;
  lv_color_t dropdown_highlight_bg;
  lv_color_t dropdown_highlight_text;
  lv_color_t dropdown_highlight_border;
  lv_color_t dropdown_active_bg;
  lv_color_t dropdown_active_border;
  lv_color_t scrollbar;
  lv_color_t shortcut_active_bg;
  lv_color_t modal_bg;
  lv_color_t modal_border;
  lv_color_t input_bg;
  lv_color_t input_text;
  lv_color_t input_border;
  lv_color_t row_bg;
  lv_color_t row_border;
  lv_color_t battery_track;
  lv_color_t battery_fill;
  lv_color_t confirm_yes_bg;
  lv_color_t confirm_no_bg;
};

// Recomputes the active palette. Out-of-range values fall back to Plumeria
// Dark. Safe to call repeatedly; the UI must rebuild its styles afterwards.
void applyUiThemePalette(uint8_t theme, uint8_t mode);

const UiPalette& uiPalette();
uint8_t activeUiTheme();
uint8_t activeUiMode();

// Index into kUiThemePresets, or 0 when the pair is unknown.
int uiThemePresetIndex(uint8_t theme, uint8_t mode);
const char* uiThemePresetName(uint8_t theme, uint8_t mode);

// Preview colors for the picker rows, without having to apply the theme.
// 0=background, 1=panel, 2=accent, 3=text readable on that preset's panel.
lv_color_t uiThemePresetSwatch(int preset_index, uint8_t swatch);

// Maps a legacy UI hex literal onto the active palette. Unmapped values (the
// splash artwork, pure black scrims) pass through unchanged.
lv_color_t themedColorHex(uint32_t rgb);

}  // namespace ui
}  // namespace plumeria
