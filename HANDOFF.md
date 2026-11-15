# DualApp handoff (state on 2026-11-15)

Read this first, then the files in "Read in this order".

## What the project is

DualApp is a **no-root Android multi-instance app manager**: it runs several copies of one app (clones) on one phone. Target phone: Samsung Galaxy S25+ (SM-S936B), Android 17 / API 37, One UI 9, arm64. Target apps: **MLBB** (Mobile Legends) and **Instagram**. The key goal is letting MLBB clones **share its ~12 GB of game files** instead of each clone downloading its own copy.

The clone engine is **NewBlackbox** (open source, upstream commit `89b5983`). It is not vendored: `phase0/engine/fetch-newblackbox.sh` clones it into `phase0/engine/NewBlackbox/` (gitignored) and applies our patches.

## Where we are

- **Phase 0 ended NO-GO** (`phase0/REPORT.md`). Clones work, and MLBB runs after patch 0001. But shared storage needs a copy-on-write hook layer ("DualFs") that doesn't exist yet, and the engine is fragile on Android 17.
- **Phase 0b** is the current phase: 14 tasks, T0–T13, in `phase-0b-plan.md`. They settle feasibility before Phase 1.
- **T1 (framework diff) is DONE.** See `phase0b/results/t1/t1-summary.md`.
  - There is no new crash source on Android 17 / One UI 9.
  - It found two silent bugs, now fixed by patches 0004 and 0005 (`phase0/engine/patches/`).
- **The engine on the phone is the build with patches 0001 + 0004 + 0005**, installed 2026-11-14 as an update. The engine and a Canary clone run with no crashes (`phase0b/results/t1/engine-update-0004-0005.txt`).
- **Neither 0004 nor 0005 is proven at runtime yet.**
  - 0004 is checked by T11 step 3: a guest's JobScheduler/WorkManager job runs.
  - 0005 is checked by kit case K9: `onNewIntent` reaches a guest activity that is already running. K9 needs a Canary v0.3 compat page.
- **T0 (baseline) is DONE** (2026-11-15). See `phase0b/results/t0/t0-baseline.txt`.
  - Firmware, security patch and ART are unchanged from Phase 0; free space is now **14 GB** (was 16–19 GB).
  - The installed engine is the **0001 + 0004 + 0005** build (versionCode 400, updated 2026-11-14 02:26:31).
  - Per-UID network counter: **`dumpsys netstats detail`** works; `dumpsys connectivity trafficcontroller` has no byte counters on this firmware.
- **T2 (runtime audit) is DONE** (2026-11-15). See `phase0b/results/t2/t2-runtime-audit.txt`.
  - VirtualSpoof works. AntiDetection, FileSystemHook and ResourceLoading log "installed" but install nothing.
  - The HiddenAPI exemption fails on Android 17; UnixFileSystem hooks 8 of 9 (`canonicalize0` changed); GmsProxy is inactive.
  - `ANDROID_ID` is identical in the normal app and Users 0/1; a native hard-coded `/sdcard` write escapes the container.
  - **New engine bug:** the `:black` process is frozen and killed by Android's app freezer, which can crash a guest's `startActivity`. Needs a T12/T13 fix.
- **T3 (filesystem probe) is DONE** (2026-11-15). See `phase0b/results/t3/t3-fs-probe.txt`.
  - **xattr tag storage works** on the external path and from a guest; it survives rename.
  - **The pool must live on the external FUSE mount** (`st_dev=182`); cross-mount `rename(2)` is `EXDEV`.
  - Sparse placeholders cost **0 blocks**; 10,000 placeholders cost **~4.04 KiB each**.
  - Symlinks are denied on external; `io_uring_setup` is killed by seccomp (`SIGSYS`).
  - No need to move MLBB's external data to internal storage.
- **T4 (MLBB inventory) is DONE** (2026-11-15). See `phase0b/results/t4/`.
  - Host tree: 161,316 files / 11.77 GiB; `assets/Art` is 69% of it.
  - Overlap (img excluded): 10,954 files, **10,931 identical (99.79%)**, 23 different (metadata/login/identity).
  - 4-minute run: 8 files changed in place, 8,042 new files (~462 MiB) downloaded; order UI → Art → Audio → Video.
  - N1 confirmed: preferences and `base_loadres_version_info` cache the instance's own absolute root.
  - Draft profile: `phase0b/results/t4/mlbb-sharing-profile.json`.
