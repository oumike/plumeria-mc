# Plumeria for MeshCore

**Website:** : <https://plumeria.sumat.org/>

Empty multi-target ESP32 firmware scaffold based on the same release/build flow used in camillia-mt.

Current default target: Cardputer + Cap LoRa/GPS (`cardputer-cap`), with multi-hardware support across listed environments.

This firmware is currently **standalone-only** (no companion transport mode).

## Targets

- `cardputer-cap`
- `tlora-pager-tft`
- `tdeck` (board + radio + display + basic trackball input wired)

## Quick Start

1. Install PlatformIO CLI.
2. Build default target (Cardputer + Cap LoRa/GPS):

```bash
pio run
```

3. Upload + monitor with helper script:

```bash
./build-upload-monitor.sh --cardputer
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

- Identity persisted in NVS via `Preferences`.
- Contacts persisted in NVS via `Preferences`.
- Channels persisted in NVS via `Preferences`.

## CI/CD

GitHub Actions workflow at `.github/workflows/build.yml` runs only when a GitHub release is published.

## Use of AI

Hello!  I've been a developer professionally since about 2001 working on a large list of technologies.  I've created this project in my spare time so I could contribute to my favorite new hobby (mesh networking) and try out coding with an AI partner (Claude).  Lots of this code has been touched by AI but as I go through the process I'm reviewing the code.  AI is tool, and like any other tool can be used well or used poorly.

This project is a bit more than a proof of concept but not something that has any commercial value.  I'm doing this for fun and to learn.  Feel free to contribute, use or ignore.

## License

This project is licensed under the **MIT License.**

### Copyright 2026 Michael A. Cojocari
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
