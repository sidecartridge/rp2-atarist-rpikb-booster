# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

See also: `AGENTS.md` (project layout, build prereqs, agent workflow rules, expected artifacts) and `README.md` (user-facing behavior, Wi-Fi defaults, hostnames). This file is the deeper technical map — boot flow, request handling, flash layout, build pipeline, and the cross-firmware handoff protocol.

---

## Build commands

Top-level `build.sh <board_type> <build_type> [release_type] [board_flavor]`:

```sh
./build.sh pico2_w release "" souffle          # booster firmware only
./build.sh pico2_w release final souffle       # full merged image (booster + core IKBD)
./build.sh pico2_w debug   "" croissant        # debug, Croissant flavor
```

`board_flavor` is `croissant` (`BOARD_TARGET=1`) or `souffle` (`BOARD_TARGET=2`). `board_type` defaults to `pico2_w` (RP2350); `pico_w` (RP2040) is also accepted. `release_type` is consumed by `booster/build.sh` to select a `version-<type>.txt` file (e.g. `version-beta.txt`); `final` and empty both map to plain `version.txt`. Only when this arg is non-empty does the root script also build the core IKBD firmware and run `merge_uf2.py`.

Two important side effects of root `build.sh`:
- `rm -rf build/` at the start.
- Force-syncs `rp2-atarist-rpikb` to `origin/main` and **fails the build if HEAD ≠ origin/main**. Don't run it when the submodule has uncommitted/in-progress core-firmware work unless you've pushed it. `AGENTS.md` covers the non-destructive submodule rules in detail.

Iterating on booster-only changes is faster via `cd booster && ./build.sh pico2_w release "" souffle` — same flags, skips the core firmware build/merge. Internally this delegates to CMake presets in `booster/src/CMakePresets.json` (`croissant-debug`, `souffle-debug`, `croissant-release`, `souffle-release`) and writes to `booster/build-<flavor>-<kind>/`, then copies `booster.uf2` to `booster/dist/`. `booster/build.sh` only wipes its own preset-specific build dir, so other-flavor caches survive.

Manual CMake invocation (bypassing both scripts):

```sh
export PICO_SDK_PATH=$PWD/pico-sdk PICO_EXTRAS_PATH=$PWD/pico-extras BLUEPAD32_ROOT=$PWD/bluepad32
export BOARD_TARGET=2  # 1=Croissant, 2=Souffle
cd booster/src
cmake --preset souffle-release && cmake --build --preset souffle-release
```

No test suite. Verification is "builds, produces UF2, no clang-tidy regressions in touched files." Lint config: `.clang-format` and `.clang-tidy` at repo root.

## High-level architecture

This is a **single-binary Pico W / Pico 2 W firmware** — the configuration app for the SidecarTridge IKBD system. There is no RTOS. All work runs cooperatively on core 0: the CYW43 driver background context, lwIP, BTstack, and the manager loop share one thread.

The Booster firmware's job is **configuration only**. Actual Atari ST IKBD emulation lives in a separate firmware (`rp2-atarist-rpikb/`, a submodule). The two binaries are merged into a single UF2 by `merge_uf2.py` and flipped between at boot via a shared settings flag — see "Cross-firmware handoff" below.

### Module layout (`booster/src/`)

| File | Responsibility |
| --- | --- |
| `main.c` | Power/clocks bring-up, flash layout asserts, settings init, hand off to `mngr_loop`. |
| `mngr.c` | Wi-Fi mode selection (AP/STA), STA→AP fallback, httpd start, main poll loop. |
| `network.c` | CYW43 init, AP setup with DHCP, STA connect (incl. BSSID→SSID resolution), scanning, mDNS, country/auth code translation, max-range radio profile. |
| `mngr_httpd.c` | All CGI handlers, all SSI tag resolvers, JSON payload formatting, base64+url-decoding of POSTed config. |
| `btloop.c` | Bluepad32/BTstack glue: scan/discover/pair callbacks, device classification, pairing persistence in settings. |
| `gconfig.c` | Single global `SettingsContext`, default key/value table, `PARAM_BOOT_FEATURE` handshake key. |
| `settings/settings.c` | Magic-versioned flash-backed key/value store (generic, reusable). |
| `cjson/`, `dhcpserver/` | Vendored cJSON and Micropython's DHCP server. |
| `fsdata_srv.c` | **Generated** at build time from `fs/`. Never edit by hand. |
| `version.c` | Holds `RELEASE_VERSION` / `RELEASE_DATE` strings injected via CMake. |
| `memmap_booster*.ld` | Custom flash layout — see "Flash layout". |

