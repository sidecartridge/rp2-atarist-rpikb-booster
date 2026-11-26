#!/usr/bin/env python3
"""
build_factory_uf2 — the Souffle factory image: a release UF2 with its settings
sector pre-filled for the factory test.

    python3 build_factory_uf2.py RELEASE_UF2 [--out FILE] [--skip-key-check]

RELEASE_UF2 is a full Souffle release image (from `./build.sh pico2_w release
final souffle`, or the release download). The output is that image byte for
byte, except the settings sector (GLOBAL_CONFIG, 0x101FD000), which the
release image zero-fills and the factory image fills with:

    BOOT_FEATURE=IKBD, MODE=1 (USB), FACTORY_MODE=1,
    WIFI_MODE=1 (STA) and the bench Wi-Fi: SSID, password, auth, country

Every other setting takes its default. The core firmware then boots into USB
mode with the factory test on; the bench Wi-Fi is used when it hands over to
Booster. The factory reset erases the sector, so the board becomes a clean
Souffle.

The bench Wi-Fi comes from FACTORY_WIFI_SSID, FACTORY_WIFI_PASSWORD,
FACTORY_WIFI_AUTH (default 5, WPA2-AES) and FACTORY_WIFI_COUNTRY (default XX)
in the environment, else from factory.env beside this script (KEY=VALUE
lines), else from tools/dev/bench.env (BENCH_WIFI_*). These files are
gitignored; the password is never printed. Build this image locally, by hand:
never in CI, since it carries the password.

Refuses a Croissant image, a debug build, and a tree where Booster's and the
core firmware's settings defaults do not have the same keys: a key one
firmware lacks is dropped when that firmware saves, which would lose the
factory test's results. --skip-key-check builds anyway, for bench tests of the
Booster side before the core firmware has the factory keys.

Exit codes: 0 written, 1 error, 2 usage.
"""

from __future__ import annotations

import argparse
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

UF2_MAGIC = (0x0A324655, 0x9E5D5157)
UF2_BLOCK, UF2_PAYLOAD = 512, 256
BOOSTER = (0x10120000, 0x101DE000)       # memmap_booster_rp2350.ld FLASH
GLOBAL_CONFIG = 0x101FD000               # the settings sector, 4 KB
SECTOR = 4096

# The settings format both firmwares share (booster/src/settings): 127-byte
# entries, back to back: 30-byte key, 1-byte type, 96-byte value, NUL-padded;
# MAGICVERSION first. The bytes after the last entry are left erased.
KEY_LEN, VALUE_LEN = 30, 96
INT, STRING = 0, 1
MAGIC = (0x1234 << 16) | 0x0001          # CONFIG_MAGIC_NUMBER, CONFIG_VERSION_NUMBER

BUILD_ID_RE = re.compile(
    rb"\0([0-9a-f]{7}(?:-dirty\.[0-9a-f]{7})?(?:\+debug)?|nogit(?:\+debug)?)\0")
VERSION_RE = re.compile(rb"\0(v\d+\.\d+\.\d+[0-9A-Za-z.\-]*)\0")


class FactoryError(Exception):
    pass


def encode_entry(key: str, kind: int, value: str) -> bytes:
    raw = value.encode("latin-1")
    if len(raw) >= VALUE_LEN:
        raise FactoryError(f"{key}: at most {VALUE_LEN - 1} bytes")
    return key.encode().ljust(KEY_LEN, b"\0") + bytes([kind]) + raw.ljust(VALUE_LEN, b"\0")


def encode_sector(entries: list[tuple[str, int, str]]) -> bytes:
    blob = encode_entry("MAGICVERSION", INT, str(MAGIC)) + b"".join(
        encode_entry(*e) for e in entries)
    return blob.ljust(SECTOR, b"\xff")


def read_env_file(path: str, prefix: str) -> dict[str, str]:
    values = {}
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            for line in f:
                key, sep, value = line.strip().partition("=")
                if sep and key.startswith(prefix):
                    values[key[len(prefix):]] = value
    return values


def bench_wifi() -> dict[str, str]:
    """SSID, PASSWORD, AUTH, COUNTRY of the bench Wi-Fi."""
    found = read_env_file(os.path.join(HERE, "tools", "dev", "bench.env"), "BENCH_WIFI_")
    found.update(read_env_file(os.path.join(HERE, "factory.env"), "FACTORY_WIFI_"))
    found.update({k[len("FACTORY_WIFI_"):]: v for k, v in os.environ.items()
                  if k.startswith("FACTORY_WIFI_") and v})
    ssid, password = found.get("SSID", ""), found.get("PASSWORD", "")
    if not ssid or not password:
        raise FactoryError("no bench Wi-Fi: set FACTORY_WIFI_SSID and FACTORY_WIFI_PASSWORD "
                           "(environment, factory.env or tools/dev/bench.env)")
    if not 1 <= len(ssid.encode()) <= 32:
        raise FactoryError("the SSID is 1 to 32 bytes")
    if not 8 <= len(password) <= 63:
        raise FactoryError("a WPA2 password is 8 to 63 characters")
    auth = found.get("AUTH") or "5"
    country = (found.get("COUNTRY") or "XX").upper()
    if not auth.isdigit() or not re.fullmatch(r"[A-Z]{2}", country):
        raise FactoryError("AUTH is a number and COUNTRY two letters")
    return {"SSID": ssid, "PASSWORD": password, "AUTH": auth, "COUNTRY": country}