- **T5 (M8 prototype) is IN PROGRESS** (2026-11-15).
  - **Hook spike passed:** all six Dobby hooks installed, 0 crashes, **+40 ns/call**, `__openat` caught all 1,011,014 opens. Technique A selected. See `phase0b/results/t5/t5-backend.txt`.
  - **Patches 0002 and 0003 written:** `NativeCore.enableDualFs` + `BActivityThread` call, and `Bcore/src/main/cpp/dualfs/` (config, placeholder, copy-up, hooks). The engine builds; 0001+0004+0005+0002+0003 apply cleanly to a fresh `89b5983`.
  - **Remaining:** `dualfs-tool`, `dfaudit`, Canary v0.3 attack matrix, PackLoader workloads, M9 spike, engine update (needs approval) and R7.
- **Not started:** T6–T13.

## What to do next

Follow `phase-0b-plan.md` Section 5 task by task. Each task has a procedure, deliverables (Section 10) and fixed pass/fail criteria (Section 7). Don't reinterpret the criteria afterwards.

The suggested order is:

1. **Finish T5** (`dualfs-tool`, `dfaudit`, Canary attack matrix, PackLoader workloads, M9 spike, engine update).
2. **T6–T13** after it.

Record each task's text results under `phase0b/results/<task>/`. Add a short "T<n> result" note under that task in `phase-0b-plan.md`, as was done for T1.

## Read in this order

1. `HANDOFF.md` (this file)
2. `phase-0b-plan.md`: the plan. Sections 2.1, 3.8, 5, 7 and 10 matter most.
3. `phase0/REPORT.md`: Phase 0 results and why it was NO-GO.
4. `phase0b/results/t1/t1-summary.md`: the latest finished work.
5. `plan.md`: the original product plan. Section 9 covers storage design, and Section 20 is the Phase 0 day log. Read only what a task needs.

## Environment (Windows PC + USB phone)

- Repo: `C:\Users\child\PROJECTS\DualApp`, branch `main`, remote `github.com/Horali/DualApp` (private).
- Phone: serial `RRCY1032Y6F`, USB, already authorized.
  - adb is at `C:/Users/child/AppData/Local/Android/Sdk/platform-tools/adb.exe`.
  - Use **Git Bash**, not PowerShell (PowerShell mangles quotes and paths). Run `export MSYS_NO_PATHCONV=1` first.
- Build-tools (`dexdump` etc.): `C:/Users/child/AppData/Local/Android/Sdk/build-tools/36.0.0/`.
- **Build the engine** (offline, about 30 s):

  ```bash
  cd phase0/engine/NewBlackbox
  export JAVA_HOME="/c/Program Files/Android/Android Studio/jbr" GRADLE_USER_HOME=/c/Users/child/PROJECTS/DualApp/phase0/.gradle-home
  ./gradlew --offline :app:assembleDebug
  ```

  - The APK is `app/build/outputs/apk/debug/BlackBox_4.0.0_arm64-v8a-debug.apk`.
  - Install with `adb install -r` (an update that keeps the clones' data).
  - The engine package is `top.niunaijun.blackbox`, and it's debuggable, so `adb shell run-as top.niunaijun.blackbox ...` works.
- **Re-run the framework diff:** `phase0b/scripts/framework-diff.sh`. It pulls the phone's framework jars into `phase0b/work/` (gitignored) and compares them with the engine.
- **Save tokens:** adb output (logcat, dumpsys) is huge. Always filter it on the PC (`grep`, `head`, `tail`, or a script that prints a summary) before reading it.

## Rules (from the owner; follow them exactly)

- **The phone:**
  - Ask the owner before anything that changes it: installing or uninstalling, launching a guest app, changing settings, or deleting app data.
  - Read-only actions are fine without asking: `adb pull`, `dumpsys`, `getprop`, logcat.
  - Instagram login happens only with the owner's approval and a throwaway account.
- **Engine source:**
  - Never hand-edit it without saving the change as a numbered patch in `phase0/engine/patches/`. 0002/0003 are reserved for DualFs; compat fixes are `00NN-compat-*`.
  - A patch must apply cleanly to a fresh `89b5983` checkout. The engine files use CRLF line endings; keep them.
- **The engine's spoofing / anti-detection code:** keep it as shipped. Every result is labelled "upstream engine, spoofing on".
- **Commits:**
  - Use Conventional Commits (`type(scope): summary`, lowercase, imperative, about 72 characters or less). Make small commits and push to `main` often.
  - Use GitHub account **Horali** (`gh auth switch -h github.com -u Horali`).
  - **No AI attribution** of any kind in commits or PRs (no Co-Authored-By, no "Generated with").
- **Never commit:** pulled phone jars and dex dumps, APKs, screenshots of the phone, `.env`, or build outputs. Keep `.gitignore` up to date; `phase0b/work/` is already ignored.
- **Explaining to the owner:**
  - Use plain, simple English; the owner is learning.
  - Show results (tables, screenshots) instead of just describing them.
  - Say honestly when an idea is weak.