### Boot sequence (`main.c`)

1. **USB controller shutdown** *before* clocks are touched. Without this, if a USB device is connected at power-on the Pico hangs when the SDK reconfigures clocks. Both RP2040 and RP2350 are affected (RP2350 also routes USBPHY to GPIO).
2. Sets voltage (`vreg_set_voltage(RP2040_VOLTAGE)` — `VREG_VOLTAGE_1_10`) and overclocks to `RP2040_CLOCK_FREQ_KHZ=125000` (the constant name is legacy — value is shared between RP2040 and RP2350 builds).
3. Drives `KBD_ATARI_OUT_3V3_GPIO` high, `KBD_USB_OUT_3V3_GPIO` low. The two GPIOs are *swapped* between Croissant (`ATARI=7, USB=8`) and Souffle (`ATARI=8, USB=7`) — see `constants.h:34-46`.
4. `gconfig_init(NULL)`. If the flash magic doesn't match, writes defaults and re-inits. Recovery is in-place: a fresh device boots straight into a sane state.
5. **Writes `PARAM_BOOT_FEATURE=IKBD` and saves**. This is the cross-firmware handshake — see next section.
6. `mngr_init()` + `mngr_loop()`. Never returns.

### Cross-firmware handoff (important — easy to miss)

The Booster and the core IKBD firmware are two separate binaries occupying different flash regions in the same merged UF2. A small **boot selector** (built into the core firmware) reads `PARAM_BOOT_FEATURE` from the global config flash region at startup and jumps to whichever image that key points to (`"IKBD"` or `"BOOSTER"`).

- The Booster sets `PARAM_BOOT_FEATURE=IKBD` *before* entering its loop. So on the **next** reset/power cycle, the system boots into the IKBD core firmware.
- The web UI's "reboot to IKBD" button just triggers a reset — the work has already been done.
- To get back into Booster, the user must press a board-specific button (Souffle: `CONFIG`) or hold reset for 10 s on Croissant. The boot selector reads that input and writes `PARAM_BOOT_FEATURE=BOOSTER` before jumping.
- `gconfig_init()` takes an optional `currentAppName`. If the boot selector passes `"BOOSTER"` and the stored value doesn't match, it returns `GCONFIG_MISMATCHED_APP`. The Booster itself passes `NULL` so the check is bypassed.

If you change `PARAM_BOOT_FEATURE` semantics, you must also update the boot selector in `rp2-atarist-rpikb/`.

### Manager loop (`mngr.c`)

`mngr_init()`:
- Reads `PARAM_WIFI_MODE` (default `0` = AP).
- AP mode: hands off to `network_wifiInit(WIFI_MODE_AP)`.
- STA mode: 3 retry attempts. On total failure: **rewrites the AP credentials/hostname back into settings**, calls `network_deInit()`, then `network_wifiInit(WIFI_MODE_AP)`. So a bad STA config self-recovers to the discoverable AP after one boot cycle — but it also overwrites whatever Wi-Fi config the user saved. Watch for this if you ever change STA error handling.
- Calls `btloop_disable()` (BT is opt-in; the user starts scanning from the web UI via `/btstart.cgi`).
- `mngr_httpd_start()` sets up the lwIP httpd with CGI + SSI tables.

`mngr_loop()`:
- Forever loop, marked `__not_in_flash_func` (must stay in RAM — flash writes for BT TLV and settings save happen on this same core, and code executing from flash would stall).
- Wait window: `MNGR_WAIT_PAIRING_MS=10` while BT is active, `MNGR_WAIT_HTTPD_MS=10` otherwise. Currently identical, but the structure is there for tuning.
- `cyw43_arch_wait_for_work_until` is the idle primitive (this is the threadsafe-background CYW43 build, so the driver and lwIP run in an async context).
- LED: `KBD_USB_OUT_3V3_GPIO` blinks at `MNGR_BLINK_PERIOD_MS=500`. `KBD_ATARI_OUT_3V3_GPIO` stays high.

