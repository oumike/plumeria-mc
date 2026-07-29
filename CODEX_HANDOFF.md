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

---

# Contact capacity + CSV export (issue #9)

**Limit.** `PLUMERIA_MAX_CONTACTS` (default 128, `src/mesh/contact_capacity.h`) caps contacts kept in
memory/NVS; `MeshAdapter::contactLimit()` clamps it to MeshCore's compile-time `MAX_CONTACTS`, so
lowering it in `platformio.ini` works but raising it past `MAX_CONTACTS` does nothing.

**Eviction.** `MeshAdapter::ensureContactCapacityForInsert()` is called *before* every insert (advert
path in `StandaloneChatMesh::onAdvertRecv`, plus `importContactByPublicKeyHex` and contact load) and
FIFO-evicts the oldest non-favorite (`selectFifoEvictionIndexBy`, ordered by first-seen, not
`lastmod` and not name). Favorites (`ContactInfo::flags & 0x01`) are never evicted; when only
favorites remain the insert is refused — `block_auto_add_` makes MeshCore's
`shouldAutoAddContactType()` return false for that one advert so the limit can't be exceeded, and
`contactsRejectedCount()` counts it. Vendored MeshCore is **unmodified**: its own
`shouldOverwriteWhenFull()`/`lastmod` policy stays off.

**First-seen.** Kept out of `ContactInfo` (vendored layout untouched) in a file-scope table
`g_contact_first_seen` in `mesh_adapter.cpp`, keyed by an 8-byte pubkey prefix, persisted in its own
NVS blob (`cfirst_blob`) and pruned/backfilled in `saveContactsToFs()`. Contacts persisted before
this feature backfill from `lastmod` at load.

**Pointer hazard.** Evicting/removing compacts MeshCore's contacts array, so
`StandaloneChatMesh::invalidateCachedContacts()` clears `pending_ack_contact_` on every removal.

