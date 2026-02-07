# rp2-atarist-rpikb-booster

Configuration app for the **rp2-atarist-rpikb** firmware. It runs on the
Booster firmware and exposes a web UI so you can configure Bluetooth/USB input
devices and system settings without extra tools.

## What it does

When flashed, the Booster creates a Wi‑Fi access point you can connect to and
use to configure the rp2-atarist-rpikb firmware.

## Quick start

1. Flash the Booster UF2 to the device.
2. Connect your computer or phone to the Wi‑Fi network:
   - SSID: `croissant-UUID`
   - Password: `sidecart`
3. Open the configuration UI in your browser:
   - `http://croissant.local`

Notes:
- Only HTTP is supported (no HTTPS).
- If `croissant.local` does not resolve, use the IP shown on the device (if
  available) or your OS’s connected network details.

## Project structure

This repo builds:
- `booster/` — main firmware (web UI + config endpoints)
- `placeholder/` — fallback app used when no app is installed
- `rp2-atarist-rpikb/` — core IKBD firmware (optional in full build)

See `AGENTS.md` for build and workflow details.

## Building

Use the top-level build script:

```sh
./build.sh <board_type> <build_type> [release_type]
```

Examples:

```sh
# Firmware-only (booster + placeholder), no final merged image
./build.sh pico_w release
./build.sh pico2_w release

# Full image build (includes core IKBD merge): third arg required
./build.sh pico_w release full
./build.sh pico2_w release full

# Debug full image build
./build.sh pico_w debug full
./build.sh pico2_w debug full
```

Note: in `build.sh`, the final merged image step is skipped when the third argument is omitted.


## License

This project is licensed under the GNU General Public License v3.0. See the LICENSE file for details.

## Copyright

Except where otherwise noted, this project is copyright of GOODDATA LABS S.L. All rights reserved.
