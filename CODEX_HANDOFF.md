# Codex Handoff — plumeria-mc

Context for continuing work on this firmware. Written at the end of a session that added several
features; read the whole thing before touching code — codex starts cold and this captures the
non-obvious layering.

## Project basics
- ESP32 multi-target MeshCore firmware (PlatformIO). Targets in `platformio.ini`.
- Default/dev target: `cardputer-cap` (Cardputer + LoRa cap, **no SD card**, physical keyboard).
- Other enabled target for verifying touch + SD paths: `tdeck` (touchscreen, has SD).
- Build (fast, ~10s each):
  ```
  ~/.platformio/penv/bin/pio run -e cardputer-cap
  ~/.platformio/penv/bin/pio run -e tdeck
  ```
- **Everything below currently compiles clean on both `cardputer-cap` and `tdeck`.** Builds only
  prove compilation — none of the UI flows have been exercised on hardware this session.

## Critical layering note (do not "fix" by moving things)
`src/web/web_config.*` is **misleadingly named**. It is the firmware's shared **settings/persistence
module** (`WebSettings` struct, NVS load/save, and the `plumeria::web::setX(...)` setters that apply
settings to the mesh/radio/wifi). The web server is only one consumer. The **on-device config screen
and the onboarding wizard call into `plumeria::web::` too** (e.g. `setNodeName`, `setMultiAck`,
`setRepeaterMode`, `loadSettings`). Radio region + WiFi creds live in this NVS store and are applied
at boot. So persistence for onboarding correctly goes through `plumeria::web::`, NOT a new module.

## Key files
- `src/mesh/mesh_adapter.cpp` / `.h` — `MeshAdapter` (public API) wraps `StandaloneChatMesh :
  BaseChatMesh : mesh::Mesh` (MeshCore vendored at `third_party/MeshCore`, library v1.10.0).
- `src/ui/standalone_ui.cpp` (~10k lines) / `.h` — all on-device LVGL UI.
- `src/web/web_config.cpp` / `.h` — settings/persistence module (see note above).
- `src/main.cpp` — boot; fresh-install detection.

---

# Features added this session (all compiling)

### 1. Contact delete button
- Contacts screen: `(D)el` button in the **header bar** (`header_bar_`, right-aligned), shown only
  while contacts is open (the wifi/gps/clock cluster is hidden then, freeing the right side — see
  `refreshHeaderVisuals()`). Keyboard `d` deletes (with confirm); `(D)M` was rebound to `m`.
- Delete uses `mesh_adapter_->removeContactByPublicKeyHex(...)`.

### 2. Generalized confirmation modal
- The old `cfg_confirm_*` modal was renamed to `confirm_*` and generalized: `ConfirmKind` enum
  (`None, CfgRow, ContactDelete, ImportFirstInstall, RegionDefault`), `openConfirmDialog(kind,
  title, body, guard_ms, yes_label=nullptr, no_label=nullptr)`, `acceptConfirmDialog()` dispatches
  by kind, `declineConfirm()` handles No/Esc per kind.
- Modal key/click handling is **hoisted** above the per-screen branches in `handleKey` /
  `handleClick` (search `confirm_open_`). Backdrop is parented to `root_` so it overlays any screen.

### 3. Repeater mode
- `StandaloneChatMesh::allowPacketForward()` overridden to return `repeater_enabled_` (this IS how
  MeshCore defines a repeater). When on, self-advert type switches to `ADV_TYPE_REPEATER`
  (`createTypedSelfAdvert()`).
- Adapter: `setRepeaterMode(bool)` / `getRepeaterMode()`.
- Config screen row `kCfgRowRepeater` (confirm-on-enable, warning text) + web config checkbox with
  warning + full NVS/import/export/apply wiring (mirrors `multi_ack`). Defaults OFF on fresh install.

