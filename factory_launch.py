#!/usr/bin/env python3
"""
factory_launch — flash a Souffle in BOOTSEL with the factory image of a
published release.

    python3 factory_launch.py [VERSION] [--dir DIR] [--no-flash] [--timeout S]

VERSION (default: version.txt) is a published release, like v1.3.0. In DIR
(default: dist/factory, gitignored):

1. ikbd-booster-VERSION-souffle-full.uf2, the release image users download:
   used if it is there, else downloaded from tosemulator.sidecartridge.com.
   Either way it must be a whole UF2 (every block in sequence, the image
   from the start of flash) holding Souffle's Booster of VERSION.
2. ikbd-booster-VERSION-souffle-factory.uf2: used if it is there, else made
   from the release image by build_factory_uf2.py, with the bench Wi-Fi from
   factory.env or tools/dev/bench.env.
3. Unless --no-flash: waits (up to --timeout seconds, default 300) for one
   RP2350 in BOOTSEL, copies the factory image onto its drive as a user
   would, and waits for the board to leave BOOTSEL. The board then runs the
   factory test (see build_factory_uf2.py).

Souffle only: the factory test needs the USB keyboard and the joystick lines,
which Croissant does not have. Run it by hand on the bench, never in CI: the
factory image carries the bench Wi-Fi password.

Exit codes: 0 flashed (or, with --no-flash, both images ready), 1 error,
2 usage.
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import urllib.request

import build_factory_uf2 as factory

HERE = os.path.dirname(os.path.abspath(__file__))
DOWNLOAD = "https://tosemulator.sidecartridge.com/{name}"
FLASH_START = 0x10000000
# Where an RP2350 in BOOTSEL mounts its drive (macOS, then Linux).
DRIVES = ("/Volumes/*/INFO_UF2.TXT", "/media/*/*/INFO_UF2.TXT", "/run/media/*/*/INFO_UF2.TXT")


def check_release(path: str, version: str) -> None:
    """A whole Souffle release UF2 of this version, or FactoryError."""
    blocks = factory.read_uf2(path)
    for number, (_, _, block) in enumerate(blocks):
        block_no, num_blocks = struct.unpack_from("<2I", block, 20)
        if num_blocks != len(blocks):
            raise factory.FactoryError(f"{path}: has {len(blocks)} blocks of the {num_blocks} "
                                       f"its blocks announce: an incomplete UF2")
        if block_no != number:
            raise factory.FactoryError(f"{path}: block {number} is numbered {block_no}: "
                                       f"a damaged UF2")
        address, size = struct.unpack_from("<2I", block, 12)
        if size != factory.UF2_PAYLOAD or address % factory.UF2_PAYLOAD:
            # The bootrom ignores such a block, never counts the download as
            # complete, and does not reboot after it.
            raise factory.FactoryError(f"{path}: block {number} is {size} bytes at "
                                       f"0x{address:08x}, not a whole 256-byte page: the "
                                       f"board would not reboot after the copy")
    if blocks[0][0] != FLASH_START:
        raise factory.FactoryError(f"{path}: does not start at 0x{FLASH_START:08x}: not a full image")
    booster = factory.region(blocks, *factory.BOOSTER)
    if b"\0souffle\0" not in booster:
        raise factory.FactoryError(f"{path}: not a Souffle image")
    found = factory.VERSION_RE.search(booster)
    if not found or found.group(1).decode() != version:
        raise factory.FactoryError(f"{path}: holds Booster "
                                   f"{found.group(1).decode() if found else 'of no version'}, "
                                   f"not {version}")


def release_image(folder: str, version: str) -> str:
    name = f"ikbd-booster-{version}-souffle-full.uf2"
    path = os.path.join(folder, name)
    if os.path.exists(path):
        print(f"release image: {path} (already here)", flush=True)
    else:
        url = DOWNLOAD.format(name=name)
        print(f"release image: downloading {url}", flush=True)
        partial = path + ".part"
        request = urllib.request.Request(url, headers={"User-Agent": "curl/8"})
        try:
            with urllib.request.urlopen(request, timeout=60) as response, \
                    open(partial, "wb") as out:
                shutil.copyfileobj(response, out)
        except OSError as exc:
            if os.path.exists(partial):
                os.remove(partial)
            raise factory.FactoryError(f"{url}: {exc} (is {version} published?)") from exc
        try:
            check_release(partial, version)
        except factory.FactoryError:
            os.remove(partial)
            raise
        os.replace(partial, path)
        return path
    check_release(path, version)
    return path


def factory_image(folder: str, version: str, release: str) -> str:
    path = os.path.join(folder, f"ikbd-booster-{version}-souffle-factory.uf2")
    if os.path.exists(path):
        print(f"factory image: {path} (already here)", flush=True)
        return path
    run = subprocess.run([sys.executable, os.path.join(HERE, "build_factory_uf2.py"),
                          release, "--out", path])
    if run.returncode != 0:
        raise factory.FactoryError("build_factory_uf2.py failed")
    return path


def bootsel_drives() -> list[str]:
    drives = []
    for info in (p for pattern in DRIVES for p in glob.glob(pattern)):
        try:
            with open(info, encoding="ascii", errors="replace") as f:
                if re.search(r"Board-ID:\s*RP2350", f.read()):
                    drives.append(os.path.dirname(info))
        except OSError:
            pass
    return drives


def flash(image: str, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    print("waiting for a Souffle in BOOTSEL (hold BOOTSEL while plugging it in)...", flush=True)
    while not (drives := bootsel_drives()):
        if time.monotonic() > deadline:
            raise factory.FactoryError(f"no RP2350 in BOOTSEL after {timeout:.0f} s")
        time.sleep(1)
    if len(drives) > 1:
        raise factory.FactoryError(f"{len(drives)} RP2350 drives ({', '.join(drives)}): "
                                   "leave one board in BOOTSEL")
    drive = drives[0]
    print(f"copying {os.path.basename(image)} to {drive}", flush=True)
    try:
        shutil.copyfile(image, os.path.join(drive, os.path.basename(image)))
    except OSError:
        # The board reboots as soon as it has every block, and the drive can
        # vanish before the copy returns; only a drive still there is an error.
        if os.path.exists(os.path.join(drive, "INFO_UF2.TXT")):
            raise
    for _ in range(30):
        if not os.path.exists(os.path.join(drive, "INFO_UF2.TXT")):
            print("the board left BOOTSEL: it now runs the factory test", flush=True)
            return
        time.sleep(1)
    raise factory.FactoryError(f"{drive} is still mounted 30 s after the copy: "
                               "the board did not take the image")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1].strip())
    ap.add_argument("version", nargs="?")
    ap.add_argument("--dir", default=os.path.join(HERE, "dist", "factory"))
    ap.add_argument("--no-flash", action="store_true", help="only make the two images")
    ap.add_argument("--timeout", type=float, default=300, help="seconds to wait for BOOTSEL")
    args = ap.parse_args()
    version = args.version
    if not version:
        with open(os.path.join(HERE, "version.txt"), encoding="ascii") as f:
            version = f.read().strip()
    if not re.fullmatch(r"v\d+\.\d+\.\d+", version):
        print(f"error: {version}: not a version like v1.3.0", file=sys.stderr)
        return 2
    try:
        os.makedirs(args.dir, exist_ok=True)
        release = release_image(args.dir, version)
        image = factory_image(args.dir, version, release)
        if not args.no_flash:
            flash(image, args.timeout)
    except (factory.FactoryError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