### Network state machine (`network.c`)

**AP mode (`network_wifiInit(WIFI_MODE_AP)`):**
- SSID is `<hostname>-<pico-serial>`, where hostname comes from `PARAM_HOSTNAME` (default `BOARD_CODENAME` = `croissant` or `souffle`). The serial is the 8-byte unique board ID as 16 hex chars. The hostname prefix is truncated if needed to keep the suffix visible.
- Static IP `192.168.4.1/24`. Internal DHCP server (Micropython's port) hands out leases.
- mDNS: `<hostname>.local` resolving via `pico_lwip_mdns`. Service name `<board>_httpd` over `_http._tcp`.
- Defaults are *written back to flash* on first AP init if missing (`PARAM_HOSTNAME`, `PARAM_WIFI_PASSWORD`, `PARAM_WIFI_AUTH`). Subsequent boots skip this.
- Auth defaults to `WIFI_AP_AUTH=5` → `WPA2_AES_PSK`. Password `sidecart` (in `WIFI_AP_PASS`).
- Country code: `PARAM_WIFI_COUNTRY` (default `XX` = worldwide). Validated against a hardcoded ISO-3166-alpha-2 allowlist in `getCountryCode()`.

**STA mode (`network_wifiStaConnect`):**
- `PARAM_WIFI_SSID` can hold either an SSID or a BSSID (`xx:xx:xx:xx:xx:xx`). BSSID values trigger an extra scan to resolve the SSID first (cached scan results, falling back to a fresh 6 s scan). This is so a user with multiple APs sharing an SSID can pin to a specific radio.
- BSSID-directed connect that fails will retry once as SSID-only join (radio may have moved channels).
- Async connect with 30 s timeout (`NETWORK_CONNECT_TIMEOUT`). Polled via `cyw43_tcpip_link_status` translated through `wifi_sta_conn_status_t`.

**Radio tuning (both modes):**
- Power management disabled (`CYW43_NONE_PM`) — max throughput, max power draw.
- `qtxpower=127` (FW-clamped) → request maximum legal TX power.
- AP mode forces 2.4 GHz multicast/basic rate to 1 Mbps (`2g_mrate`) for better edge coverage. Range over speed.
- Chip antenna forced (`CYW43_IOCTL_SET_ANTDIV=0`).

### HTTP request handling (`mngr_httpd.c` + `fs/`)

lwIP's bundled httpd, SSI + CGI. Templates and assets live in `fs/`. **`fsdata_srv.c` is generated by `external/makefsdata`** (a Perl script — a forked lwIP version that adds `text/css` MIME support, sourced from `github.com/sidecartridge/lwip` per the CMakeLists comment). CMake regenerates it whenever any file under `fs/` changes (`booster/src/CMakeLists.txt:119-145`). Editing `fsdata_srv.c` directly is pointless — the next build overwrites it.

**Pages (`fs/*.shtml`):**
- `mngr_home.shtml` — mode picker (Passthrough/USB/BT). Visibility of cards depends on the `CTARGET` SSI value (computer target bitmask: Croissant=5 Native+BT, Souffle=6 USB+BT).
- `mngr_native.shtml`, `mngr_usb.shtml`, `mngr_bt.shtml` — per-mode config.
- `mngr_btpair.shtml` — BT pairing flow (calls `/btstart.cgi`, polls `/btlist.cgi`, persists via `/saveparams.cgi`).
- `mngr_wifi.shtml` — Wi-Fi advanced settings (AP/STA mode switch, country, etc.).
- `response.shtml` — generic redirect-after-action page; reads `RSPSTS` + `RSPMSG`.
- `json.shtml` / `jsonempty.shtml` — bare JSON wrappers around the `JSONPLD` SSI tag.
- `error.shtml`, `404.html` — error pages.

**SSI tags (max 8 chars each; defined in `ssi_tags[]` and resolved in `ssi_handler`):**
`HOMEPAGE`, `SSID`, `IPADDR`, `JSONPLD` (chunked, multi-part), `TITLEHDR`, `RSPSTS`, `RSPMSG`, `MODE`, `JUSB`, `JPORT`, `MORIG`, `MSPEED`, `KBLANG`, `BTKBL`, `CTARGET`, `BTGSHT`, `JASHT`, `WFIMODE`, `WFIHOST`, `WFISSID`, `WFIPASS`, `WFIAUTH`, `WDFHOST`, `WDFPASS`, `WDFAUTH`, `RTHLPMSG`. Adding a tag: append to `ssi_tags[]` *and* add a `case` to `ssi_handler` with the matching index. Tag length must stay ≤ `LWIP_HTTPD_MAX_TAG_NAME_LEN` (LWIP asserts this at init).

`JSONPLD` is special — it uses lwIP's `LWIP_HTTPD_SSI_MULTIPART` chunking (128 bytes per part) to stream the entire `httpd_json_payload` (1024 bytes) without overflowing the lwIP insert buffer.

**CGI endpoints (`cgi_handlers[]`):**
| URI | Function | Notes |
| --- | --- | --- |
| `/test.cgi` | `cgi_test` | Stub. |
| `/saveparams.cgi` | `cgi_saveparams` | Accepts `?json=<url-encoded base64 JSON>` of `[{name,type,value},…]`. Writes each entry to settings via `settings_put_*` then `settings_save`. `USB_KB_LAYOUT` is force-lowercased. Special-cases `MODE` to clamp to 1 or 2 explicitly. |
| `/btlist.cgi` | `cgi_btlist` | Snapshot of discovered BT devices, returned as JSON. |
| `/btstart.cgi` | `cgi_btstart` | `btloop_enable()` — first call also calls `uni_init()` (Bluepad32 init). |
| `/btstop.cgi` | `cgi_btstop` | `btloop_disable()` (does not unload the stack). |
| `/btpairings.cgi` | `cgi_btpairings` | Current persisted pairings for `PARAM_BT_KEYBOARD` / `PARAM_BT_MOUSE` / `PARAM_BT_GAMEPAD`. Value format: `XX:XX:XX:XX:XX:XX#Device Name`. |
| `/btclean.cgi` | `cgi_btclean` | Wipes all pairings: BTstack key DB + LE bonds + the three settings keys. |
| `/btunpair.cgi` | `cgi_btunpair` | `?type=keyboard|mouse|gamepad`. Drops link key for that one device, clears the setting. |

POST is accepted but the body is discarded — config flows through GET query strings (base64+url-encoded JSON in `?json=`). The `httpd_post_*` stubs exist only to satisfy the linker when `LWIP_HTTPD_SUPPORT_POST=1`.

### Bluetooth (`btloop.c`)

Thin platform-callbacks wrapper around **Bluepad32** + **BTstack**. The `uni_platform` struct in `btloop_platform()` wires Bluepad32's lifecycle hooks (`init`, `on_init_complete`, `on_device_discovered`, `on_device_connected`, `on_device_disconnected`, `on_device_ready`, `on_oob_event`) to local handlers.

- Discovery filter: HID peripherals only (`UNI_BT_COD_MAJOR_PERIPHERAL` + keyboard/mouse/gamepad/joystick minors). Others get `UNI_ERROR_IGNORE_DEVICE`.
- Device cache: 16-slot `bt_devices[]` array, addressed by MAC string. Powers `/btlist.cgi`.
- On pairing (`on_device_ready`): classifies the device, calls `btloop_persist_pairing()` which writes `<MAC>#<name>` to the appropriate settings key and saves.
- BTstack TLV (link keys, bonds) lives in its own flash bank — see "Flash layout" below.
- `btloop_clear_pairings()` wipes both the TLV (`uni_bt_del_keys_unsafe`, `uni_bt_le_delete_bonded_keys`) and the three settings strings.
- `btloop_poll()` runs `async_context_poll(cyw43_arch_async_context())` — this is what services BTstack work.

`uni_init(0, NULL)` is only called once (lazily on first `btloop_enable`). Subsequent enable/disable cycles just flip `btloop_active`.

### Settings store (`settings/settings.c` + `gconfig.c`)

Custom in-RAM cache of a `SettingsConfigEntry[]` array (key/type/value triples), persisted as a single magic-versioned blob in flash.

- Flash region: `_global_config_flash_start`, 4 KB (one sector). Mapped via the linker.
- Magic: 32-bit, formed as `(magic << 16) | version` = `0x12340001`. A `MAGICVERSION` entry is written *first* in the blob; `settingsLoadAllEntries` reads that, and if it doesn't match, the entire blob is treated as missing → defaults are loaded.
- Save path: `flash_range_erase` of the whole sector, then `flash_range_program` of the cached blob. Interrupts disabled during the write (`save_and_disable_interrupts`). Always pass `disable_interrupts=true` from this codebase.
- Defaults table: `defaultEntries[]` in `gconfig.c`. All keys defined as `#define PARAM_*` in `gconfig.h`.
- Entry size: `SETTINGS_MAX_KEY_LENGTH=30` + 4 bytes type + `SETTINGS_MAX_VALUE_LENGTH=96`. Mind the 96-byte value cap when storing strings.
- The `gconfig` context is a singleton (`gSettingsCtx`). All modules call `gconfig_getContext()`.

The settings module also supports erase/deinit/reinit (used in the STA fallback in `main.c` recovery path). Note: there are *more* flash regions defined in `constants.h` than this code currently uses (`_storage_flash_start`, `_global_lookup_flash_start`); the lookup table is a sidecar feature managed by the boot selector, not by Booster.

### Flash layout (RP2350, `memmap_booster_rp2350.ld`)

```
0x10000000 .. 0x10120000   (1.125 MB) → core IKBD firmware (rp2-atarist-rpikb)
0x10120000 .. 0x101E0000   (768 KB)   → BOOSTER code (FLASH)
0x101DE000 .. 0x101FC000   (120 KB)   → CONFIG_FLASH  (per-app config bank)
0x101FC000 .. 0x101FD000   (4 KB)     → GLOBAL_LOOKUP (app UUID → config sector)
0x101FD000 .. 0x101FE000   (4 KB)     → GLOBAL_CONFIG (this is what gconfig uses)
0x101FE000 .. 0x10200000   (8 KB)     → BT_TLV        (BTstack link keys/bonds)
```

RAM: 512 KB at `0x20000000`, scratch X/Y banks for stacks. **Note the LD file says core IKBD goes from 0x10000000–0x1011FFFF** — moving the BOOSTER `FLASH` origin requires updating `merge_uf2.py`'s `FLASH_END_ADDR` and the corresponding LD in the core firmware. The RP2040 `memmap_booster.ld` differs in origins; check it before doing pico_w-only changes.

`main.c` asserts the relationship between `PICO_FLASH_BANK_STORAGE_OFFSET` / `PICO_FLASH_BANK_TOTAL_SIZE` and the BT_TLV region at boot.

### Board flavors (`constants.h`)

| | Croissant (BOARD_TARGET=1) | Souffle (BOARD_TARGET=2) |
| --- | --- | --- |
| `KBD_RESET_IN_3V3_GPIO` | 3 | (not defined — Souffle doesn't drive ATARI signals) |
| `KBD_BD0SEL_3V3_GPIO` | 6 | (not defined) |
| `KBD_ATARI_OUT_3V3_GPIO` | 7 | **8** |
| `KBD_USB_OUT_3V3_GPIO` | 8 | **7** |
| `COMPUTER_TARGET` mask | 5 (Native + BT) | 6 (USB + BT) |
| AP SSID prefix | `croissant` | `souffle` |
| Return-to-settings UX | Hold RESET 10 s while powering on | Press `CONFIG` button |

`COMPUTER_TARGET` is derived in CMake (not in C). It's exposed to the web UI via the `CTARGET` SSI tag and used by JS in `mngr_home.shtml` to hide the modes the board can't run.

`BOARD_TARGET=0` is "unknown" — defined but rejected by the compile-time `#error` in `constants.h`, so it never builds. Don't rely on it.

## Build pipeline (end to end)

1. `./build.sh` (root) — git submodule sync, deletes `build/`, calls `rp2-atarist-rpikb/build.sh` first if `release_type` is set, then `booster/build.sh`.
2. `booster/build.sh` — pins SDK versions (`pico-sdk` tag `2.2.0`, `pico-extras` tag `sdk-2.2.0`, `bluepad32` branch `synthetic-HID-descriptor`), exports env (`PICO_SDK_PATH`, `PICO_EXTRAS_PATH`, `BLUEPAD32_ROOT`, `DEBUG_MODE`, `DISPLAY_ATARIST=1`, `PICO_FLASH_ASSUME_CORE0_SAFE=1`, `BOOSTER_DOWNLOAD_HTTPS=0`, `RELEASE_VERSION`, `RELEASE_DATE`, `BOARD_TARGET`), then `cmake --preset <flavor>-<kind>` and `cmake --build --preset <same>`.
3. CMake configures: resolves `PICO_BOARD`/`PICO_PLATFORM` from `BOARD_TYPE` env, runs `pico_sdk_init`, generates `fsdata_srv.c` via Perl, adds the `booster` executable, links `pico_lwip_http`, `pico_lwip_mdns`, `pico_mbedtls`, `pico_cyw43_arch_lwip_threadsafe_background`, `pico_btstack_cyw43`, `pico_btstack_classic`, custom `settings` lib, `bluepad32`. Sets `pico_set_binary_type(booster booster)` — this writes the boot-selector-readable tag identifying the image. Links with the platform-specific memmap LD.
4. Root `build.sh` post-step (only with `release_type`): copies booster UF2 to `build/`, runs `merge_uf2.py <core> <booster> dist/...`. Writes `ikbd-booster-<version>[-debug]-full.uf2`.

`build_uf2.py` is for combining placeholder+booster (currently commented out in root `build.sh`). `merge_uf2.py` is the active tool — it concatenates two UF2 streams, zero-fills the gap between them, and renumbers `blockNum`/`numBlocks` across the whole resulting image so `picotool` accepts it. `FLASH_END_ADDR` in that script is `0x10200000` (RP2040-style, 2 MB) — this clamps the merged image even though the RP2350 has more flash available.

## CI / Release

- `.github/workflows/build.yml`: PR + manual trigger. Matrix is {release,debug} × {croissant,souffle}. Always builds `final` (i.e. full merged image). Artifacts staged but not uploaded.
- `.github/workflows/release.yml`: triggered on `v*` tags. Validates tag matches `version.txt` exactly. Same matrix. Publishes to GitHub Releases (auto-generated notes) **and** uploads UF2s to S3 bucket `tosemulator.sidecartridge.com` via the AWS credentials in repo secrets.
- `make tag` (from root Makefile) creates and pushes the tag for whatever `version.txt` says.

## Compile-time flags worth knowing

- `_DEBUG` — set from `DEBUG_MODE` env, controls `DPRINTF` (via `debug.h` / `settings.h`), enables UART stdio in release-strip, raises Bluepad32 log verbosity.
- `BOOSTER_DOWNLOAD_HTTPS=0` — currently hardcoded in `booster/build.sh`. If flipped, lwIP altcp/mbedTLS get pulled in (`lwipopts.h:149-159`). No active HTTPS code path.
- `PICO_FLASH_ASSUME_CORE0_SAFE=1` — required because flash writes happen on core 0 while the loop runs on core 0. Single-core invariant.
- `DISPLAY_ATARIST=1` — exported but its consumers are inside `rp2-atarist-rpikb` / Bluepad32, not in Booster itself.
- `HCI_CONTROLLER_CHIPSET_PICOW` — declares the CYW43 BT controller chipset for BTstack.
- `BOARD_TARGET={1,2}` — flows from env into both CMake (via `target_compile_definitions`) and into `constants.h` to pick pinouts and the AP-codename.
- `COMPUTER_TARGET` — derived from `BOARD_TARGET` in CMake. Don't set manually.
- `ENABLE_BLE` / `ENABLE_CLASSIC` — defined by btstack_config.h. Both must be enabled.

## Things that aren't obvious from any single file

- **Three firmware images, one UF2.** Booster + core IKBD (and historically a `placeholder/`, currently commented out of the build) live at different flash origins and are merged. The boot selector chooses between them based on `PARAM_BOOT_FEATURE` in `GLOBAL_CONFIG_FLASH` (sector at `0x101FD000`).
- **`PARAM_BOOT_FEATURE=IKBD` is always written by Booster before its loop starts.** This is intentional: every Booster session is followed by an IKBD session on the next reset. To stay in Booster, the user re-enters Booster mode through the board button (handled by the boot selector in `rp2-atarist-rpikb`).
- **The settings flash region is shared between Booster and IKBD.** Both write to the same `GLOBAL_CONFIG_FLASH` sector. Coordination is by convention — only one of them runs at a time, so there's no concurrency, but if you change the schema in Booster you must update IKBD to read the new format.
- **The `fs/` directory is the web UI source of truth.** Edit there, never touch `fsdata_srv.c`.
- **`PICO_PLATFORM` is resolved from `BOARD_TYPE` (env), not the other way around.** `pico_w` → `rp2040`, `pico2_w` → `rp2350`. Any other value fails configure with a clear error.
- **`pico_set_binary_type(booster booster)` is load-bearing.** It writes a binary-info tag the boot selector reads to confirm "this is Booster, not the placeholder". Don't change the second argument.
- **Bluepad32 is on a non-tagged branch (`synthetic-HID-descriptor`).** Pinned by `booster/build.sh`. There is local divergence from `4.2.0`. Do not retag.
- **The custom `makefsdata` is a forked Perl script** (`booster/src/external/makefsdata`). Vanilla lwIP's version doesn't emit `text/css` headers. If CSS stops being served correctly, suspect a regression there.
- **Wi-Fi STA fallback is destructive.** If STA fails 3× the device rewrites `PARAM_HOSTNAME`, `PARAM_WIFI_PASSWORD`, `PARAM_WIFI_AUTH` back to AP defaults. The user's saved SSID/password is lost across that fallback.
- **`PARAM_WIFI_SSID` can hold a BSSID.** `xx:xx:xx:xx:xx:xx` is detected and triggers a scan-based SSID resolution before connect.
- **`flash_binary_start`, `_storage_flash_start`, `_config_flash_start`, `_global_lookup_flash_start`, `_global_config_flash_start`** are extern symbols supplied by the linker. Changing the LD requires updating `gconfig.c`'s flash-offset calculation (`(unsigned int)&_global_config_flash_start - XIP_BASE`).
- **Releases push to S3 (`tosemulator.sidecartridge.com`)** in addition to GitHub Releases. Tag → both targets get the UF2.
- **Build works around a picotool `find_binary_start` bug.** Stock upstream `picotool` ≥ 2.0.0 misclassifies the booster ELF as RP2040 because `find_binary_start` requires a PT_LOAD that contains `FLASH_START` (`0x10000000`), and the booster's first flash segment is at `0x10120000` (the IKBD core firmware occupies the start of flash). Without intervention, picotool then rejects the RP2350 `.stack_dummy` PT_LOAD at `0x20081000`. The workaround lives in `booster/src/CMakeLists.txt` — a `PICOTOOL_EXTRA_UF2_ARGS` property that passes `--platform <rp2040|rp2350>` to picotool, which sets the model directly and bypasses the broken auto-detection. No patched picotool needed. See `docs/known-issues/picotool-find-binary-start.md`.

---

## Working style

These behavioral guidelines bias toward caution over speed. For trivial tasks, use judgment.

### 1. Think before coding

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them — don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

### 2. Simplicity first

Minimum code that solves the problem. Nothing speculative.
- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

### 3. Surgical changes

Touch only what you must. Clean up only your own mess.
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it — don't delete it.
- When your changes orphan an import/variable/function, remove it. Don't remove pre-existing dead code unless asked.

The test: every changed line should trace directly to the user's request.

### 4. Goal-driven execution

Define success criteria. Loop until verified.
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan with a verification check per step.

### 5. No AI attribution

Never add AI-tool attribution to commits, PR descriptions, code comments,
docs, or any other artifact. This means **no**:
- "Generated with Claude Code", "Co-authored by Claude", "Made with ChatGPT",
  or any similar phrasing.
- `Co-Authored-By: Claude …`, `Co-Authored-By: ChatGPT …`, or any other
  AI co-author trailer.
- "AI-assisted", "written with the help of an LLM", etc., as comments or
  changelog entries.

Write the message as the human author. Do not mention AI tools used to
produce the work.
