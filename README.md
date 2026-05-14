# rp2-atarist-rpikb-booster

Configuration app for the **rp2-atarist-rpikb** firmware. It runs on the
Booster firmware and exposes a web UI so you can configure Bluetooth/USB input
devices and system settings without extra tools.

## What it does

When flashed, the Booster creates a Wi-Fi access point you can connect to and
use to configure the rp2-atarist-rpikb firmware.

## Quick start

1. Flash the Booster UF2 to the device.
2. Connect your computer or phone to the Wi-Fi network:
   - `BOARD_TARGET=1` (CROISSANT): SSID `croissant-UUID`
   - `BOARD_TARGET=2` (SOUFFLE): SSID `souffle-UUID`
   - Password: `sidecart`
3. Open the configuration UI in your browser:
   - `BOARD_TARGET=1` (CROISSANT): `http://croissant.local`
   - `BOARD_TARGET=2` (SOUFFLE): `http://souffle.local`

Notes:
- Only HTTP is supported (no HTTPS).
- If the `.local` hostname does not resolve, use the IP shown on the device (if
  available) or your OS's connected network details.

## Factory reset

If the device gets stuck in a bad configuration — wrong Wi-Fi credentials,
broken Bluetooth pairings, or you simply want to hand it to someone else —
there is a hidden recovery page. It is **not linked from any other screen**;
you have to type the URL yourself:

- `BOARD_TARGET=1` (CROISSANT): `http://croissant.local/factoryreset.shtml`
- `BOARD_TARGET=2` (SOUFFLE): `http://souffle.local/factoryreset.shtml`

The page shows a warning and a single confirm button. Clicking it wipes every
saved setting, forgets every paired Bluetooth keyboard / mouse / gamepad, and
reboots the device straight back into configuration mode so you can set it up
from scratch.

This action cannot be undone.

## Project structure

This repo builds:
- `booster/` — main firmware (web UI + config endpoints)
- `placeholder/` — fallback app used when no app is installed
- `rp2-atarist-rpikb/` — core IKBD firmware (optional in full build)

See `AGENTS.md` for build and workflow details.

## Building

Use the top-level build script:

```sh
./build.sh <board_type> <build_type> [release_type] [board_flavor]
```

Arguments:
- `board_type`: `pico_w`, `pico2`, `pico2_w` (default: `pico2_w`)
- `build_type`: `release` or `debug` (default: `release`)
- `release_type`: optional. If provided, the script also builds
  `rp2-atarist-rpikb/` and produces a merged full image.
- `board_flavor`: `croissant`, `souffle`, `1`, or `2` (default: `souffle`)

Examples:

```sh
# Firmware-only (booster + placeholder), no final merged image
./build.sh pico2_w release
./build.sh pico2_w release "" croissant

# Full image build (booster + placeholder + core IKBD)
./build.sh pico2_w release final souffle
./build.sh pico2_w release final croissant

# Debug full image build
./build.sh pico2_w debug final souffle
```

Notes:
- `board_flavor` maps to `BOARD_TARGET` internally:
  - `croissant` / `1` -> `BOARD_TARGET=1`
  - `souffle` / `2` -> `BOARD_TARGET=2`
- `booster/build.sh` uses `booster/src/CMakePresets.json` presets:
  - `croissant-debug`, `souffle-debug`, `croissant-release`, `souffle-release`
- The merged image step is skipped when `release_type` is omitted.

## License

This project is licensed under the GNU General Public License v3.0. See the LICENSE file for details.

## Copyright

Except where otherwise noted, this project is copyright of GOODDATA LABS S.L. All rights reserved.
