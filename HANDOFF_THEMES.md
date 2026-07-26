# Handoff — UI themes + Heltec V4 removal

Session ending 2026-07-25. Base commit `21bcca0` (Release v1.8.2), branch `main`, VERSION `v1.8.2`.
**Everything below is uncommitted working-tree state.** Read `CODEX_HANDOFF.md` first for general
project layering; this document only covers what this session changed.

## Status

| Work | State |
|---|---|
| Theme engine + palette | Built, compiles on all 3 targets, **not yet verified on hardware** |
| On-device theme picker | Crashed once (fixed), **fix not yet flashed/tested** |
| Web config theme picker | Built, **never exercised** |
| Heltec V4 removal | Done, proven binary-identical, all 3 targets build |

Builds green: `tdeck`, `tlora-pager-tft`, `cardputer-cap`.
User tests on **T-Deck** first.

## Files

```
new:      src/ui/ui_theme.h, src/ui/ui_theme.cpp      (untracked — git add these)
modified: src/ui/standalone_ui.{h,cpp}, src/web/web_config.{h,cpp}
modified: src/main.cpp, src/mesh/mesh_adapter.cpp,
          src/hal/device_{board,lvgl}.cpp, src/ota/ota_update.cpp   (Heltec removal only)
deleted:  boards/heltec_v4.json
```

---

## 1. Themes (GitHub issue #2)

Ported from `~/Projects/camillia/camillia-mt`. 12 families × dark/light = 24 presets.
Camillia's `CAMELLIA` family was replaced by `PLUMERIA`; the other 11 keep camillia's exact base
colors.

### The central mechanism — read this before editing standalone_ui.cpp

`standalone_ui.cpp` line ~79 defines:

```c
#define lv_color_hex(rgb) ::plumeria::ui::themedColorHex(static_cast<uint32_t>(rgb))
```

This **shadows LVGL's own `lv_color_hex()` for that translation unit only**. Every color literal in
the file is therefore a *palette lookup key*, not a literal. `themedColorHex()` (ui_theme.cpp) maps
~46 known hex values onto palette roles and passes anything else through unchanged (splash artwork,
the black modal scrim).

Consequences you must respect:

- The `kColor*` names are **macros**, not constants. They have to be, so each use re-reads the
  active palette instead of freezing at static-init time.
- Adding a new UI color means adding its literal to the `themedColorHex()` switch, or it will not
  follow the theme.
- The macro must stay below every `#include`.

This is why the diff is ~700 lines instead of several thousand — no call sites changed.

### Palette derivation

`kUiThemePresets[]` authors each theme as 4 colors (bg / panel / panel_alt / accent) + mode.
`derivePalette()` blends those into ~46 roles.

**Exception:** Plumeria Dark calls `applyLegacyPlumeriaDark()`, which restores the shipped palette
verbatim so the default look is unchanged. If Plumeria Dark ever looks different from v1.8.2, that is
a mapping bug in `themedColorHex()`, not a tuning issue.

### Live theme switching

`rebuildUiForThemeChange()` flushes dirty history → deletes `root_` → `resetUiObjectHandles()` zeroes
~157 widget handles → resets 20 shared `lv_style_t` → rebuilds → re-renders chat from memory.

**Deferred to `loop()` via `theme_rebuild_pending_`.** It must never run inside an LVGL event
callback — the rebuild frees the very object that raised the event. Same pattern as camillia's
`scheduleThemeRebuild`.

`resetUiObjectHandles()` was generated mechanically from the member declarations in
`standalone_ui.h` and is kept in the same order. **If you add an `lv_obj_t*` member, add it there
too**, or it becomes a dangling pointer after the first theme switch. `splash_overlay_` is
deliberately excluded (lives on the screen, not under `root_`).

### Entry points

- **On-device:** config row 10 (`kCfgRowTheme`) → `openThemeListDialog()`. j/k + arrows, Enter
  applies, Backspace closes. Row indices after Theme shifted by +1 (`kCfgRowExportConfig` is now 11);
  all references are symbolic so this was safe.
- **Web:** family + mode `<select>` in the config section. `/api/save` sets `g_ui_theme_changed`;
  the UI polls `plumeria::web::consumeUiThemeChanged()` in `loop()` and repaints without reboot.
- **Persistence:** `WebSettings::ui_theme` / `ui_mode` in the `plumeria_web` NVS namespace, so
  export/import config carries the theme.

### The crash that was fixed (not yet re-tested)

Selecting the Theme row panicked: `StoreProhibited`, `EXCVADDR 0x40`, backtrace at
`lv_obj_class.c:97`.

Root cause was **LVGL pool exhaustion**, not logic. The row loop built 24 × (1 button + 3 preview
swatches + 1 label) = **120 objects at once** into a 64 KB pool that already held the main UI, the
open config dialog, and up to 64 chat rows. `lv_btn_create` returned NULL and the next line
dereferenced it.