**Favorite-safety backstop.** MeshCore's `addContact()` passes `transient_only = (type ==
ADV_TYPE_NONE)`, and that branch of `allocateContactSlot()` overwrites the oldest `ADV_TYPE_NONE`
contact **ignoring the favorite flag** — and contacts imported by public key are type
`ADV_TYPE_NONE`. Never call `mesh->addContact()` directly: use
`MeshAdapter::addContactWithoutOverwrite()`, which refuses when the table is physically full.
`onContactOverwrite()` is also overridden to clean the side tables and warn if the library ever
reuses a favorite's slot.

**Web.** `GET /api/contacts/export.csv` streams a download (chunked, escaping in
`src/web/csv_field.h`); `/api/status` gained `contact_count`, `contact_limit`, `contacts_evicted`,
`contacts_rejected`; `/api/contacts` gained `first_seen`.

**Tests.** `pio test -e native` runs `test/test_contacts/` (eviction policy + CSV escaping). This
required splitting `platformio.ini`'s old `[env]` into `[esp32_common]` with `extends =` on each
device env, so the native env doesn't inherit board/framework/lib_deps.

---

# WiFi network chooser (issue #10)

**Where it appears.** Onboarding's WiFi step (`openWifiSsidPrompt()` now opens the chooser instead of
a blank text prompt) and a new config-screen row `kCfgRowWifi` (inserted after Web Config —
`kCfgRowCount` went 17 -> 18 and the constants below it renumbered).

**Scan.** `startWifiScan()` / `pollWifiScan()` in `standalone_ui.cpp`. The scan is **async**
(`WiFi.scanNetworks(true, false)`) and polled from `StandaloneUi::loop()`, so the mesh loop keeps
running — a blocking scan would stall it for seconds. `WIFI_MODE_NULL` -> `WIFI_STA`, `WIFI_MODE_AP`
-> `WIFI_AP_STA` (never plain STA, so an active AP-mode web config session survives), and
`restoreWifiModeAfterScan()` puts the radio back on close. Results are de-duplicated by SSID (keeping
the strongest), hidden SSIDs are dropped, and the list is sorted strongest-first, capped at
`kWifiScanMaxCount`.

**Rows.** Networks first ("<ssid>  -57 dBm  lock/open"), then three fixed actions: Rescan / Enter SSID
manually / Skip (onboarding) or Back (config). Row index space is `[0, wifi_scan_count_)` for
networks then `+kWifiListActionRows`. Keys mirror the region picker (j/k or arrows, Enter, Esc or c,
plus r and m); touch goes through each row button's `onWifiListEvent`.

**Shared prompt state.** The password (and manual SSID) prompt reuses the onboarding compose steps —
`OnboardingStep::WifiSsid` / `WifiPass` — because the compose dialog's OK/Cancel handlers already
route on `onboarding_step_ != None` for both keyboard and touch builds. `wifi_setup_from_config_`
distinguishes the caller: `finishWifiSetup()` either reboots (onboarding) or writes
`cfg_status_text_` and returns to the config screen. Anything that exits the picker must clear
`onboarding_step_` / `identity_prompt_open_` — see `cancelWifiPicker()`.

**Not done:** the web-config scan endpoint/picker from the issue. Scanning forces a station interface
up while the browser session is riding that same radio, which risks dropping the very connection
serving the page; on-device is the priority path and covers the use case.

---

# No automatic SD writes (removed)

Onboarding used to call `exportConfigToSd()` from `finishOnboardingAndReboot()` on first install
(gated by `first_install_auto_export_pending_`, off on Cardputer). It **overwrote an existing
`/plumeria/plumeria-config.yaml` without asking and destroyed a user's backup**, so the flag and both
call sites are gone. `exportConfigToSd()` now has exactly one caller: the confirm-gated
`kCfgRowExportConfig` config row.

Do not reintroduce implicit writes to SD. The exported YAML carries `identity_private_key` and
`wifi_pass` in plaintext, and the writer does `SD.remove(target)` before writing — so any automatic
call is both a secret-disclosure and a data-loss path.

The overwrite itself is intended behavior for the manual export: a user picking "Export Config to SD"
should replace the existing file. No timestamped filenames or "file exists" guards there.

---

# AP-lite web config (issue #11)

**The gate.** `webCfgUseLite()` returns `g_ap_mode || kLiteOnlyBoard`. `g_ap_mode` is set by
`startFallbackAp()` and cleared on STA connect / `end()`; `kLiteOnlyBoard` is compile-time true for
`DEVICE_CARDPUTER_LORA_HAT`, so the Cardputer serves lite in **both** AP and STA — same reasoning as
camillia (no PSRAM, tiny internal heap once WiFi is up, and our full page is a ~28 KB PROGMEM blob
plus a CDN Leaflet map).

**The lite page** (`sendLitePage()`): server-rendered, **no JavaScript**, streamed. Static CSS head
goes out via `sendFlashChunked()` (512-byte `sendContent_P` chunks); dynamic markup accumulates in a
`String` flushed by `sendChunkIfBig(html, 700)`. Carries node identity, WiFi, radio/region, mesh
settings, device/UI settings, the channel list with add/remove, adverts, export links, and a paste-in
config import. No contacts list, no heat map, no Leaflet.

**One source of truth.** The lite forms post the *same field names* to the *same handlers* the JSON
API uses — `/save` -> `handleSaveAll`, `/lite/channels/add` -> `handleChannelAdd`, etc., via the
`lite_form()` wrapper in `registerRoutes()`. The wrapper sets `g_html_reply`, which makes
`sendJsonOk()` / `sendJsonError()` emit a small HTML result page instead of JSON (it lifts the
handler's own `"message"` out of the JSON payload). **Booleans are `<select>` 0/1, not checkboxes** —
an unchecked checkbox posts nothing, and these handlers treat "absent" as "unchanged", so a checkbox
could never turn a flag off.

**Cardputer AP fallback re-enabled.** `startFallbackAp()` no longer bails to `WIFI_OFF` on Cardputer.
It now sets `WiFi.setSleep(false)` (modem sleep stalls the synchronous WebServer until page loads
time out), starts a captive-portal `DNSServer` answering every lookup with the SoftAP IP (pumped from
`plumeria::web::loop()`), and fails soft if `softAP()` returns false. Compile out with
`-DPLUMERIA_AP_FALLBACK_ENABLED=0` if the old bring-up crash reappears.

**Diagnostics.** `logHeapDiag(tag)` prints free heap + largest free block at AP bring-up and around
the lite page serve — the largest-block number is the one that decides whether a page can be built.

**Still accumulating a whole String:** `buildConfigText()` (config export download). `handleContacts`
was converted to chunked streaming since it scales with the contact limit; the export path would need
`buildConfigText` restructured, and it is shared with the on-device SD export.

---

# OTA proxy lives in plumeria-mc-web (not in this repo)

The firmware fetches updates over plain HTTP (no TLS on device) from
`http://ota.plumeria.sumat.org` — see `kLatestReleaseApiUrl` / `kReleaseDownloadBaseUrl` in
`src/ota/ota_update.cpp`:

