# rp2-atarist-rpikb-booster

Configuration app for the **rp2-atarist-rpikb** firmware. It runs on the
Booster firmware and exposes a web UI so you can configure Bluetooth/USB input
devices and system settings without extra tools.

> 🛒 **Get the hardware:** [Croissant](https://sidecartridge.com/products/sidecartridge-croissant-atari-st/) or [Souffle](https://sidecartridge.com/products/sidecartridge-souffle-mega-st/)

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

## Souffle factory test

A bench check for newly assembled Souffle boards: it installs a published
release with a factory test switched on, which checks the USB keyboard port
and all twelve joystick and mouse lines, then leaves a normal Souffle behind
after a factory reset. Croissant has no factory test.

You need:
- A clone of this repository and Python 3 (no other packages).
- The bench Wi-Fi the tested board joins to show its result, in `factory.env`
  at the repository root (gitignored):
  ```
  FACTORY_WIFI_SSID=<network>
  FACTORY_WIFI_PASSWORD=<password>
  FACTORY_WIFI_AUTH=5        # optional: 5 WPA2-AES (default)
  FACTORY_WIFI_COUNTRY=ES    # optional: XX (worldwide) by default
  ```
  Without it, `tools/dev/bench.env` (`BENCH_WIFI_*`) is used.
- A USB keyboard, the joystick-port test harness that holds all twelve lines
  low, and an Atari Mega ST or Mega STE to power the board.

Steps:

1. Run the launcher. It takes the version from `version.txt`, or the one you
   give it:
   ```sh
   python3 factory_launch.py           # or: python3 factory_launch.py v1.3.0
   ```
   In `dist/factory/` it downloads the published Souffle release image (or
   uses the one already there), checks it is complete, and makes the factory
   image next to it. Then it waits for a board.
2. Hold BOOTSEL on the Souffle while plugging its micro-USB into the computer.
   The launcher copies the factory image onto the board's drive, and the board
   restarts by itself as soon as the copy completes.
3. Unplug the micro-USB and connect the Souffle to the Mega ST(E). On
   micro-USB power alone the USB-A port is unpowered, so the keyboard would
   not work.
4. Plug the USB keyboard into the USB-A port and the harness into the joystick
   ports, and power on the computer. Type `a`, `b`, `c`, `d` and `e` on the
   keyboard, and leave the harness in for at least 10 seconds. Do not press
   CONFIG: that ends the test as a failure.
5. Once both checks have passed, the board saves the result and starts the
   configuration app on the bench Wi-Fi. From a computer on that network, open
   `http://souffle.local/factory.shtml`: it shows PASS or FAIL, the keys
   typed, the keyboard's USB ID, how long the lines were held, and each line.
6. Press **Factory reset** on that page and confirm. The board erases the factory
   settings and the bench Wi-Fi and comes back as a new Souffle, on its own
   access point.

For the next board, run the launcher again: it reuses both images in
`dist/factory/`. Options: `--dir DIR` keeps the images elsewhere,
`--no-flash` only makes them, and `--timeout S` waits longer than 300 seconds
for a board in BOOTSEL. The factory image carries the bench Wi-Fi password:
keep it on the bench, never publish or commit it.

## License

This project is licensed under the GNU General Public License v3.0. See the LICENSE file for details.

## Copyright

Except where otherwise noted, this project is copyright of GOODDATA LABS S.L. All rights reserved.
