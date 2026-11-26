# Developer tools

Host-side tools for working on Booster with the hardware attached: a Souffle or Croissant board
(Pico 2 W, RP2350) with a Raspberry Pi Debug Probe wired to the Pico's SWD pins and to its debug
UART. They build, flash and check a board, read its state while it runs, drive it remotely, and
run hardware harnesses, so a change is judged on hardware in one command. The Atari need not be
connected for any of them. Ported from sidecartos's `tools/dev`; Python tools use the standard
library only.

## Wiring

| Debug Probe | Souffle / Croissant (Pico 2 W) |
|---|---|
| D (SWD): SC, SD, GND | SWCLK, SWDIO, GND |
| U (UART): RX | GPIO 0 (UART0 TX, the debug console) |
| U (UART): TX | GPIO 1 (UART0 RX) |

The Atari serial link uses UART1 on GPIO 4/5 and is not involved. Power the board from its micro
USB or from the Atari.

## Environment

- OpenOCD with RP2350 support and the `rp2350.cm0`/`rp2350.cm1` target names: `$OPENOCD`,
  `openocd` on `PATH`, or `../pico/openocd/src/openocd` (the Raspberry Pi fork); its scripts from
  `$PICO_OPENOCD_PATH`. (The older `../pico/openocd-rp2350` build that `.vscode/launch.json` uses
  names the targets `rp2350.dap.core0`/`core1` and does not work with `swd.py`.)
- The Arm toolchain on `PATH` (`arm-none-eabi-gcc`, `-nm`, `-objdump`, `-objcopy`, `-addr2line`),
  and `arm-none-eabi-gdb` (or `$ARM_GDB_PATH`) for `postmortem`.
- Python 3.10 or later.
- The network once per checkout: in a fresh clone `flash.sh` first clones the submodules. Every
  later run works from the local history.

## Firmware support these tools rely on

- Every build carries its build ID in flash as the `release_build_id` string
  (`booster/src/build_id.cmake`): `<sha7>`, or `<sha7>-dirty.<diff7>` with uncommitted changes
  under `booster/` (the generated `fsdata_srv.c` does not count), then `+debug` for a debug build.
  The same tree gives the same ID, and the string is in flash even in a stripped ELF.
- Debug builds run the console at 921,600 baud and start with a banner naming the image and its
  build: `main.c:59:main(): SidecarTridge IKBD Booster. v1.3.0 (...). Build e73cbe2+debug. DEBUG
  mode.` (`DPRINTF` puts the source location first). Release builds have no console.
- Builds from `flash.sh` keep their symbols (`BOOSTER_KEEP_SYMBOLS=ON`); the shipped release ELF is
  stripped. The `.uf2` is the same either way.

## Rules

Every tool follows these; follow them too when driving the probe by hand.

- **Reset through the watchdog**, never with OpenOCD's `reset`: PSM `WDSEL` = `0x01FFFFF3`, then
  `WATCHDOG_CTRL.TRIGGER`. It restarts both cores together and leaves PIO and DMA stopped, as a
  power-on does.
- **No OpenOCD work area** except while writing flash: its default, 64 KB at `0x20010000`, is in
  the running firmware's RAM. `verify` then compares on the host, and GDB connects with
  `gdb_memory_map disable`.
- **Quiesce before a flash write:** halting the cores does not stop PIO and DMA, which the core
  firmware's USB host and Atari link use. `swd.py program` and `settings set` halt both cores, stop
  every PIO state machine and abort every DMA channel, write, then reset.
- **Never read the flash over SWD while the firmware writes it** (settings, Bluetooth keys): the
  QMI can wedge for good. `swd.py` reads the flash only with core 0 halted outside a flash
  operation, and resumes it.
- **One owner per probe and per UART.** Close a VS Code debug session before running a tool, and
  any serial terminal (or another project's `console.py watch`, which grabs the probe's UART when
  it is plugged in) before `console.py watch`.
- **Halting stops Wi-Fi, Bluetooth and the Atari link** until the RP resumes.

## How the board switches between the two firmwares

The flash holds the core firmware at `0x10000000` and Booster at `0x10120000`. The core firmware
always starts first and enters Booster when `MODE` is 255 (configuration) or when the CONFIG button
(GPIO 9) is pressed. A stored `BOOT_FEATURE` other than `IKBD` makes it erase all settings and
reboot, so the tools never write it: `swd.py boot booster` presses CONFIG over SWD (drives GPIO 9
high, then releases it), and `swd.py boot ikbd` resets the chip.

