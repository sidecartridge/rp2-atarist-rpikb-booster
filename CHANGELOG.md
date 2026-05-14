# Changelog

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
