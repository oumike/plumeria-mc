# plumeria-mc

Empty multi-target ESP32 firmware scaffold based on the same release/build flow used in camillia-mt.

Current focus target: LilyGo T-Lora Pager TFT (`tlora-pager-tft`).

This firmware is currently **standalone-only** (no companion transport mode).

## Targets

- `tlora-pager-tft` (active/default)
- `tdeck` (board + radio + display + basic trackball input wired)
- `cardputer-cap` (placeholder)
- `heltec-v4` (placeholder)
- `heltec-v4-vertical` (placeholder)

## Quick Start

1. Install PlatformIO CLI.
2. Build default target:

```bash
pio run
```

3. Upload + monitor with helper script:

```bash
./build-upload-monitor.sh --pager
```

## Scripts

- `build-upload-monitor.sh`: interactive/selectable build, upload, and monitor flow.
- `release.sh`: bumps `VERSION`, builds release envs, commits, tags, and pushes.

## Graphics

LVGL is included as a project dependency and initialized in firmware startup.

- Dependency: `lvgl/lvgl@8.3.11`
- Current status: tlora-pager display flush + trackball key input drivers are wired.
- Next step: extend input handling to full keyboard integration.

## Architecture

The firmware now uses a modular standalone structure:

- `src/mesh/mesh_adapter.*`: standalone MeshCore boundary (identity load, radio init, mesh loop, send APIs).
- `src/hal/device_board.*`: board abstraction + per-device radio pin/radio defaults.
- `src/hal/device_lvgl.*`: LVGL display + input driver bridge with per-device mappings.
- `src/ui/standalone_ui.*`: unique LVGL shell for on-device operation.
- `src/config/features.h`: feature toggles (companion disabled by design).

Mesh adapter persistence:

- Identity persisted using MeshCore `IdentityStore`.
- Contacts persisted to SPIFFS (`/mesh_contacts.bin`).
- Channels persisted to SPIFFS (`/mesh_channels.bin`).

## CI/CD

GitHub Actions workflow at `.github/workflows/build.yml` builds known hardware envs present in `platformio.ini` and creates a draft release when a `v*.*.*` tag is pushed.

## Use of AI

Hello!  I've been a developer professionally since about 2001 working on a large list of technologies.  I've created this project in my spare time so I could contribute to my favorite new hobby (mesh networking) and try out coding with an AI partner (Claude).  Lots of this code has been touched by AI but as I go through the process I'm reviewing the code.  AI is tool, and like any other tool can be used well or used poorly.

This project is a bit more than a proof of concept but not something that has any commercial value.  I'm doing this for fun and to learn.  Feel free to contribute, use or ignore.

## License

GNU General Public License v3.0 (GPLv3)