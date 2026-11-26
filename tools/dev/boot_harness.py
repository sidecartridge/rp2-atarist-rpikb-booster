#!/usr/bin/env python3
"""Boot the board into Booster N times in a row and check every boot: it runs
the build that was flashed, at the low-power profile, and (debug builds) how
long Booster takes from its banner to a running HTTP server.

Each boot is a watchdog-style reset: the core firmware starts first and hands
over to Booster (MODE 255, or the CONFIG button pressed over SWD), so the
figures include that handover but not the crystal startup a cold start goes
through. Release and debug builds both work; with a debug build `console.py
watch` must run and every boot's banner is checked.

    python3 tools/dev/boot_harness.py --boots 20
    python3 tools/dev/boot_harness.py --boots 20 --flash souffle release

Exits 0 when every boot passes; writes tools/dev/logs/boot-harness-<time>.json.
"""

import argparse
import datetime
import re
import statistics
import sys

from harnesslib import (LogCursor, check, flash, report, running_build, swd,
                        swdpy)

STAMP = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d\.\d{3}) ")


def stamp(line: str | None) -> datetime.datetime | None:
    m = STAMP.match(line or "")
    return datetime.datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S.%f") if m else None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--boots", type=int, default=10)
    ap.add_argument("--flash", nargs=2, metavar=("FLAVOR", "TYPE"),
                    help="flash this board flavor and build type first")
    args = ap.parse_args()
    if args.flash and not flash(*args.flash):
        return report("boot-harness")

    app, kind, build_id = running_build()
    if not check("Booster runs", app == "booster", f"{app} {kind}, build {build_id}"):
        return report("boot-harness")
    elf = swd.running_elf(None)
    debug = kind == "debug"
    http_ms = []
    for boot in range(1, args.boots + 1):
        cursor = LogCursor()
        rc, _ = swdpy("reset")
        rc, _ = swdpy("boot", "booster")
        ok = rc == 0
        rc, _ = swdpy("running", elf, "--timeout", "20")
        ok &= rc == 0
        detail = []
        if debug:
            banner = cursor.wait(r"SidecarTridge IKBD Booster\. .*Build "
                                 + re.escape(build_id), 20)
            http = cursor.wait("HTTP server initialized", 20)
            ok &= banner is not None and http is not None
            if banner and http:
                ms = (stamp(http) - stamp(banner)).total_seconds() * 1000
                http_ms.append(ms)
                detail.append(f"banner to HTTP {ms:.0f} ms")
        rc, clock = swdpy("clock")
        ok &= rc == 0
        detail.append("96 MHz / 1.00 V" if rc == 0 else "NOT the low-power profile")
        check(f"boot {boot}: Booster runs, build {build_id}", ok, ", ".join(detail))
    extra = {"build": build_id, "banner_to_http_ms": http_ms}
    if http_ms:
        print(f"\nbanner to HTTP server: min {min(http_ms):.0f} ms, median "
              f"{statistics.median(http_ms):.0f} ms, max {max(http_ms):.0f} ms "
              f"over {len(http_ms)} boots")
    return report("boot-harness", extra)


if __name__ == "__main__":
    sys.exit(main())