### 4. First-install onboarding wizard
Replaces the old silent SD auto-import. Flow (all in `standalone_ui.cpp`, `OnboardingStep` enum):
`startOnboarding()` → [Import? confirm, only if SD config w/ identity keys] → node name → radio
region (`RegionDefault` confirm: "Use US" / "Change" → `openRegionListDialog()` scrollable list) →
WiFi SSID → WiFi password (blank/Esc skips) → `finishOnboardingAndReboot()`.
- `main.cpp` now only **detects** an importable SD config and calls
  `g_ui.setFirstInstallImportAvailable(true)` (no auto-import). Import "Yes" calls
  `importConfigFromSd()` (imports + reboots).
- web_config additions: `regionPresetCount/Id`, `defaultRegionId`, `setRegionPreset(id)`,
  `setWifiCredentials(ssid,pass)` — persist without rebooting (caller reboots).
- Region presets = `kRegionPresets[]` in `web_config.cpp` (US default, EU_868, EU_433, ANZ, JP, KR,
  IN, TH, BR_902).

---

# OPEN ISSUE — fix this first (needs on-device iteration)

**Symptom (reported by user):** During onboarding, the WiFi SSID / password text input **moves up
and down while typing**.

**Where:** `StandaloneUi::openOnboardingComposePrompt(placeholder, max_len, allow_skip)` in
`standalone_ui.cpp` (reuses the message `compose_dialog_` / `compose_input_` as a single-line
prompt; also used for the node-name step).

**Diagnosis / hypothesis:** `compose_dialog_` is created with `LV_FLEX_FLOW_COLUMN` (search
`lv_obj_set_flex_flow(compose_dialog_`). The onboarding prompt then positions the input with a
**manual `lv_obj_align(compose_input_, LV_ALIGN_BOTTOM_MID, ...)`** (the `else` / non-onscreen-kbd
branch) — manual align fights the flex layout. A layout pass triggered while typing snaps the input
between the flex-computed and align-computed positions → vertical jitter. The on-screen-keyboard
branch (`kUseOnscreenKeyboard`, tdeck) sets sizes but may also thrash when the keyboard shows/hides.

**Suggested fix directions (verify on hardware — cardputer for physical kbd, tdeck for touch):**
- Stop fighting flex: for the single-line prompt, either set `compose_input_` flex properties and
  drop the manual `lv_obj_align`, or temporarily `lv_obj_set_layout(compose_dialog_, LV_LAYOUT_NONE)`
  for the prompt and restore flex in `closeComposeDialog`. Watch the button row / hint label don't
  break.
- Confirm nothing re-runs layout per keystroke (grep `refreshComposeDialog` callers; it only sets
  label text today, but check for a `LV_EVENT_VALUE_CHANGED` handler on `compose_input_`).
- The node-name step uses the same code, so a correct fix helps all three onboarding text steps.

---

# Fixed this session (verify on hardware)
- **Advert during onboarding (root cause found & fixed):** `MeshAdapter::setRepeaterMode` was
  broadcasting a flood advert **ungated** whenever called with `ready_`. Boot-time web-config apply
  calls `setRepeaterMode(false)`, firing an advert even on a fresh install. Now gated:
  `if (ready_ && changed && adverts_unlocked_for_boot_) broadcastSelfAdvertFlood();`. All other
  advert paths were already gated by `adverts_unlocked_for_boot_` (false until identity loaded from
  NVS). **Verify:** fresh install emits NO advert until onboarding completes + reboots.
- Compose title is now step-aware (shows "WiFi SSID"/"WiFi Password" instead of "Identity Name").

# Minor loose ends (low priority)
- WiFi step's Skip affordance works via the compose Cancel button / Esc, but the button label still
  reads its default ("Cancel"), not "Skip". `openOnboardingComposePrompt` has an unused `allow_skip`
  param intended for this.
- `StandaloneUi::applyIdentityNameFromPrompt()` is now **dead code** (superseded by
  `commitOnboardingText()`); safe to delete.
- Password field is intentionally **not** masked (user explicitly wants plaintext) — leave as is.

# Verification still needed on hardware (nothing below was device-tested)
- Full onboarding flow on tdeck (import prompt path) and cardputer (no-SD path: name → region →
  wifi → reboot).
- Region-list picker nav (up/down/enter on cardputer; tap on tdeck) and small-screen scrolling.
- Contact delete button placement/tap; repeater mode actually relaying a 3rd node's traffic.