## Debug console: `console.py`

Captures the console of a debug build to `tools/dev/logs/console.log`, a timestamp on every line,
and mirrors it to the terminal. Use it instead of a serial terminal.

```bash
python3 tools/dev/console.py watch          # leave running in a terminal
python3 tools/dev/console.py since-boot     # everything since the last banner
python3 tools/dev/console.py tail 100
python3 tools/dev/console.py grep 'HTTP server' --since-boot
python3 tools/dev/console.py wait 'HTTP server initialized' --timeout 20
```

`watch` finds the Debug Probe by its USB name (`--port` to choose another device), waits for it
when it is unplugged, and reopens it when it returns. A boot starts at either firmware's banner,
so one log follows the board across the jump into Booster. `grep` and `wait` exit with 3 when
nothing matches; `wait` only matches lines that arrive after it starts. The log rotates at 32 MB.

## Build, flash and verify: `flash.sh`

```bash
tools/dev/flash.sh souffle debug               # build Booster, flash, verify
tools/dev/flash.sh souffle release --full      # also the core firmware
tools/dev/flash.sh croissant debug --build-only
```

Builds out of tree, incrementally, in `tools/dev/builds/<flavor>-<type>/{booster,core}`, with the
settings of `booster/build.sh` (release is CMake Release with symbols kept, debug is CMake Debug
with the 921,600 baud console). Everything builds against worktrees in `tools/dev/sdk/` made from
the submodules' own history at the pinned refs (pico-sdk 2.2.0, pico-extras sdk-2.2.0, Bluepad32
`synthetic-HID-descriptor`), so the submodules, `booster/build-*`, `build/` and `dist/` are never
touched. `--full` also builds the core firmware at the commit the `rp2-atarist-rpikb` submodule
pins. The ELFs are kept as `tools/dev/builds/elf/<booster|core>-<flavor>-<type>-<id>.elf`.

Then it flashes through the probe (`swd.py program`: only the ELFs' own flash, so the config
sectors, the settings and the Bluetooth keys are kept), runs Booster (`swd.py boot booster`) and
checks over SWD: Booster runs, the flash matches each ELF byte for byte, and Booster carries the
new build ID. On failure it exits 1 and prints the console since the last boot. picotool is not
used. `RELEASE_DATE` defaults to the HEAD commit's date, so builds of one tree are byte-identical;
`FLASH_BUILDS_DIR` moves the build directories. The product build, `./build.sh`, is separate and
still makes the UF2s in `dist/`.

A first build takes about 2 min 40 s; reruns compile only what changed. Flashing and verifying
take 1.5 to 2.5 minutes in all.

## Debug probe: `swd.py`

Reads memory through the debug port while the CPU keeps running, so it works on release builds as
on debug ones, and on a hung RP. Exit codes: 0 success, 1 error, 2 usage, 3 the check failed.

### Generic

```bash
python3 tools/dev/swd.py board                  # chip, which image runs, its build
python3 tools/dev/swd.py running ELF            # wait until the RP runs this ELF
python3 tools/dev/swd.py verify ELF...          # flash identical to each ELF?
python3 tools/dev/swd.py build-id [ELF ...]     # each image's build ID
python3 tools/dev/swd.py read 0x20000000 1024 out.bin
python3 tools/dev/swd.py program ELF...         # quiesce, flash, reset
python3 tools/dev/swd.py reset                  # watchdog-style reset
python3 tools/dev/swd.py resume                 # release cores a debugger left halted
python3 tools/dev/swd.py crash                  # why did it last reset?
python3 tools/dev/swd.py heap [--watch 5 --csv FILE]
python3 tools/dev/swd.py postmortem [--leave-halted]
```

Both images put their RAM vector table at `0x20000000`, so which one runs is read from core 0's
program counter, sampled 32 times through the DWT: samples in one image's flash and none in the
other's decide. Commands that need symbols use the cached ELF whose build ID the RP carries
(`--elf` overrides); `build-id` without an ELF falls back to scanning the image's flash, so it also
names a product UF2's build. `postmortem` halts the RP, prints both cores' backtraces and
registers through GDB, then resumes it.

### Booster

