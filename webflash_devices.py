#!/usr/bin/env python3
"""
webflash_devices — add a release to webflash.sidecartridge.com's devices.json.

    python3 webflash_devices.py VERSION DEVICES_JSON [--datetime ISO8601]

DEVICES_JSON is devices.json from sidecartridge/rp-webusb-picoflash. For each
board (keyboardemulator-croissant-family, keyboardemulator-souffle-family) the
release and the debug firmware of VERSION go at the top of the family's list,
release first, as the file lists every version. Name, description,
device_type and post_install_html are those of the newest entry of the same
kind; id, version, datetime (default: now, UTC) and uf2_url, the image the
release workflow uploads to tosemulator.sidecartridge.com, are the new
version's. A family that already lists VERSION is left as it is, so running
it twice changes nothing. The file keeps its two-space JSON layout.

The release workflow runs this when a v* tag is published, then opens the PR
on the webflash repository.

Exit codes: 0 written or already there, 1 error, 2 usage.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from datetime import datetime, timezone

BOARDS = ("croissant", "souffle")
UF2_URL = "https://tosemulator.sidecartridge.com/ikbd-booster-{version}-{board}{debug}-full.uf2"


def entry(template: dict, board: str, kind: str, version: str, when: str) -> dict:
    new = dict(template)
    new["id"] = f"keyboardemulator-{board}-{kind}-{version}"
    new["version"] = version
    new["datetime"] = when
    new["uf2_url"] = UF2_URL.format(version=version, board=board,
                                    debug="-debug" if kind == "debug" else "")
    return new


def add_release(data: dict, version: str, when: str) -> list[str]:
    added = []
    for board in BOARDS:
        family = next((f for f in data["families"]
                       if f.get("id") == f"keyboardemulator-{board}-family"), None)
        if family is None:
            raise ValueError(f"no keyboardemulator-{board}-family in devices.json")
        firmwares = family["firmwares"]
        if any(f.get("version") == version for f in firmwares):
            continue
        new = []
        for kind in ("release", "debug"):
            template = next((f for f in firmwares
                             if f["id"].startswith(f"keyboardemulator-{board}-{kind}")), None)
            if template is None:
                raise ValueError(f"no {kind} entry to follow in keyboardemulator-{board}-family")
            new.append(entry(template, board, kind, version, when))
        firmwares[:0] = new
        added += [f["id"] for f in new]
    return added


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1].strip())
    ap.add_argument("version")
    ap.add_argument("devices_json")
    ap.add_argument("--datetime", help="the release time, ISO 8601 (default: now, UTC)")
    args = ap.parse_args()
    if not re.fullmatch(r"v\d+\.\d+\.\d+", args.version):
        print(f"error: {args.version}: not a version like v1.3.0", file=sys.stderr)
        return 2
    when = args.datetime or datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    try:
        with open(args.devices_json, encoding="utf-8") as f:
            data = json.load(f)
        added = add_release(data, args.version, when)
        if added:
            with open(args.devices_json, "w", encoding="utf-8") as f:
                f.write(json.dumps(data, indent=2, ensure_ascii=False) + "\n")
    except (OSError, ValueError, KeyError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    print(f"added {', '.join(added)}" if added else f"{args.version} already listed: no change")
    return 0


if __name__ == "__main__":
    sys.exit(main())
