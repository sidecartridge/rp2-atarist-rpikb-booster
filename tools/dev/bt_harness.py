#!/usr/bin/env python3
"""Start and stop Booster's Bluetooth from the web UI over the bench Wi-Fi and
measure what it does to HTTP: latency before, during scanning and after
/btstop.cgi, cycle after cycle. After a stop, HTTP must be back to its
baseline (the radio is shared: a Bluetooth stack left running slows Wi-Fi),
and a new /btstart.cgi must work without a reboot.

Puts Booster on the bench Wi-Fi (tools/dev/bench.env) for the run and restores
the settings snapshot afterwards; the restore resets the chip, which also
ends any Bluetooth activity. Nothing is paired. Needs Booster on the RP (a
build from tools/dev/flash.sh) and the Debug Probe.

    python3 tools/dev/bt_harness.py --cycles 3

Exits 0 when every check passes; writes tools/dev/logs/bt-harness-<time>.json.
"""

import argparse
import re
import statistics
import sys
import time

from harnesslib import (BenchWifi, check, json_body, report, running_build,
                        swd, swdpy)

PAGE = "/mngr_home.shtml"
SAMPLES = 10
# HTTP after a stop counts as back to baseline within twice the baseline
# median or 100 ms more, whichever is larger.
SLACK_FACTOR, SLACK_MS = 2.0, 100.0


def latency(bench: BenchWifi) -> tuple[float, int]:
    """Median ms of SAMPLES GETs of PAGE, and how many failed."""
    times, failed = [], 0
    for _ in range(SAMPLES):
        status, _, secs = bench.get(PAGE, timeout=8)
        if status == 200:
            times.append(secs * 1000)
        else:
            failed += 1
        time.sleep(0.1)
    return (statistics.median(times) if times else float("inf")), failed


def scanning() -> bool | None:
    rc, out = swdpy("bt")
    m = re.search(r"^bluetooth: .*, (scanning|not scanning)$", out, re.M)
    return None if rc or not m else m.group(1) == "scanning"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--cycles", type=int, default=2)
    args = ap.parse_args()
    app, kind, build_id = running_build()
    if not check("Booster runs", app == "booster", f"{app} {kind}, build {build_id}"):
        return report("bt-harness")
    figures = []
    with BenchWifi() as bench:
        check("on the bench Wi-Fi", bool(bench.ip), bench.ip)
        base, failed = latency(bench)
        check("baseline HTTP, Bluetooth never started", failed == 0,
              f"median {base:.0f} ms")
        limit = max(base * SLACK_FACTOR, base + SLACK_MS)
        for cycle in range(1, args.cycles + 1):
            status, _, _ = bench.get("/btstart.cgi")
            time.sleep(3)
            check(f"cycle {cycle}: /btstart.cgi, Bluetooth scanning",
                  status == 200 and scanning() is True, str(status))
            during, failed = latency(bench)
            check(f"cycle {cycle}: HTTP answers while scanning", failed == 0,
                  f"median {during:.0f} ms, {failed} failed")
            status, body, _ = bench.get("/btlist.cgi")
            data = json_body(body)
            check(f"cycle {cycle}: /btlist.cgi answers JSON",
                  status == 200 and isinstance(data, dict) and "devices" in data,
                  f"{len(data.get('devices', [])) if isinstance(data, dict) else '?'}"
                  " devices nearby")
            status, _, _ = bench.get("/btstop.cgi")
            time.sleep(2)
            check(f"cycle {cycle}: /btstop.cgi, Bluetooth stopped",
                  status == 200 and scanning() is False, str(status))
            after, failed = latency(bench)
            check(f"cycle {cycle}: HTTP back to baseline after the stop",
                  failed == 0 and after <= limit,
                  f"median {after:.0f} ms (baseline {base:.0f}, limit {limit:.0f})")
            figures.append({"cycle": cycle, "during_ms": round(during),
                            "after_ms": round(after)})
    check("the settings are back exactly as before the run",
          swd.read_settings() == bench.snapshot)
    print(f"\nbaseline {base:.0f} ms; " + "; ".join(
        f"cycle {f['cycle']}: scanning {f['during_ms']} ms, after stop "
        f"{f['after_ms']} ms" for f in figures))
    return report("bt-harness", {"build": build_id, "baseline_ms": round(base),
                                 "cycles": figures})


if __name__ == "__main__":
    sys.exit(main())