def factory_entries(wifi: dict[str, str]) -> list[tuple[str, int, str]]:
    return [("BOOT_FEATURE", STRING, "IKBD"), ("MODE", INT, "1"),
            ("FACTORY_MODE", INT, "1"), ("WIFI_MODE", INT, "1"),
            ("WIFI_SSID", STRING, wifi["SSID"]), ("WIFI_PASSWORD", STRING, wifi["PASSWORD"]),
            ("WIFI_AUTH", INT, wifi["AUTH"]), ("WIFI_COUNTRY", STRING, wifi["COUNTRY"])]


def default_keys(src: str) -> set[str]:
    """The keys of a firmware's settings defaults table (src/gconfig.c and the
    PARAM_* names in its gconfig.h)."""
    with open(os.path.join(src, "include", "gconfig.h"), encoding="utf-8") as f:
        names = dict(re.findall(r'#define\s+(PARAM_\w+)\s+"([^"]+)"', f.read()))
    with open(os.path.join(src, "gconfig.c"), encoding="utf-8") as f:
        table = re.search(r"defaultEntries\[\]\s*=\s*\{(.*?)\};", f.read(), re.S).group(1)
    return {names.get(p, p) for p in re.findall(r"\{\s*(PARAM_\w+)\s*,", table)}


def check_keys() -> None:
    booster = default_keys(os.path.join(HERE, "booster", "src"))
    core = default_keys(os.path.join(HERE, "rp2-atarist-rpikb", "src"))
    if booster != core:
        raise FactoryError(
            "Booster's and the core firmware's settings defaults differ: only in Booster "
            f"{sorted(booster - core) or 'none'}, only in the core {sorted(core - booster) or 'none'}")


def read_uf2(path: str) -> list[list]:
    with open(path, "rb") as f:
        data = f.read()
    if not data or len(data) % UF2_BLOCK:
        raise FactoryError(f"{path}: not a UF2 file")
    blocks = []
    for off in range(0, len(data), UF2_BLOCK):
        block = bytearray(data[off:off + UF2_BLOCK])
        m0, m1, _, address, size = struct.unpack_from("<5I", block)
        if (m0, m1) != UF2_MAGIC:
            raise FactoryError(f"{path}: bad UF2 block at offset {off}")
        blocks.append([address, size, block])
    return blocks


def region(blocks: list[list], start: int, end: int) -> bytes:
    image = bytearray(end - start)
    for address, size, block in blocks:
        if start <= address < end:
            image[address - start:address - start + size] = block[32:32 + size]
    return bytes(image)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1].strip())
    ap.add_argument("release_uf2")
    ap.add_argument("--out", help="default: dist/factory/ikbd-booster-<version>-souffle-factory.uf2")
    ap.add_argument("--skip-key-check", action="store_true")
    args = ap.parse_args()
    try:
        blocks = read_uf2(args.release_uf2)
        booster = region(blocks, *BOOSTER)
        if b"\0souffle\0" not in booster or b"\0croissant\0" in booster:
            raise FactoryError("not a Souffle image: the factory test is Souffle only")
        ids = {m.group(1).decode() for m in BUILD_ID_RE.finditer(booster)}
        if any(i.endswith("+debug") for i in ids):
            raise FactoryError("a debug build: use the release image")
        version = VERSION_RE.search(booster)
        version = version.group(1).decode() if version else "unknown"
        if args.skip_key_check:
            print("warning: settings key sets not checked (--skip-key-check): for bench "
                  "tests only", file=sys.stderr)
        else:
            check_keys()
        wifi = bench_wifi()
        sector = encode_sector(factory_entries(wifi))
        patched = 0
        for address, size, block in blocks:
            if GLOBAL_CONFIG <= address < GLOBAL_CONFIG + SECTOR:
                if size != UF2_PAYLOAD:
                    raise FactoryError(f"unexpected UF2 payload size {size} at 0x{address:08x}")
                block[32:32 + size] = sector[address - GLOBAL_CONFIG:address - GLOBAL_CONFIG + size]
                patched += 1
        if patched != SECTOR // UF2_PAYLOAD:
            raise FactoryError(f"the image covers {patched} of the settings sector's "
                               f"{SECTOR // UF2_PAYLOAD} blocks: use a full release image")
        out = args.out or os.path.join(HERE, "dist", "factory",
                                       f"ikbd-booster-{version}-souffle-factory.uf2")
        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        with open(out, "wb") as f:
            for _, _, block in blocks:
                f.write(block)
    except (FactoryError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    print(f"{out}: Souffle {version} (build {', '.join(sorted(ids)) or 'unknown'}), factory "
          f"settings: USB mode, factory test on, bench Wi-Fi {wifi['SSID']} "
          f"(auth {wifi['AUTH']}, country {wifi['COUNTRY']})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
