# Changelog

## v1.3.0 (2026-11-26) - release

Drag-and-drop installs finish on their own, USB keyboards can click the
mouse, settings survive firmware updates reliably, and Souffle boards get a
factory test. This release changes both the configuration app and the IKBD
keyboard firmware.

### What's new

- **Mouse clicks from a USB keyboard.** As with Bluetooth keyboards,
  ALT+Backspace now acts as ALT+Insert (the TOS left mouse click) and
  ALT+Delete as ALT+CLR/HOME (the right click), so compact and Mac-style USB
  keyboards without Insert or CLR/HOME can click without a mouse.
- **New releases appear in the web installer.** Each release now proposes
  its firmware, release and debug, for Croissant and Souffle, to the
  installer at webflash.sidecartridge.com.
- **Souffle factory test.** A factory image (built on the bench, never
  published) starts the board in USB mode with a test that checks the USB
  keyboard (the keys a to e) and all twelve joystick lines, stores the result
  and hands over to the configuration app. A hidden page,
  `http://souffle.local/factory.shtml`, shows the result. Normal boards are
  unaffected: the test only runs when the factory image turns it on.

### What's fixed

- **Drag-and-drop installs restart the board by themselves.** Copying a
  full `.uf2` image onto the board's USB drive used to leave it waiting with
  the drive still mounted, so you had to unplug it. The image was a few bytes
  short of its last flash page, which the boot ROM ignores; it now fills its
  2 MB exactly, and the board restarts into the new firmware as soon as the
  copy completes.
- **Settings load reliably across firmware updates.** The settings were
  saved with leftover memory after the last entry, and loading could read
  past it. They are now saved with zeros after the last entry and loaded up
  to that end marker, in both firmwares, so a firmware that adds settings (as
  this one does) loads an older settings sector reliably, and a setting it
  does not know no longer resets one it does.
- **Saving settings from the web UI no longer overruns the stack.** Two
  4 KB decode buffers moved off the processor stack.

### Under the hood

- Every build carries its git commit as a build ID, and debug builds print
  it in their first console line; both firmwares' debug consoles run at
  921,600 baud.
- Developer tools in `tools/dev/` build, flash and check a board through a
  Raspberry Pi Debug Probe, with repeatable hardware harnesses.
- Host unit tests (`make -C tests/host test`) cover the settings store, the
  web request decoding and the Wi-Fi parameter parsing under ASan and UBSan,
  and run in CI.
- The core firmware is built from the commit this repository pins, and a
  release is refused unless that commit carries the core's tag of the same
  version.
- `factory_launch.py` makes the Souffle factory image from a published
  release and installs it on a board in BOOTSEL.
- The GPL-3.0 `LICENSE` file the README refers to is now in the repository.

## v1.2.0 (2026-06-15) - release

A power-and-thermal pass on the keyboard firmware: the adapter does exactly
the same job at a much lower clock and voltage, so it draws less current
from the Atari and runs cooler. Everything you do with it — keyboard,
mouse, joystick, USB and Bluetooth — behaves exactly as before. All of the
changes in this release are in the IKBD keyboard firmware; the
configuration app is unchanged.

### What's changed

- **Lower power, runs cooler.** The keyboard firmware now runs at
  96 MHz / 1.00 V instead of 225 MHz / 1.20 V, and the keyboard emulation
  core now idles in a low-power wait between its pacing windows instead of
  busy-spinning the processor. The net effect is noticeably less current
  drawn from the Atari's keyboard connector and less heat, with no change
  to how anything behaves. The clock floor is set by the Wi-Fi / Bluetooth
  radio, which stops responding below ~96 MHz; the keyboard emulation, the
  Atari serial link and USB are all comfortable well below that.
- **The mode LED is dimmer (Souffle).** The USB / Bluetooth indicator LED
  is now dimmed with PWM instead of being driven full-on. It's still
  clearly lit — but at the new low power level a continuously-on LED was a
  surprisingly large slice of the total draw.

### What's fixed

- **USB hubs come up more reliably from a cold start.** Some USB hubs
  occasionally failed to initialize when the adapter was powered on cold.
  The firmware now gives the hub a moment to stabilize before starting the
  USB host, so it enumerates reliably.

## v1.1.0 (2026-05-14) - release

This release focuses on three things you can actually feel when using the
device: it pulls less power from the Atari Mega ST / Mega STE keyboard
connector, Bluetooth pairing behaves more reliably, and the configuration
web UI is faster and looks the same on every page.

### What's new

- **Factory reset page.** Type `/factoryreset.shtml` into your browser
  (there's no link to it from anywhere — you have to know the address) to
  get a "Factory reset" screen. Confirm the warning and the device wipes
  every saved setting, forgets every paired Bluetooth keyboard / mouse /
  gamepad, and reboots itself straight back into configuration mode so you
  can start from scratch. Useful when something gets stuck or you want to
  hand the adapter to someone else.

### What's changed

- **Lower power draw.** The processor now runs at a lower clock and
  voltage when in configuration mode, and the Wi-Fi radio is allowed to
  sleep between activity instead of being kept fully awake. The transmit
  power is also reduced to a level that's plenty for using the device
  next to your computer. The net effect: less load on the Atari's
  keyboard power line, less heat, and noticeably less impact on a tired
  vintage power supply.
- **Smaller, faster web UI.** The configuration pages have been cleaned
  up, deduplicated, and minified at build time. Pages load faster and
  the firmware image is about 80 KB smaller. Each page now has its own
  browser-tab title (Passthrough, USB, Bluetooth, BT Pairing, Wi-Fi)
  so it's clear which screen you're on. The active mode card on the home
  page is announced to screen readers. The Bluetooth pairings list
  wraps neatly on narrow phone screens instead of running off the edge.
- **Two pages removed** that you couldn't reach anyway — they used to
  link out to internet resources that don't work on a device with no
  internet connection.

### What's fixed

- **Bluetooth pairing no longer slows down the web UI after you stop.**
  Previously, even after you clicked "Stop pairing" the Bluetooth radio
  kept scanning in the background, which made the configuration pages
  feel sluggish until you rebooted. Now stopping pairing actually shuts
  the Bluetooth side down so Wi-Fi gets the radio back, and the web UI
  goes back to its normal snappy state. Starting another pairing session
  later works correctly without a reboot.
- **No more boot loop when entering configuration mode.** The first
  attempt at the low-power profile in this release crashed the chip
  during the transition from the running keyboard firmware. The order
  of operations is fixed and the chosen low-power settings are validated
  on real hardware.
- **Firmware fits properly in its allocated flash region.** A previously
  silent overlap between the booster code area and the saved-settings
  area is gone, so heavy debug builds no longer risk silently corrupting
  the saved settings sector.
- **Builds with current versions of `picotool`.** Recent upstream
  picotool releases misidentified the Booster image and refused to write
  it to the device; the build now passes the correct chip family
  explicitly so the standard `picotool` works again.

## v1.0.0 (2026-01-21) - release

First official release of the 1.0.x series. The code has been tested and is considered stable.

### Changes

### New features

### Fixes
