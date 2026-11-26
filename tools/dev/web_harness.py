#!/usr/bin/env python3
"""Check Booster's web UI over the bench Wi-Fi: every page and asset is served,
the JSON endpoints answer JSON, and /saveparams.cgi stores what the web UI
sends, read back from flash over SWD.

Puts Booster on the bench Wi-Fi (tools/dev/bench.env) for the run and restores
the settings snapshot afterwards, failure or not; the restore resets the chip.
Needs Booster on the RP (a build from tools/dev/flash.sh) and the Debug Probe.
Bluetooth is not started (tools/dev/bt_harness.py does that).

    python3 tools/dev/web_harness.py

Exits 0 when every check passes; writes tools/dev/logs/web-harness-<time>.json.
"""

import argparse
import os
import statistics
import sys

from harnesslib import (REPO, BenchWifi, check, json_body, report,
                        running_build, saveparams_path, swd)

FS = os.path.join(REPO, "booster", "src", "fs")


def stored(key: str) -> str | None:
    return swd.settings_value(swd.read_settings(), key)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.parse_args()
    app, kind, build_id = running_build()
    if not check("Booster runs", app == "booster", f"{app} {kind}, build {build_id}"):
        return report("web-harness")
    latency = {}
    with BenchWifi() as bench:
        check("on the bench Wi-Fi", bool(bench.ip), bench.ip)

        # Every page and asset in fs/ is served; 404.html is what a missing
        # page gets, with a 404 status.
        for name in sorted(os.listdir(FS)):
            if name.startswith(".") or name == "404.html":
                continue
            status, body, secs = bench.get("/" + name)
            latency[name] = round(secs * 1000)
            check(f"GET /{name}", status == 200 and len(body) > 0,
                  f"{status}, {len(body)} B, {secs * 1000:.0f} ms")
        status, body, _ = bench.get("/no-such-page.html")
        check("a missing page is a 404 with the redirect page", status == 404
              and b"Redirecting" in body, str(status))

        for path, keys in (("/btpairings.cgi", {"keyboard", "mouse", "gamepad"}),
                           ("/btlist.cgi", {"devices"}),
                           ("/factory.cgi", {"factory_mode", "result", "build"})):
            status, body, _ = bench.get(path)
            data = json_body(body)
            check(f"GET {path} answers JSON with {', '.join(sorted(keys))}",
                  status == 200 and isinstance(data, dict) and keys <= set(data),
                  body[:80].decode(errors="replace"))

        # /saveparams.cgi as the web UI sends it, one entry per type, read
        # back from the settings sector in flash.
        speed = stored("MOUSE_SPEED") or "5"
        new_speed = "3" if speed != "3" else "7"
        joystick = stored("JOYSTICK_USB")
        new_joystick = "false" if joystick == "true" else "true"
        entries = [
            {"name": "MOUSE_SPEED", "type": "INT", "value": new_speed},
            {"name": "JOYSTICK_USB", "type": "BOOL",
             "value": "Yes" if new_joystick == "true" else "No"},
            {"name": "USB_KB_LAYOUT", "type": "STRING", "value": "DE"},
        ]
        status, body, _ = bench.get(saveparams_path(entries))
        reply = json_body(body)
        check("saveparams answers status 200", status == 200 and isinstance(reply, dict)
              and reply.get("status") == 200, body[:100].decode(errors="replace"))
        after = {k: v for k, _, v in swd.read_settings()}
        check("saveparams: the INT is stored", after.get("MOUSE_SPEED") == new_speed,
              f"MOUSE_SPEED {speed} -> {after.get('MOUSE_SPEED')}")
        check("saveparams: the BOOL is stored", after.get("JOYSTICK_USB") == new_joystick,
              f"JOYSTICK_USB {joystick} -> {after.get('JOYSTICK_USB')}")
        check("saveparams: USB_KB_LAYOUT is stored lowercased",
              after.get("USB_KB_LAYOUT") == "de", f"-> {after.get('USB_KB_LAYOUT')}")

        before = {k: v for k, _, v in swd.read_settings()}
        status, body, _ = bench.get("/saveparams.cgi?json=%21%21bad")
        reply = json_body(body)
        check("saveparams refuses bad base64 with status 400",
              isinstance(reply, dict) and reply.get("status") == 400,
              body[:100].decode(errors="replace"))
        check("a refused saveparams changes nothing",
              {k: v for k, _, v in swd.read_settings()} == before)
        status, body, _ = bench.get(saveparams_path(
            [{"name": "MOUSE_SPEED", "type": "FLOAT", "value": "1"}]))
        reply = json_body(body)
        check("saveparams refuses an unknown type with status 400",
              isinstance(reply, dict) and reply.get("status") == 400,
              body[:100].decode(errors="replace"))
    check("the settings are back exactly as before the run",
          swd.read_settings() == bench.snapshot)
    if latency:
        print(f"\nfirst GET per page: median {statistics.median(latency.values()):.0f} ms, "
              f"max {max(latency.values())} ms")
    return report("web-harness", {"build": build_id, "latency_ms": latency})


if __name__ == "__main__":
    sys.exit(main())
