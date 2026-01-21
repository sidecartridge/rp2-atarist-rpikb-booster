# AGENTS.md

This repository contains **rp2-atarist-rpikb-booster**, the Booster firmware and
configuration UI for the rp2-atarist-rpikb project. It builds:

- `booster/` — main Booster firmware (web UI, Bluetooth pairing UI, settings)
- `placeholder/` — fallback app used when no app is installed
- `rp2-atarist-rpikb/` — core IKBD firmware (optional in combined build)

See `README.md` for user-facing behavior and installation flow.

---

## Project layout (important)

- `booster/`  
  Main firmware. Produces `booster-<board>.uf2` in `booster/dist/`.

- `placeholder/`  
  Minimal app. Produces `placeholder-<board>.uf2` in `placeholder/dist/`.

- `rp2-atarist-rpikb/`  
  Core IKBD firmware. Produces `rp2-ikbd-<board>.uf2` in `rp2-atarist-rpikb/dist/`.

- Root scripts:
  - `build.sh` builds Booster + placeholder, and optionally merges in the core
    firmware when a third arg (release type) is provided.
  - `build_uf2.py`, `merge_uf2.py`, `show_uf2.py` for UF2 manipulation.

- Submodules at repo root (do not vendor these):
  - `pico-sdk/`
  - `pico-extras/`
  - `bluepad32/`
  - `rp2-atarist-rpikb/`

---

## Build prerequisites

### 1) Initialize submodules
```sh
git submodule init
git submodule update --init --recursive
```

### 2) Environment variables

Build scripts set these internally, but if you build manually you’ll need:

- `PICO_SDK_PATH` (e.g. `$(pwd)/pico-sdk`)
- `PICO_EXTRAS_PATH` (e.g. `$(pwd)/pico-extras`)

### 3) Toolchain

You need an ARM embedded toolchain (`arm-none-eabi-*`) and CMake. Python is
required for the UF2 merge tools.

---

## Build commands (what agents should run)

Use the repo root `build.sh`:

### Debug build
```sh
./build.sh pico_w debug
```

### Release build
```sh
./build.sh pico_w release
```

### Full image build (includes core firmware)
```sh
./build.sh pico_w release full
```

Notes:
- `build.sh` deletes the root `build/` directory each run.
- With the third arg, `build.sh` also builds `rp2-atarist-rpikb/` and merges a
  combined UF2 into `dist/`.

### Expected artifacts

After a successful build, `dist/` should contain:

- `rp-booster-<version>.uf2` (or `-debug`)
- If full build: `rp-booster-<version>-full.uf2`
- Core firmware UF2 is copied into `dist/` when full build is requested.

---

## Agent workflow rules (read carefully)

### Non-destructive workflow (must follow)

This repo is often edited interactively by a human while the agent is running.
Treat the current filesystem state as the source of truth.

**Never discard or overwrite local changes without explicit user approval.**
In particular, do not run any of these unless the user explicitly says to:

- `git restore` / `git checkout -- <path>` / `git reset` (any form that reverts files)
- `git clean` (any form)
- `git submodule update` / changing submodule SHAs

`build.sh` runs `git submodule update` and **deletes `build/`**. Treat that as
destructive unless the user explicitly requests a build.

### Formatting / linting

- Follow `.clang-format` and `.clang-tidy` in the repo root.
- If you touch C/C++ files, format them and avoid introducing new clang-tidy warnings.

### Submodules

Do not change submodule pins unless explicitly requested. If you do, call it out
clearly in the final response.

### No secrets

Do not add tokens/keys/credentials to the repo. Keep config local.

---

## What “done” looks like for a change

Before considering a change complete:

1. A build succeeds (only if the user asked to build).
2. `dist/` contains `rp-booster-<version>*.uf2`.
3. No obvious style regressions in files you touched.

