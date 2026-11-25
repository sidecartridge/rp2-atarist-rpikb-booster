#!/usr/bin/env python3
"""Exercise every tools/dev command against the running Booster and say,
check by check, whether it behaves.

Needs Booster on the RP (a build from tools/dev/flash.sh, so the cached ELF
has symbols), the Debug Probe, and, for a debug build, `tools/dev/console.py
watch` running: it reads the console log, never the UART. The Atari need not
be connected. The RP is reset on the way; the settings are not changed. The
round trip through the core firmware (boot ikbd, then boot booster) runs only
when MODE is not 255, since MODE 255 sends the core firmware straight back
into Booster.

    python3 tools/dev/tools_harness.py                    # against what runs
    python3 tools/dev/tools_harness.py --flash souffle    # flash the debug build first

Exits 0 when every check passes; writes tools/dev/logs/tools-harness-<time>.json.
"""

import argparse
import re
import sys

from harnesslib import (LogCursor, check, flash, report, running_build, skip,
                        swd, swdpy)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--flash", metavar="FLAVOR", choices=("souffle", "croissant"),
                    help="flash the debug build for this board flavor first")
    args = ap.parse_args()
    if args.flash and not flash(args.flash, "debug"):
        return report("tools-harness")

    app, kind, build_id = running_build()
    if not check("Booster runs, from a cached ELF", app == "booster",
                 f"{app} {kind}, build {build_id}"):
        return report("tools-harness")
    debug = kind == "debug"
    elf = swd.running_elf(None)
    rc, out = swdpy("build-id")
    check("build-id: Booster's matches its cached ELF",
          rc == 0 and f"booster: {build_id}" in out,
          " / ".join(out.strip().splitlines()))
    rc, out = swdpy("verify", elf)
    check("verify: the flash matches the ELF", rc == 0)

    cursor = LogCursor()
    rc, _ = swdpy("reset")
    if debug:
        banner = cursor.wait(r"SidecarTridge IKBD Booster\. .*Build " +
                             re.escape(build_id), 20)
        check("reset, and the console shows the boot banner with the build ID",
              rc == 0 and banner is not None,
              "" if banner else "is console.py watch running?")
        check("the HTTP server starts", cursor.wait(
            "HTTP server initialized", 20) is not None)
    else:
        check("reset", rc == 0)
    rc, out = swdpy("running", elf, "--timeout", "20")
    check("running: the Booster ELF", rc == 0, out.strip())

    for name, args_, expect in (
            ("board", ("board",), r"running: booster"),
            ("settings", ("settings",), r"^MODE=\d+$"),
            ("clock", ("clock",), r"low-power profile .*: yes"),
            ("net", ("net",), r"^mode: (AP|STA)$"),
            ("bt", ("bt",), r"^bluetooth: "),
            ("heap", ("heap",), r"heap "),
            ("crash", ("crash",), r"last reset: the watchdog, forced")):
        rc, out = swdpy(*args_)
        check(name, rc == 0 and re.search(expect, out, re.M) is not None,
              out.strip().splitlines()[0] if out.strip() else "")
    rc, out = swdpy("settings")
    check("settings: the Wi-Fi password is masked",
          rc == 0 and re.search(r"^WIFI_PASSWORD=(\*+)?$", out, re.M) is not None)

    mode = swd.settings_value(swd.read_settings(), "MODE")
    if mode == "255":
        skip("boot ikbd, then boot booster", "MODE is 255: the core firmware "
             "goes straight back into Booster")
    else:
        rc, out = swdpy("boot", "ikbd")
        check("boot ikbd: the core firmware runs", rc == 0, out.strip())
        rc, out = swdpy("boot", "booster")
        check("boot booster: CONFIG pressed over SWD, Booster runs", rc == 0,
              out.strip())

    rc, out = swdpy("postmortem", timeout=120)
    check("postmortem: backtraces, then both cores released",
          rc == 0 and "=== backtraces" in out and "both cores released" in out)
    rc, out = swdpy("board")
    check("both cores run after the postmortem",
          rc == 0 and "rp2350.cm0 running, rp2350.cm1 running" in out)
    return report("tools-harness", {"build": build_id})


if __name__ == "__main__":
    sys.exit(main())