Fix: **2 objects per row** (48 total). Swatches are gone; each row previews its theme by drawing
*itself* — fill from `panel_alt`, border from `accent`, label auto-contrasted against the fill. Plus
null checks that tear the whole dialog down and report `"Theme picker: out of memory"` rather than
panicking.

**If it still exhausts the pool**, you now get that message instead of a reboot. Next lever: drop the
buttons for clickable labels, halving to 24 objects.

Two latent bugs found while fixing this:
- `style_button_active_` selection highlight would never have rendered — **local styles outrank
  added styles in LVGL**, and rows now set colors locally. Selection is a local 3px focus border.
- `lv_obj_set_width(label, LV_PCT(100) - (swatch_x + 6))` was arithmetic on `LV_PCT`'s sentinel
  encoding. Gone with the swatches.

### Known-unverified / likely to need tuning

- **Light modes generally.** Blend factors in `derivePalette()` are aesthetic guesses never seen on
  a screen. `text_dim` contrast is the most likely first casualty.
- Semantic colors (RX/TX/ACK/error/WiFi) deliberately keep their hue across all themes and only
  darken in light mode — theming them to the accent would make them unlearnable.
- Confirm-dialog buttons stay dark in both modes on purpose; their labels are white.
- Web picker has never been loaded in a browser.

---

## 2. Heltec V4 removal

User's call: *"There's a fantastic firmware called wadamesh for the heltec and I'd rather people use
that."*

102 `DEVICE_HELTEC_V4_EXPANSION` sites across 7 files, plus `boards/heltec_v4.json`. There was
**never a heltec env in platformio.ini**, so this was already dead code nothing compiled.

Method: `/usr/bin/unifdef -m -UDEVICE_HELTEC_V4_EXPANSION` on the 7 files, then 4 compound conditions
simplified by hand (unifdef leaves partially-known expressions alone):

- `defined(DEVICE_TDECK) || defined(DEVICE_HELTEC_V4_EXPANSION)` → `defined(DEVICE_TDECK)` (×2)
- `defined(DEVICE_CARDPUTER_LORA_HAT) || (defined(HELTEC) && defined(DEVICE_UI_VERTICAL))` → cardputer only
- the 4-way buzzer condition in `triggerMessageNotificationChime()`

`PLUMERIA_HAS_HELTEC_BUZZER_BACKEND` and `HELTEC_COMPACT_SELECTOR` disappeared with their blocks.
`DEVICE_UI_VERTICAL` still exists in 2 non-Heltec spots and was left alone (also never defined).

Side effects in `standalone_ui.h`: `kCfgRowCount` is now unconditionally 14, `kContactActionCount` 3.

### Verification (worth repeating if you touch this)

Naive size comparison was inconclusive: `text/data/bss` were byte-identical, but `firmware.bin`'s
hash changed. Chased it down:

1. Relinking identical source reproduces the same hash → **build is deterministic**.
2. A **comment-only** edit (3 `// probe` lines, zero code change) left loadable sections identical at
   `418af7d9…` while `firmware.bin` changed → the `.bin` hash tracks **DWARF line numbers** via the
   embedded `app_elf_sha256` (the same value printed in panic dumps), not executable content.
3. Decisive test: restored `HEAD` versions of the 5 files the theme work never touched (so `HEAD` is
   exactly the pre-removal baseline), rebuilt, and compared `objcopy --strip-debug` hashes of each
   `.o`. **All 5 byte-identical**, covering 29 sites including every `#elif` chain.

Note `-ffunction-sections` is on, so `objcopy -j .text` extracts nothing — use `--strip-debug` and
hash the whole object.

---

## Immediate next steps

1. **Flash T-Deck, open Config → Theme.** Confirm no panic, and that Plumeria Dark is visually
   identical to v1.8.2.
2. Switch to a light theme; check chat/contacts/live-feed readability, especially dim text.
3. Reboot and confirm the theme persisted.
4. Exercise the web picker.
5. `git add src/ui/ui_theme.{h,cpp}` — currently untracked and would be lost by a careless `git
   stash`/`checkout`.
6. Nothing is committed. No release cut; VERSION untouched at `v1.8.2`.

## Open GitHub issues from this session

- **#1** OTA: drop TLS, HTTP reverse proxy + ECDSA-signed firmware (camillia-style)
- **#2** Themes — *this work*
- **#3** Bubble/outline chat rendering with optional colors
- **#4** Emoji support — fallback font + on-device picker

#3 and #4 both interact with themes: bubbles must consume the palette rather than defining their own
colors, and emoji change line height. #4's memory analysis matters — camillia budgets 5 `lv_tiny_ttf`
instances against a 96–128 KB LVGL pool, and plumeria's is 64 KB on tdeck/pager. The theme picker
already proved that pool is tight.
