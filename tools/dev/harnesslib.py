"""
harnesslib — what the tools/dev harnesses share: PASS,
FAIL and SKIP checks, a cursor on the console log, runs of swd.py and
flash.sh, and the JSON report. Standard library only.

Every harness needs the Debug Probe and, for debug builds, `console.py watch`
running: it reads the console *log* (tools/dev/logs/console.log), never the
UART, so a person can watch the console while a harness runs. Ported from
sidecartos.
"""

from __future__ import annotations

import datetime
import json
import os
import re
import subprocess
import sys
import time

DEV = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(DEV))
LOG = os.path.join(DEV, "logs", "console.log")
BOOT_RE = re.compile(r"SidecarTridge IKBD (Booster|core)\. v\S+ \(")
sys.path.insert(0, DEV)
import swd  # noqa: E402

results: list[dict] = []


def check(name: str, ok: bool, detail: str = "") -> bool:
    results.append({"check": name, "ok": bool(ok), "detail": detail})
    print(f"  {'PASS' if ok else 'FAIL'}  {name}" + (f"  ({detail})" if detail else ""),
          flush=True)
    return bool(ok)


def skip(name: str, why: str) -> None:
    results.append({"check": name, "ok": None, "detail": why})
    print(f"  SKIP  {name}  ({why})", flush=True)


def run(*args: str, timeout: float = 600) -> tuple[int, str]:
    p = subprocess.run(args, cwd=REPO, capture_output=True, text=True,
                       timeout=timeout)
    return p.returncode, p.stdout + p.stderr


def swdpy(*args: str, timeout: float = 180) -> tuple[int, str]:
    return run(sys.executable, os.path.join(DEV, "swd.py"), *args,
               timeout=timeout)


def flash(flavor: str, build_type: str) -> bool:
    rc, out = run(os.path.join(DEV, "flash.sh"), flavor, build_type, timeout=900)
    return check(f"flash.sh {flavor} {build_type}", rc == 0,
                 out.strip().splitlines()[-1] if out.strip() else "")


class LogCursor:
    """What the console log gained since the cursor was placed."""

    def __init__(self) -> None:
        self.pos = os.path.getsize(LOG) if os.path.exists(LOG) else 0

    def text(self) -> str:
        if not os.path.exists(LOG):
            return ""
        size = os.path.getsize(LOG)
        if size < self.pos:       # the log rotated
            self.pos = 0
        with open(LOG, encoding="utf-8", errors="replace") as f:
            f.seek(self.pos)
            return f.read()

    def wait(self, pattern: str, timeout: float) -> str | None:
        """The first new line matching the regex, or None after the timeout."""
        rx = re.compile(pattern)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for line in self.text().splitlines():
                if rx.search(line):
                    return line
            time.sleep(0.2)
        return None


def running_build() -> tuple[str, str, str]:
    """(image, build type, build ID) of what runs, from `swd.py board`."""
    rc, out = swdpy("board")
    m = re.search(r"running: (\w+) (\w+), build (\S+)", out)
    if rc or not m:
        raise swd.SwdError("cannot tell what runs: " + out.strip()[-200:])
    return m.group(1), m.group(2), m.group(3)


def report(name: str, extra: dict | None = None) -> int:
    failed = [r for r in results if r["ok"] is False]
    passed = [r for r in results if r["ok"] is True]
    skipped = [r for r in results if r["ok"] is None]
    os.makedirs(os.path.join(DEV, "logs"), exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    path = os.path.join(DEV, "logs", f"{name}-{stamp}.json")
    with open(path, "w") as f:
        json.dump({"harness": name, "time": stamp, "results": results,
                   **(extra or {})}, f, indent=2)
    print(f"\n{len(passed)} passed, {len(failed)} failed, {len(skipped)} skipped; "
          f"report {os.path.relpath(path, REPO)}")
    return 1 if failed else 0


# --- Booster on the bench Wi-Fi ----------------------------------------------------

import base64  # noqa: E402
import json  # noqa: E402
import urllib.error  # noqa: E402
import urllib.parse  # noqa: E402
import urllib.request  # noqa: E402


class BenchWifi:
    """Booster on the bench Wi-Fi (tools/dev/bench.env) for the duration of a
    `with` block, its settings snapshot restored afterwards, failure or not.
    The restore rewrites the settings sector and resets the chip."""

    def __init__(self, timeout: float = 60) -> None:
        self.timeout = timeout
        self.snapshot = None
        self.ip = ""

    def __enter__(self) -> "BenchWifi":
        self.snapshot = swd.apply_settings(swd.bench_settings())
        if swd.settings_value(self.snapshot, "MODE") != "255":
            ok, how = swd.boot_booster(self.timeout)
            if not ok:
                raise swd.SwdError(f"Booster does not run ({how})")
        self.ip = swd.wait_wifi(self.timeout, want_sta=True)["ip"]
        return self

    def __exit__(self, *exc) -> None:
        if self.snapshot:
            swd.write_settings(self.snapshot)
            print("  (settings restored)", flush=True)

    def get(self, path: str, timeout: float = 10) -> tuple[int, bytes, float]:
        """HTTP status, body and seconds for one GET; status 0 on no answer."""
        start = time.monotonic()
        try:
            with urllib.request.urlopen(f"http://{self.ip}{path}",
                                        timeout=timeout) as r:
                body = r.read()
                return r.status, body, time.monotonic() - start
        except urllib.error.HTTPError as e:
            return e.code, e.read(), time.monotonic() - start
        except OSError:
            return 0, b"", time.monotonic() - start


def saveparams_path(entries: list[dict]) -> str:
    """The request the web UI sends: encodeURIComponent(btoa(JSON.stringify()))."""
    raw = json.dumps(entries, separators=(",", ":")).encode()
    return "/saveparams.cgi?json=" + urllib.parse.quote(
        base64.b64encode(raw).decode(), safe="")


def json_body(body: bytes):
    try:
        return json.loads(body.decode("utf-8", errors="replace"))
    except ValueError:
        return None