```bash
python3 tools/dev/swd.py settings [--show-secrets]          # the settings sector
python3 tools/dev/swd.py settings set MODE=1 MOUSE_SPEED=7  # rewrite entries, verify, reset
python3 tools/dev/swd.py boot booster                       # press CONFIG over SWD
python3 tools/dev/swd.py boot ikbd                          # reset; stays in the core unless MODE is 255
python3 tools/dev/swd.py clock                              # 96 MHz / 1.00 V low-power profile?
python3 tools/dev/swd.py net                                # Wi-Fi mode, status, IP, hostname
python3 tools/dev/swd.py bt                                 # Bluetooth state, devices, pairings
python3 tools/dev/swd.py wifi-bench [--off]                 # Booster on the bench Wi-Fi, or back to AP
```

The settings sector (`0x101FD000`, shared by both firmwares) holds 127-byte entries: a 30-byte key,
a 1-byte type and a 96-byte value, `MAGICVERSION` first. `settings set` accepts existing keys only
and checks values against their type; it resets the chip afterwards because the running firmware
keeps its own copy in RAM. `net` and `bt` read Booster's own variables by symbol, so they need a
build from `flash.sh`.

## The bench Wi-Fi

`tools/dev/bench.env` (gitignored, never committed) holds the network the board joins for the web
harnesses:

```
BENCH_WIFI_SSID=<network>
BENCH_WIFI_PASSWORD=<password>
BENCH_WIFI_AUTH=5          # optional: 5 WPA2-AES (default), 6 WPA2 mixed, 0 open; no WPA3
BENCH_WIFI_COUNTRY=ES      # optional: XX (worldwide) by default
```

The same names in the environment win. `swd.py wifi-bench` writes them into the settings in STA
mode, makes Booster run and waits for its address; it stops with an error if Booster gives up and
falls back to its access point. `--off` restores the access point's defaults (Booster also uses
`WIFI_PASSWORD` as the AP's password). A board stuck at `LINK JOIN` in the console associated but
failed the WPA2 handshake: check the password.

## Hardware harnesses

Scripts that run a set of checks on the board and print PASS, FAIL or SKIP for each. They need the
Debug Probe and, for debug builds, `console.py watch` running (they read its log, never the UART).
Each writes a JSON report to `tools/dev/logs/` and exits 0 only when no check failed. Run them as
`python3 tools/dev/<harness>.py` (not `python3 -I`: they import `harnesslib.py` beside them).

```bash
python3 tools/dev/tools_harness.py [--flash souffle]       # every swd.py command
python3 tools/dev/boot_harness.py --boots 20 [--flash souffle release]
python3 tools/dev/web_harness.py                           # web UI over the bench Wi-Fi
python3 tools/dev/bt_harness.py --cycles 3                 # Bluetooth start/stop vs HTTP
```

- `tools_harness.py` checks the tools themselves: build ID, `verify`, a reset with the banner and
  the HTTP server on the console, `running`, `board`, `settings`, `clock`, `net`, `bt`, `heap`,
  `crash`, `postmortem` with both cores running after it, and the round trip through the core
  firmware when `MODE` is not 255.
- `boot_harness.py` boots into Booster N times and checks every boot: the flashed build runs at the
  low-power profile; with a debug build it reports Booster's time from its banner to a running HTTP
  server (about 1.27 s).
- `web_harness.py` puts Booster on the bench Wi-Fi, fetches every page and asset, the 404 page and
  the JSON endpoints, sends `/saveparams.cgi` exactly as the web UI does and reads the stored values
  back from flash, and checks that bad requests are refused without changing anything.
- `bt_harness.py` puts Booster on the bench Wi-Fi, starts and stops Bluetooth from the web UI, and
  checks that HTTP keeps answering while it scans and is back to its baseline latency after every
  stop.

`web_harness.py` and `bt_harness.py` restore the settings as they were before the run, failure or
not; the restore resets the chip.

## Measuring builds

```bash
tools/dev/measure_builds.sh souffle OUT_DIR                  # release and debug
tools/dev/stackdepth.py OUT_DIR/souffle-debug/booster roots main cgi_saveparams ssi_handler
tools/dev/stackdepth.py OUT_DIR/souffle-release/booster top
```

`measure_builds.sh` builds through `flash.sh --build-only` into `OUT_DIR` (a temporary directory by
default) with `-fstack-usage -fcallgraph-info=su`, and prints each ELF's sections, its flash use
against Booster's 760K slot, the web assets' size and its heap room (`__end__` to `__HeapLimit`).
`stackdepth.py` computes worst-case stack depth from those builds: static call edges only (lwIP's
CGI and SSI handlers and the Bluetooth callbacks are called indirectly: name them as roots),
without interrupt stacking or precompiled libraries, so treat its answer as a floor.

`logs/`, `builds/`, `sdk/` and `bench.env` are generated or private here and are gitignored.