- `GET /firmware/latest` -> GitHub's `releases/latest` JSON (firmware parses `tag_name`)
- `GET /firmware/<tag>/plumeria-mc-<slug>-<tag>-ota.bin` (+ `.sig`) -> the release asset

That hostname is served by the **reverse proxy (Nginx Proxy Manager)**, not by the website
container. Verified 2026-07-26 against the working camillia setup: `ota.camillia.sumat.org` returns
404 for `/index.html` and `/favicon.svg` but 200 for `/firmware/latest`, so it is not forwarding to
`camillia-mt-web` — that container's `/firmware/*` routes exist for the **browser flasher**
(same-origin CORS for esp-web-tools), which is a different consumer with the same paths.

Config to paste into the proxy host: `../plumeria-mc-web/deploy/npm-ota-host.conf`.

`deploy/nginx/ota-proxy.conf` used to live here as a standalone vhost. Nothing in this repo ever
deployed it, and nginx config does not belong in the firmware repo, so it was **deleted 2026-07-26**
and the proxy-side copy now lives in `plumeria-mc-web/deploy/`.

**The remaining wiring is in Nginx Proxy Manager, not in any repo:** `ota.plumeria.sumat.org` needs a
proxy host carrying those location blocks, with **Force SSL and HSTS off** — the device cannot follow
a redirect to `https://`. As of 2026-07-26 that proxy host did not exist: `/` returned NPM's "host
isn't set up yet" default page and every `/firmware/*` request 404'd, which is the on-device OTA 404.
`http://plumeria.sumat.org/firmware/latest` is not a substitute — it 301s to https.

Diagnosing OTA 404s: `curl -i http://ota.plumeria.sumat.org/firmware/latest`. An openresty HTML 404
means the proxy answered locally (host/config missing); a GitHub JSON `{"message":"Not Found"}` means
the proxy works and the release or asset name is wrong.

---

# Space opens compose; per-contact chat colors (issues #5, #3 follow-up)

**Space, not Enter** (`standalone_ui.cpp`, `handleKey`). The global compose shortcut now fires on
`' '` as well as `m`, and additionally excludes the admin screens (`admin_pw_open_`,
`admin_cmd_open_`, `admin_screen_open_`) — those blocks sit *after* the shortcut in `handleKey`, so
space would otherwise have hijacked them. Enter no longer opens compose from the chat zone or the DM
body. Enter *is* still accepted on the explicitly focused `chat_new_btn_` / `dm_new_btn_` (that is
button activation, not a view-level shortcut) — both now take Space too. Hint labels and
`kHelpBodyText` updated.

**Bugfix found while there:** `kCfgRowLabels[]` still had 17 entries after the WiFi config row pushed
`kCfgRowCount` to 18, so the row-creation loop read one past the end. Added "WiFi" at index 3. Keep
that array in sync with the `kCfgRow*` constants.

**Contact colors.** Two defects made colors look random rather than per-contact:

1. `getOrCreateContactColorSlot()` derived the initial slot from
   `stableTextHash(id) ^ millis() ^ nowEpochSecondsOrZero()` — the color depended on *when* a contact
   was first rendered, so it changed whenever the persisted table was lost, full (the >96 fallback
   uses a plain hash), or not yet loaded. Now derived from the identity hash alone.
2. The DM views key colors by **public key**; the main chat panel passes no identity, so it falls back
   to the **display name** parsed out of the line. Same person, two table entries, two colors.
   `linkContactColorAlias(primary, alias)` now registers the key/name pair from the three DM render
   paths. Rule: whichever identity already has a slot wins, so linking never repaints a conversation
   already on screen; only a new identity adopts the other's color.

Note both entries share the 96-slot table, so a DM contact costs two entries. Existing persisted
slots are left as-is, so colors assigned before this change do not move.
