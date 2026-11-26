"""Booster's and the core firmware's settings defaults must hold the same keys:
a stored key the running firmware does not know is dropped on load and gone
after its next save, so a key only one of them has is lost whenever the other
one saves. Reads both gconfig.c tables with build_factory_uf2.py's parser."""
import os
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, REPO)
import build_factory_uf2  # noqa: E402

booster = build_factory_uf2.default_keys(os.path.join(REPO, "booster", "src"))
core = build_factory_uf2.default_keys(os.path.join(REPO, "rp2-atarist-rpikb", "src"))
if not booster or booster != core:
    print(f"settings keys differ: only in Booster {sorted(booster - core) or 'none'}, "
          f"only in the core {sorted(core - booster) or 'none'}")
    sys.exit(1)
print(f"test_default_keys.py: {len(booster)} keys, identical in both firmwares")
