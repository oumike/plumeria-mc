#include "ui/ui_theme.h"

namespace plumeria {
namespace ui {

namespace {

constexpr uint32_t rgb24(uint8_t r, uint8_t g, uint8_t b) {
  return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

// Linear interpolation between two 24-bit colors. t=0 yields a, t=255 yields b.
uint32_t blend24(uint32_t a, uint32_t b, uint8_t t) {
  const int ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
  const int br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
  const int r = ar + (((br - ar) * t) / 255);
  const int g = ag + (((bg - ag) * t) / 255);
  const int bl = ab + (((bb - ab) * t) / 255);
  return rgb24(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(bl));
}

// Rec. 709 luma, good enough to decide black-vs-white text over a fill.
bool isLightColor(uint32_t rgb) {
  const int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
  return ((r * 54) + (g * 183) + (b * 19)) >> 8 > 140;
}

uint32_t contrastTextOn(uint32_t bg) {
  return isLightColor(bg) ? 0x101418u : 0xFFFFFFu;
}

lv_color_t toLv(uint32_t rgb) {
  return lv_color_make(static_cast<uint8_t>((rgb >> 16) & 0xFF),
                       static_cast<uint8_t>((rgb >> 8) & 0xFF),
                       static_cast<uint8_t>(rgb & 0xFF));
}

// Semantic status colors (RX/TX/ACK/error/link) keep their hue across themes so
// the meaning stays learnable, but light modes need them darkened to stay
// readable against a pale chat background.
uint32_t semantic(uint32_t rgb, bool is_light) {
  return is_light ? blend24(rgb, 0x0D1116, 135) : rgb;
}

UiPalette s_palette = {};
uint8_t s_active_theme = UI_THEME_PLUMERIA;
uint8_t s_active_mode = UI_MODE_DARK;
bool s_palette_ready = false;

// The palette the UI shipped with, restored verbatim for Plumeria Dark so the
// default theme is unchanged by the theming work.
void applyLegacyPlumeriaDark() {
  s_palette.bg_root = toLv(0x08121B);
  s_palette.panel = toLv(0x0C1A27);
  s_palette.panel_alt = toLv(0x102335);
  s_palette.panel_strong = toLv(0x123266);
  s_palette.sub_panel = toLv(0x0F2538);
  s_palette.sub_panel_border = toLv(0x2F5A78);
  s_palette.border = toLv(0x1D3C55);
  s_palette.divider = toLv(0x2B4A63);
  s_palette.focus = toLv(0x33D1FF);
  s_palette.active = toLv(0x1E9ED1);
  s_palette.active_border = toLv(0x54D6FF);
  s_palette.unread = toLv(0x6EF0FF);
  s_palette.text_main = toLv(0xD8E7F2);
  s_palette.text_dim = toLv(0x8FA8BA);
  s_palette.text_bright = toLv(0xEAF8FF);
  s_palette.text_on_accent = toLv(0xFFFFFF);
  s_palette.msg_rx = toLv(0x89E3FF);
  s_palette.msg_tx = toLv(0xA8FFB5);
  s_palette.msg_ack = toLv(0x7ED6A7);
  s_palette.msg_err = toLv(0xFF7D7D);
  s_palette.live_direct = toLv(0xFFD27A);
  s_palette.wifi_on = toLv(0x59D88E);
  s_palette.wifi_off = toLv(0xF56767);
  s_palette.chat_bg = toLv(0x0A1622);
  s_palette.button_focus_bg = toLv(0x133149);
  s_palette.selector_bg = toLv(0x133049);
  s_palette.dropdown_bg = toLv(0x132433);
  s_palette.dropdown_highlight_bg = toLv(0x48DBFF);
  s_palette.dropdown_highlight_text = toLv(0x03131F);
  s_palette.dropdown_highlight_border = toLv(0xA5F0FF);
  s_palette.dropdown_active_bg = toLv(0x18668A);
  s_palette.dropdown_active_border = toLv(0x6EE7FF);
  s_palette.scrollbar = toLv(0x2C7CA5);
  s_palette.shortcut_active_bg = toLv(0x15435F);
  s_palette.modal_bg = toLv(0x0E285B);
  s_palette.modal_border = toLv(0x5C86C6);
  s_palette.input_bg = toLv(0x102B61);
  s_palette.input_text = toLv(0xE8F1FF);
  s_palette.input_border = toLv(0x4C76BA);
  s_palette.row_bg = toLv(0x14344B);
  s_palette.row_border = toLv(0x3F7292);
  s_palette.battery_track = toLv(0x0B1E2D);
  s_palette.battery_fill = toLv(0x59D8A0);
  s_palette.confirm_yes_bg = toLv(0x2F6B30);
  s_palette.confirm_no_bg = toLv(0x6B3030);
}

void derivePalette(const UiThemePreset& preset) {
  const bool light = (preset.mode == UI_MODE_LIGHT);
  const uint32_t bg = preset.bg_main;
  const uint32_t panel = preset.panel_bg;
  const uint32_t alt = preset.panel_alt;
  const uint32_t accent = preset.accent;

  const uint32_t active = blend24(accent, bg, light ? 40 : 70);
  const uint32_t text_main = light ? 0x1E242C : 0xF3F6FA;

  s_palette.bg_root = toLv(bg);
  s_palette.panel = toLv(panel);
  s_palette.panel_alt = toLv(alt);
  s_palette.panel_strong = toLv(blend24(alt, accent, light ? 36 : 48));
  s_palette.sub_panel = toLv(blend24(panel, bg, light ? 60 : 96));
  s_palette.sub_panel_border = toLv(blend24(alt, accent, light ? 60 : 80));
  s_palette.border = toLv(blend24(alt, accent, light ? 55 : 40));
  s_palette.divider = toLv(blend24(alt, accent, light ? 80 : 70));
  s_palette.focus = toLv(accent);
  s_palette.active = toLv(active);
  s_palette.active_border = toLv(blend24(accent, 0xFFFFFF, light ? 40 : 60));
  s_palette.unread = toLv(blend24(accent, light ? 0x000000 : 0xFFFFFF, light ? 60 : 90));
  s_palette.text_main = toLv(text_main);
  s_palette.text_dim = toLv(light ? 0x5E6876 : 0xB7C0CC);
  s_palette.text_bright = toLv(blend24(text_main, light ? 0x000000 : 0xFFFFFF, 70));
  s_palette.text_on_accent = toLv(contrastTextOn(active));
  s_palette.msg_rx = toLv(semantic(0x89E3FF, light));
  s_palette.msg_tx = toLv(semantic(0xA8FFB5, light));
  s_palette.msg_ack = toLv(semantic(0x7ED6A7, light));
  s_palette.msg_err = toLv(semantic(0xFF7D7D, light));
  s_palette.live_direct = toLv(semantic(0xFFD27A, light));
  s_palette.wifi_on = toLv(semantic(0x59D88E, light));
  s_palette.wifi_off = toLv(semantic(0xF56767, light));
  s_palette.chat_bg = toLv(light ? blend24(panel, 0xFFFFFF, 70) : blend24(bg, panel, 70));
  s_palette.button_focus_bg = toLv(blend24(alt, accent, light ? 45 : 60));
  s_palette.selector_bg = toLv(blend24(alt, bg, light ? 40 : 60));
  s_palette.dropdown_bg = toLv(blend24(bg, panel, light ? 150 : 110));
  s_palette.dropdown_highlight_bg = toLv(accent);
  s_palette.dropdown_highlight_text = toLv(contrastTextOn(accent));
  s_palette.dropdown_highlight_border = toLv(blend24(accent, light ? 0x000000 : 0xFFFFFF, 70));
  s_palette.dropdown_active_bg = toLv(blend24(alt, accent, light ? 90 : 130));
  s_palette.dropdown_active_border = toLv(blend24(accent, light ? 0x000000 : 0xFFFFFF, 45));
  s_palette.scrollbar = toLv(blend24(alt, accent, light ? 110 : 140));
  s_palette.shortcut_active_bg = toLv(blend24(alt, accent, light ? 55 : 90));
  s_palette.modal_bg = toLv(panel);
  s_palette.modal_border = toLv(blend24(alt, accent, light ? 100 : 120));
  s_palette.input_bg = toLv(light ? blend24(panel, 0xFFFFFF, 90) : blend24(alt, accent, 30));
  s_palette.input_text = toLv(light ? 0x11161C : 0xF2F8FF);
  s_palette.input_border = toLv(blend24(alt, accent, light ? 90 : 110));
  s_palette.row_bg = toLv(blend24(alt, accent, light ? 18 : 25));
  s_palette.row_border = toLv(blend24(alt, accent, light ? 80 : 110));
  s_palette.battery_track = toLv(blend24(bg, panel, light ? 120 : 60));
  s_palette.battery_fill = toLv(semantic(0x59D8A0, light));
  // Confirm buttons stay deliberately dark in both modes: their labels are
  // drawn white, and a light-mode green/red fill would wash them out.
  s_palette.confirm_yes_bg = toLv(blend24(0x2F6B30, accent, 30));
  s_palette.confirm_no_bg = toLv(blend24(0x6B3030, accent, 30));
}

}  // namespace

const UiThemePreset kUiThemePresets[kUiThemePresetCount] = {
    // Plumeria is the stock look: its dark variant restores the exact palette
    // the UI shipped with, so upgrading changes nothing by default.
    {UI_THEME_PLUMERIA, UI_MODE_DARK, 0x08121B, 0x0C1A27, 0x102335, 0x33D1FF, "Plumeria Dark"},
    {UI_THEME_PLUMERIA, UI_MODE_LIGHT, 0xF2F8FC, 0xFFFFFF, 0xE2EEF6, 0x0F7FA8, "Plumeria Light"},
    {UI_THEME_EVERGREEN, UI_MODE_DARK, 0x00150B, 0x11361B, 0x1A4225, 0x55B053, "Evergreen Dark"},
    {UI_THEME_EVERGREEN, UI_MODE_LIGHT, 0xE7E7E5, 0xF7FBF7, 0xE7E4DE, 0x2D2751, "Evergreen Light"},
    {UI_THEME_EARTHEN, UI_MODE_DARK, 0x101010, 0x212021, 0x29282B, 0xD37059, "Earthy Dark"},
    {UI_THEME_EARTHEN, UI_MODE_LIGHT, 0xF7FBF7, 0xFFFFFF, 0xF7EEE1, 0xB4406B, "Earthy Light"},
    {UI_THEME_SOLARIZED, UI_MODE_DARK, 0x002B36, 0x073642, 0x0C3C47, 0x2AA198, "Solarized Dark"},
    {UI_THEME_SOLARIZED, UI_MODE_LIGHT, 0xEEE8D5, 0xFDF6E3, 0xEEE8D5, 0x2AA198, "Solarized Light"},
    {UI_THEME_CRIMSON, UI_MODE_DARK, 0x060F24, 0x12244C, 0x1B3363, 0xFF4A58, "Crimson Blue Dark"},
    {UI_THEME_CRIMSON, UI_MODE_LIGHT, 0xF3F7FF, 0xF8FBFF, 0xE6EFFF, 0xC62839, "Crimson Blue Light"},
    {UI_THEME_SCARLET_POP, UI_MODE_DARK, 0x150009, 0x760031, 0x8B0038, 0xD51C39, "Scarlet Pop Dark"},
    {UI_THEME_SCARLET_POP, UI_MODE_LIGHT, 0xFFF2F4, 0xFFF8F9, 0xFFEAED, 0xD51C39, "Scarlet Pop Light"},
    {UI_THEME_INK_WASH, UI_MODE_DARK, 0x111318, 0x1C2128, 0x252B34, 0xD8DDE4, "Ink Wash Dark"},
    {UI_THEME_INK_WASH, UI_MODE_LIGHT, 0xF3F5F7, 0xFFFFFF, 0xE8EBEF, 0x2E3440, "Ink Wash Light"},
    {UI_THEME_LAVENDAR_FIELDS, UI_MODE_DARK, 0x1A1230, 0x251A45, 0x2F2258, 0xB79BFF,
     "Lavendar Fields Dark"},
    {UI_THEME_LAVENDAR_FIELDS, UI_MODE_LIGHT, 0xF5EFFB, 0xFFF9FF, 0xEDE1F7, 0x7B5BA7,
     "Lavendar Fields Light"},
    {UI_THEME_WILD_FLOWERS, UI_MODE_DARK, 0x1A2430, 0x253547, 0x2D455B, 0xC78FCF,
     "Wild Flowers Dark"},
    {UI_THEME_WILD_FLOWERS, UI_MODE_LIGHT, 0xF6FAF4, 0xFFFFFF, 0xE5F0E2, 0x8A5FAF,
     "Wild Flowers Light"},
    {UI_THEME_QUIET_LUXURY, UI_MODE_DARK, 0x2A1F17, 0x34271E, 0x403126, 0xD9C7A3,
     "Quiet Luxury Dark"},
    {UI_THEME_QUIET_LUXURY, UI_MODE_LIGHT, 0xFAF4EA, 0xFFFDF8, 0xF1E7D5, 0xA8844F,
     "Quiet Luxury Light"},
    {UI_THEME_MORNING_DEW, UI_MODE_DARK, 0x12282A, 0x1A3638, 0x234345, 0x9CD8C8,
     "Morning Dew Dark"},
    {UI_THEME_MORNING_DEW, UI_MODE_LIGHT, 0xEEF9F6, 0xFFFFFF, 0xDDF1EC, 0x4E9C8A,
     "Morning Dew Light"},
    {UI_THEME_WINTER_CHILL, UI_MODE_DARK, 0x151F2B, 0x1C2A3A, 0x243649, 0x8FB3D9,
     "Winter Chill Dark"},
    {UI_THEME_WINTER_CHILL, UI_MODE_LIGHT, 0xF1F7FC, 0xFFFFFF, 0xDFEBF6, 0x5C86B2,
     "Winter Chill Light"},
};

int uiThemePresetIndex(uint8_t theme, uint8_t mode) {
  for (int i = 0; i < kUiThemePresetCount; i++) {
    if (kUiThemePresets[i].theme == theme && kUiThemePresets[i].mode == mode) {
      return i;
    }
  }
  return 0;
}

const char* uiThemePresetName(uint8_t theme, uint8_t mode) {
  return kUiThemePresets[uiThemePresetIndex(theme, mode)].name;
}

void applyUiThemePalette(uint8_t theme, uint8_t mode) {
  if (theme >= UI_THEME_COUNT) {
    theme = UI_THEME_PLUMERIA;
  }
  if (mode > UI_MODE_LIGHT) {
    mode = UI_MODE_DARK;
  }

  const UiThemePreset& preset = kUiThemePresets[uiThemePresetIndex(theme, mode)];
  if (preset.theme == UI_THEME_PLUMERIA && preset.mode == UI_MODE_DARK) {
    applyLegacyPlumeriaDark();
  } else {
    derivePalette(preset);
  }

  s_active_theme = preset.theme;
  s_active_mode = preset.mode;
  s_palette_ready = true;
}

const UiPalette& uiPalette() {
  if (!s_palette_ready) {
    applyUiThemePalette(UI_THEME_PLUMERIA, UI_MODE_DARK);
  }
  return s_palette;
}

uint8_t activeUiTheme() { return s_active_theme; }
uint8_t activeUiMode() { return s_active_mode; }

lv_color_t uiThemePresetSwatch(int preset_index, uint8_t swatch) {
  if (preset_index < 0 || preset_index >= kUiThemePresetCount) {
    preset_index = 0;
  }
  const UiThemePreset& p = kUiThemePresets[preset_index];
  switch (swatch) {
    case 0: return toLv(p.bg_main);
    case 1: return toLv(p.panel_alt);
    case 2: return toLv(p.accent);
    // Picker rows are filled with panel_alt, so the label has to contrast
    // against that rather than against the theme's own text color.
    default: return toLv(contrastTextOn(p.panel_alt));
  }
}

lv_color_t themedColorHex(uint32_t rgb) {
  const UiPalette& p = uiPalette();
  switch (rgb) {
    case 0x08121B: return p.bg_root;
    case 0x0C1A27: return p.panel;
    case 0x102335: return p.panel_alt;
    case 0x123266: return p.panel_strong;
    case 0x0F2538: return p.sub_panel;
    case 0x2F5A78: return p.sub_panel_border;
    case 0x1D3C55: return p.border;
    case 0x2B4A63: return p.divider;
    case 0x33D1FF: return p.focus;
    case 0x1E9ED1: return p.active;
    case 0x54D6FF:
    case 0x8DEBFF: return p.active_border;
    case 0x6EF0FF: return p.unread;
    case 0xD8E7F2: return p.text_main;
    case 0x8FA8BA: return p.text_dim;
    case 0xEAF8FF:
    case 0xD9E8FF: return p.text_bright;
    case 0xFFFFFF: return p.text_on_accent;
    case 0x89E3FF: return p.msg_rx;
    case 0xA8FFB5: return p.msg_tx;
    case 0x7ED6A7: return p.msg_ack;
    case 0xFF7D7D: return p.msg_err;
    case 0xFFD27A: return p.live_direct;
    case 0x59D88E: return p.wifi_on;
    case 0xF56767: return p.wifi_off;
    case 0x0A1622: return p.chat_bg;
    case 0x133149: return p.button_focus_bg;
    case 0x133049: return p.selector_bg;
    case 0x132433: return p.dropdown_bg;
    case 0x48DBFF: return p.dropdown_highlight_bg;
    case 0x03131F: return p.dropdown_highlight_text;
    case 0xA5F0FF: return p.dropdown_highlight_border;
    case 0x18668A: return p.dropdown_active_bg;
    case 0x6EE7FF: return p.dropdown_active_border;
    case 0x2C7CA5: return p.scrollbar;
    case 0x15435F: return p.shortcut_active_bg;
    case 0x0E285B: return p.modal_bg;
    case 0x5C86C6: return p.modal_border;
    case 0x102B61: return p.input_bg;
    case 0xE8F1FF: return p.input_text;
    case 0x4C76BA: return p.input_border;
    case 0x14344B: return p.row_bg;
    case 0x3F7292: return p.row_border;
    case 0x0B1E2D: return p.battery_track;
    case 0x59D8A0: return p.battery_fill;
    case 0x2F6B30: return p.confirm_yes_bg;
    case 0x6B3030: return p.confirm_no_bg;
    default: break;
  }
  // Splash artwork and the pure-black modal scrim are intentionally untouched.
  return toLv(rgb);
}

}  // namespace ui
}  // namespace plumeria
