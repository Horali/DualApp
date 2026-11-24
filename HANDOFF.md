# DualApp handoff (2026-11-24)

Read this first. It is the whole state of the project: what it is, what is done, what is next, the rules, and how to build/run everything. The next session should not need the old chat. If a chat message contradicts this file, **this file wins** — verify against the repo and the phone before trusting an old ticket.

## What the project is

DualApp is a **no-root Android multi-instance app manager**: it runs several copies of one app (clones) on one phone. Target phone: Samsung Galaxy S25+ (**SM-S936B**), Android 17 / API 37, One UI 9, arm64. Target apps: **MLBB** (Mobile Legends: Bang Bang, `com.mobile.legends`) and **Instagram** (`com.instagram.android`).

The key goal: MLBB clones **share its ~12 GB of game files** instead of each clone downloading its own copy. The proposed design is **M8 (placeholder overlay)**:

- each clone keeps a private **sparse placeholder** for every shared file (right size, ~0 data blocks);
- a native hook hands a read-only pool blob to the app on read;
- on write, the placeholder is copy-up'd into a private full file;
- the pool blob is never exposed as a path the guest can see.

The clone engine is **NewBlackbox** (open source, upstream commit `89b5983`). It is not vendored: `phase0/engine/fetch-newblackbox.sh` clones it into `phase0/engine/NewBlackbox/` (gitignored). Our patches live in `phase0/engine/patches/`.

## Current phase and status

- **Phase 0 ended NO-GO** (`phase0/REPORT.md`): clones work and MLBB runs after patch 0001, but shared storage needs a copy-on-write hook layer (DualFs) that did not exist, and the engine is fragile on Android 17.
- **Phase 0b** is the current phase: 14 tasks, **T0–T13**, in `phase-0b-plan.md`. Each task has a procedure (Section 5), deliverables (Section 10) and fixed pass/fail criteria (Section 7). Do not reinterpret the criteria.
- **T0–T6 are DONE (T6 except its reboot half). T7–T13 are not started.** T5 is down to two remaining items: PackLoader workloads and the M9 spike.

## Task results so far

### T1 — Android 17 framework diff (DONE, 2026-11-14)

`phase0b/results/t1/t1-summary.md`

- No new crash source: all 4 implemented framework interfaces match the phone; 2,858 Samsung-only methods classified, **0 crash**.
- Two silent bugs found and patched:
  - **0004** `IJobServiceProxy` namespace: guest JobScheduler/WorkManager jobs probably never scheduled.
  - **0005** `handleNewIntent`: the phone only has `handleNewIntent(ActivityClientRecord, List)`; none of the engine's 3 forms matched, so new intents were dropped.
- Both patches are in the installed engine (`0001 + 0004 + 0005`) but **not proven at runtime**: 0004 needs T11 step 3, 0005 needs kit case K9 (Canary v0.3 compat page).
- Smaller gaps: `SigningInfo` for guests with no host install; 3 `callActivityOnNewIntent` variants not forwarded; `setActivityLocusContext` cast; fake-location `onLocationChanged`.

### T0 — baseline (DONE, 2026-11-15)

`phase0b/results/t0/t0-baseline.txt`

- Firmware/security patch/ART unchanged from Phase 0: `S936BXXUDDZJB`, patch 2026-10-05, ART `372042720`, Android 17 / API 37, One UI 9.0.
- Free space: **14 GB** (Phase 0 had 16–19 GB). Above the 4 GB stop line; T10 needs ≥ 8 GB.
- Engine at T0: **0001 + 0004 + 0005**, versionCode 400, targetSdk 28, UID 10057.
- Per-UID network counter: **`dumpsys netstats detail`** works (828 UID entries, `uid=<n> ... rb/tb`). `dumpsys connectivity trafficcontroller` has **no byte counters** on this firmware.

### T2 — engine runtime audit (DONE, 2026-11-15)

`phase0b/results/t2/t2-runtime-audit.txt`

- **VirtualSpoof is active** (`__system_property_get` hook installed; `[spoof] ro.hardware = qcom`, etc.).
- **AntiDetection, FileSystemHook and ResourceLoading log "installed" but install nothing** (source confirms no `DobbyHook`).
- **HiddenAPI exemption fails on Android 17**: `setHiddenApiExemptions` is no longer an exported libart symbol. No crash seen.
- **UnixFileSystem hooks 8 of 9**: `canonicalize0(String)` is now a non-native wrapper; the native form is `canonicalize0(String,Z)`. `File.getCanonicalPath()` is not redirected.
- **GmsProxy inactive** ("Failed to get gms service binder"). **AntiVirtualDetect proxy never registered**.
- Property readers: Java `Build`, modern native `__system_property_find`/`read_callback`, and `getprop` children all see the **real Samsung** values. Only the old native API sees **Pixel 6 / Android 12**, and only in the clones.
- **`ANDROID_ID` is identical in the normal app, User 0 and User 1** (hash `2746e481e11f`): all clones share one ID (N6 identity link).
- **Native hard-coded `/sdcard` escape**: `/data/data/<other app>` is denied, but `/sdcard/Download/...` write succeeds (engine has All Files Access).
- **Engine bug: `:black` freezer kill.** Android's app freezer freezes the engine's `:black` process and the system kills it on the first sync binder call. A guest `startActivity` then fails with `SecurityException` and an ANR. Needs T12/T13 work.
- BTI/PAC present in libc; Dobby hook still installed.

### T3 — filesystem probe (DONE, 2026-11-15)

`phase0b/results/t3/t3-fs-probe.txt`

- **Tag storage: xattr** (`user.dualapp.blob`). Works on the external path and from a guest; survives rename.
- **Pool must be external**: L-ext/L-guest are FUSE (`st_dev=182`), internal is f2fs (`st_dev=65099`). Raw `rename(2)` across them is `EXDEV`.
- No need to move MLBB's external data to internal: sparse files, xattrs, inotify and `copy_file_range` all work on external.
- Placeholder cost: a real placeholder has **`st_blocks=8` (4 KiB)** — still ~4 KiB per file (T3 budget ≤ 8 KiB). The placeholder test now checks `st_size > 0 && st_blocks <= 8` (updated during T6 so even a 16-byte raw write bumps `st_blocks` to 16 and triggers fail-private).
- 10,000 placeholders cost **40,424 KiB ≈ 4.04 KiB each**.
- Symlinks are **denied** on the external path, so M1 is impossible there; M8 does not use symlinks.
- `fchmod` on a read-only handle and reopen through `/proc/self/fd` are **both allowed** → the M8 guards are required.
- `copy_file_range`: 64 MiB in 83 ms (**767 MB/s**).
- **`io_uring_setup` is blocked fatally**: seccomp kills the process with `SIGSYS` (syscall 425), not `EPERM`.
- `RLIMIT_NOFILE`: 32768 soft / 524288 hard, same in guest and `run-as`.

### T4 — MLBB inventory (DONE, 2026-11-15)

`phase0b/results/t4/` (summary `t4-mlbb-inventory.txt`, classification `t4-classification.md`, profile `mlbb-sharing-profile.json`, order `t4-download-order-A.txt`)

- Host tree: **161,316 files / 175 folders / 11.77 GiB**. `assets/Art` 8.36 GiB (69%), `Audio` 1.10 GiB, `img` 1.01 GiB, `UI` 768 MiB, `Document` 164 MiB, `comlibs` 136 MiB.
- Content types ≥ 64 KiB: UnityFS 15,565, other 4,441, **ELF 12** (all `comlibs`), dex 0, zip 0.
- Clone A: 58,281 files / **1,958.2 MiB**; `img/` copy 47,069 files / 906.7 MiB.
- Overlap (img excluded, full SHA-256): 10,954 files in both, **10,931 identical (99.79%)**, **23 different**.
- 4-minute run (no login): **8 files changed in place**, **8,042 new files (~462 MiB) downloaded**, 0 deleted; network ~480 MiB. Order: **UI → Art → Audio → Video**.
- **N1 confirmed**: `__SharedPreference__.xml` (`moba_loading_bg`, `__Extracted_PersistentFilesPath__`), PlayerPrefs `RWPathRec`, `DeviceSettings.xml` and `android/base_loadres_version_info` all cache **clone A's own absolute root**.
- **N2/N6**: `login_data/` is external and instance-specific; identity files found are internal only (`files/__app_uuid__`, `snssdk_openudid.xml`, `DeviceSettings.xml`).
- **N3**: APK payloads are **misspelled** `assets/Resouces4-1..4.dat` (20.5–29.2 MB) and `lib/arm64-v8a/libResouces_z*.so` (12 ELF files). No file with those names is in either tree; output is renamed/repacked into identical, non-ELF bundles (shareable). The ELF payloads are private.
- **Host internal supplement (2026-11-23, read-only root recon, `phase0b/results/t4/t4-host-internal-supplement.txt`):** confirmed `.DNA_`, `.DNA_FLAG`, `.gp_uuid`, `INSTALLATION`, `AdjustAttribution`, `com.ss.android.deviceregister.utils.Cdid.xml`, and `com.dvi.internal.xml` exist on the full host MLBB install (all internal; host 202 MB internal + 12 GB external vs Clone A 153 MB internal + 2.7 GB external); host vs Clone A SHA-256 hashes for `__app_uuid__`, `snssdk_openudid.xml`, `DeviceSettings.xml`, and PlayerPrefs all differ (no contents read); external `assets/_finish` is a 0-byte completion marker and `PatchFileList*.xml` exist on host. Add `*Cdid*.xml` and `com.dvi.internal.xml` to `dualfs-tool.c` and `mlbb-sharing-profile.json` hard excludes.

### T5 — M8 prototype (IN PROGRESS)

`phase0b/results/t5/t5-backend.txt`

**Done:**

1. **Hook-technique spike passed** (PackLoader v0.1): Dobby installed hooks on `open`, `openat`, `__openat`, `fchmod`, `fopen`, `opendir`, 0 crashes. 1M opens: 3,071 → 3,111 ns/call = **+40 ns/call** (budget 1 µs). `__openat` caught **1,011,014** opens; `open` alone ~10k. **`__openat` is the primary hook target.** Technique A (Dobby) selected.
2. **Patch 0002** (`0002-dualfs-init.patch`): `NativeCore.enableDualFs`, called in `BActivityThread.handleBindApplication` after `IOCore.enableRedirect`, only if the instance has a DualFs config. JNI registration in `BoxCore.cpp`; sources in `Android.mk`.
3. **Patch 0003** (`0003-dualfs-native.patch`): `Bcore/src/main/cpp/dualfs/` — config, counters, xattr tag, placeholder test, striped-flock copy-up (temp, `copy_file_range`/fallback, fsync, futimens, rename, dir fsync), blob handle table, blob read redirect, `O_TRUNC`, and the hook set.
4. **`dualfs-tool`** (`phase0b/tools/dualfs-tool/`, `build.sh`): `harvest`, `seed <manifest>`, `rehydrate`, `verify-tree`, `scrub`, `stats`, `cleanup`, `recover`. Self-test on 101 × 64 KiB files: **101/101** harvest, verify, rehydrate; tree **6,472 → 412 KiB** and back. Hard-excludes `login_data/` and identity files; journal at `pool/journal/harvest.log`.
5. **`dfaudit`** (`phase0b/tools/dfaudit/`, `build.sh`): inotify watcher; prints `<time> <masks> <path> <placeholder|private>`.
6. **Engine update installed + R7 PASS** (2026-11-23, `phase0b/results/t5/t5-r7.txt`): `adb install -r` kept all clone data (154M internal / 2.5G external, uid 10057). Play Protect returned `VERIFICATION_ALLOW`, but **Samsung Device security / App Protection** (`com.samsung.android.sm.devicesecurity`, `AppVerificationDialog`) separately prompts and the owner must approve it on every engine install. With no DualFs config, Canary `consistent=true` in User 0 (`74d0626c`, run 21) and User 1 (`9180c35e`, run 6), MLBB clone A reached Create Character (~35 s), DualFs dormant (0 crashes, 0 DualFs lines; 1 known `:black` freezer kill recovered).
7. **Canary v0.3 + force-stop C22 PASS** (2026-11-23, `phase0b/results/t6/t6-conformance.txt`, `phase0b/scripts/conformance-v2.sh`): fixed 0002 guest config lookup (`IStorageManagerProxy` double-redirected `getExternalFilesDir` inside `:p0`) and 0003 (`BlobEntry` placeholder tracking, `/proc/self/fd/N` reopen copy-up, `fchmodat`/`utimensat`/`readlinkat`/6 `*xattr` hooks, `st_blocks > 8` fail-private rule, pre-`O_TRUNC` tag removal, multi-`root=`/`tmp_int=`). All 5 patches apply cleanly to `89b5983`.

**Pending in T5 & pre-T7/T8 code gaps (not yet coded):**

- **PackLoader workloads (T9 prep):** 1.5 GiB deterministic pack (`xoshiro256**`, T4 ≥ 64 KiB histogram), simulated downloader, index modes `I-shared` / `I-private-verify` / `I-private-blind`, workloads W1–W5, `:worker` process (for T8), and 100k micro-benchmarks.
- **M9 spike (PoolHost):** `phase0b/apps/poolhost/` (`dev.dualapp.poolhost`) owns one 256 MiB read-only blob; Canary requests handles 1,000 times via `ContentProvider.openFile` and 1,000 times via abstract Unix socket + `SCM_RIGHTS`: latency median/p99, hash, `fchmod` → `EPERM`, `/proc/self/fd/N` `O_RDWR` reopen → `EACCES`.
- **Pre-T7/T8 code gaps:**
  - `DualFs.cpp` (`0003-dualfs-native.patch`): wire `instance.lock` (`flock(LOCK_EX | LOCK_NB)` on startup to run `cleanTmp()` only if first process in the instance, then downgrade to `LOCK_SH` for process lifetime).
  - `dualfs-tool.c`: acquire `instance.lock` (`LOCK_EX | LOCK_NB`) in `harvest`/`seed`/`rehydrate` so maintenance refuses while a guest runs (T8 step 4); make `scrub` remove placeholders pointing to quarantined blobs (T7 step 6 / P9); add `com.ss.android.deviceregister.utils.Cdid.xml` and `com.dvi.internal.xml` to hard excludes (and `mlbb-sharing-profile.json`).

### T6 — storage conformance suite v2 (DONE except reboot half)

`phase0b/results/t6/t6-conformance.txt`, `phase0b/scripts/conformance-v2.sh` (**upstream engine, spoofing on**)

- All **15/15** Dobby hooks installed `OK`; **`FAILS=0`** across `C1`–`C21` (Java + JNI, external FUSE + internal f2fs), `VERIFY_U1` (`39/39` good), and `C22_CHECK` after force-stop.
- Pool hashes/modes (`0444`)/mtimes untouched after every case; User 1 stayed 39/39; `dfaudit` detected raw-syscall `C18` (`ACCESS`) and `C19` (`MODIFY`) with **0 false alarms** elsewhere.
- Kernel inotify limits (read via the owner's root daemon): `max_user_watches=72456`, `max_user_instances=128` (400× headroom for MLBB's 175 folders).
- **Still open: C22 reboot half** — deferred to the very end of Phase 0b per owner instruction so the Termux `DirtyFrag` root daemon stays alive during remaining research and benchmarks (can be re-seeded in ~90 s via `conformance-v2.sh` right before the final reboot if T6 scratch is cleaned earlier).

## Phone state (verified read-only 2026-11-24 22:50)

| Item | Value |
|---|---|
| Device | `RRCY1032Y6F`, SM-S936B, Android 17 / API 37, fingerprint `S936BXXUDDZJB`; Wi-Fi ADB previously enabled on `192.168.1.17:5555` (when connected, verify `getprop ro.serialno` == `RRCY1032Y6F`) |
| Second device on the bus | **`0953925324001286`** (`Infinix_X6815D`, unrelated) — always pass `-s RRCY1032Y6F` |
| Engine | `top.niunaijun.blackbox`, versionCode **400**, targetSdk 28, last updated **2026-11-23 09:33**; carries `0001+0004+0005+0002+0003` |
| Free space | **6.8 GB** on `/data` (97% used, `212G` / `219G`) — above the 4 GB stop line, **below the ≥ 8 GB T10 needs**. T9 (1.5 GiB) fits if Native control's 1.5 GiB data is removed before running P1/P2; T10 needs ~1.2 GB+ more freed. |
| Cleanable T4/T6 scratch (~192 MB) | `/data/local/tmp/t4` (54M), `t6_*.bin` (65M), T6 Canary pool (65M), Canary trees (8.3M) — ask owner before deleting |
| DualFs state dir | `.../blackbox/dualfs/inst/0` and `/inst/1` exist (from T6) |
| Pool | `.../blackbox/dualfs/pool/dev.dualapp.canary` (T6 pool intact) |
| Installed test apps | Canary v0.3 (normal + clones in engine Users 0/1), PackLoader v0.1 (normal app, no data) |
| MLBB | Clone A in engine User 0, ~2.7 GB external tree (153 MB internal); Host MLBB 202 MB internal + 12 GB external — real MLBB/Instagram/Secure Folder only ever read |
| Root research channel (optional, non-persistent across reboot) | Owner's Termux `DirtyFrag` command daemon (`/sdcard/cmd.tmp` → atomic `mv` to `/sdcard/cmd.sh` → `/sdcard/cmd.out` ending with `__DONE__`; `uid=0`, `u:r:ksu:s0`). **Strictly for read-only recon & `drop_caches` only** — DualApp/engine/`dualfs-tool`/`dfaudit` must stay 100% non-root (`run-as top.niunaijun.blackbox`, UID `10057`), never write root-owned files into engine/guest folders |

## What to do next, in order (no phone reboots until final step)

1. Ask owner approval for the next phone-mutating batch: cleaning ~192 MB T4/T6 scratch, installing `PoolHost` + updated `Canary v0.3` + `PackLoader` + `instance.lock` engine update, and running M9 / T9 / T7 / T8 (**no phone reboot yet**).
2. Update hard excludes (`*Cdid*.xml`, `com.dvi.internal.xml`) in `mlbb-sharing-profile.json` and `dualfs-tool.c` (plus clean approved scratch).
3. **T5 Step 9 (M9 spike):** build `phase0b/apps/poolhost/` (`dev.dualapp.poolhost`) + Canary M9 client probe (256 MiB read-only blob; 1,000 × `ContentProvider.openFile` and 1,000 × abstract Unix socket `SCM_RIGHTS`; latency median/p99, hash, `fchmod` → `EPERM`, `/proc/self/fd/N` `O_RDWR` → `EACCES`) → `phase0b/results/t5/t5-m9-spike.txt` (delete 256 MiB test blob afterward).
4. **Wire `instance.lock` & P9 `scrub` placeholder removal:** update `0003-dualfs-native.patch` (`DualFs.cpp`) and `dualfs-tool.c`, verify all 5 patches apply cleanly to fresh `89b5983`, rebuild and `adb install -r` engine (owner approves Samsung `AppVerificationDialog`).
5. **T5 Step 6 & T9 (PackLoader 1.5 GiB benchmark):** implement deterministic 1.5 GiB pack (`xoshiro256**`, T4 ≥ 64 KiB histogram), downloader, `I-shared` / `I-private-verify` / `I-private-blind`, `W1`–`W5`, `:worker` process (for T8), and 100k micro-benchmarks; run Native control (clean up its 1.5 GiB data before P1/P2), then P1 (User 5) & P2 (User 6) → `phase0b/results/t9/t9-benchmark.json` and `t9-summary.md` (root `drop_caches` allowed before cold reads).
6. **T7** crash/recovery (`phase0b/scripts/crash-tests.sh`, `phase0b/results/t7/t7-crash.txt`, **skipping the 2 reboots for now**) and **T8** concurrency (`phase0b/results/t8/t8-concurrency.txt`).
7. **T10** MLBB (needs ≥ 8 GB free + owner approval), **T11** Instagram, **T12** compat kit K1–K9 + patch reorganization, **T13** report (`phase0b/REPORT.md` & `plan.md`), then **final deferred phone-reboot checks** (T6 C22b, T7 step 3, T11 step 5).
8. Record every task's text results under `phase0b/results/<task>/`, add a short "T<n> result" note under that task in `phase-0b-plan.md`, update this file when a task finishes, and commit/push as each deliverable lands.

## Read in this order

1. `HANDOFF.md` (this file)
2. `phase-0b-plan.md` — Sections **2.1, 3.8, 5, 7, 10** matter most
3. `phase0/REPORT.md` — why Phase 0 was NO-GO
4. `phase0b/results/t4/t4-host-internal-supplement.txt`, `phase0b/results/t5/t5-backend.txt`, then `phase0b/results/t6/t6-conformance.txt`
5. For a task, its own results: T0 `phase0b/results/t0/`, T1 `t1/`, T2 `t2/`, T3 `t3/`, T4 `t4/`.
   `plan.md` Section 9 (storage design) and Section 20 (Phase 0 log), only as needed.

## Environment (Windows PC + USB phone)

- **Repo:** `C:\Users\child\PROJECTS\DualApp`, branch `main`, remote `github.com/Horali/DualApp` (private).
- **Phone:** serial `RRCY1032Y6F`, USB. A second device (`0953925324001286`) is sometimes attached — **always pass `-s`**.
- **adb:** `C:/Users/child/AppData/Local/Android/Sdk/platform-tools/adb.exe` (37.0.1).
- **bash:** `C:\Program Files\Git\bin\bash.exe` — call it by full path. **`C:\WINDOWS\system32\bash.exe` is WSL** and silently eats `VAR=value` assignments, so scripts must not run under it. Always `export MSYS_NO_PATHCONV=1` first. Put tricky commands in a `.sh` file and run `bash file.sh`.
- **Do not pipe bash output into `head`/`grep` from PowerShell** — the pipe belongs to PowerShell. Put the filter inside the script.
- **Build-tools:** `C:/Users/child/AppData/Local/Android/Sdk/build-tools/36.0.0/` (`aapt2`, `dexdump`).
- **NDK:** `C:/Users/child/AppData/Local/Android/Sdk/ndk/29.0.13846066/` (arm64 clang at `toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android26-clang`).
- **Gradle:** wrapper in each project; offline cache at `phase0/.gradle-home`; JDK `C:/Program Files/Android/Android Studio/jbr`.

### Build commands

Engine (offline, ~1.5 min):
```bash
cd phase0/engine/NewBlackbox
export JAVA_HOME="/c/Program Files/Android/Android Studio/jbr"
export GRADLE_USER_HOME=/c/Users/child/PROJECTS/DualApp/phase0/.gradle-home
./gradlew --offline :app:assembleDebug
```
APK: `app/build/outputs/apk/debug/BlackBox_4.0.0_arm64-v8a-debug.apk`. Install with `adb install -r` (keeps clone data). Engine package `top.niunaijun.blackbox`, debuggable → `run-as` works.

Canary (v0.3):
```bash
cd phase0/canary
export JAVA_HOME="/c/Program Files/Android/Android Studio/jbr"
export GRADLE_USER_HOME=/c/Users/child/PROJECTS/DualApp/phase0/.gradle-home
./gradlew --offline :app:assembleDebug
```
APK: `app/build/outputs/apk/debug/app-debug.apk`.

PackLoader:
```bash
cd phase0b/apps/packloader
export JAVA_HOME="/c/Program Files/Android/Android Studio/jbr"
export GRADLE_USER_HOME=/c/Users/child/PROJECTS/DualApp/phase0/.gradle-home
./gradlew --offline :app:assembleDebug
```

Tools:
```bash
bash phase0b/tools/dualfs-tool/build.sh
bash phase0b/tools/dfaudit/build.sh
```

### Phone helpers

- Engine external root: `/storage/emulated/0/Android/data/top.niunaijun.blackbox/files/blackbox`.
- MLBB clone tree: that root + `/storage/emulated/0/Android/data/com.mobile.legends`.
- Host MLBB tree: `/storage/emulated/0/Android/data/com.mobile.legends/files/dragon2017` (readable as `shell`, **not** via `run-as`).
- `du`/`ls` via `run-as top.niunaijun.blackbox` for engine-owned paths; via `adb shell` for the host MLBB tree.
- Tools are pushed to `/data/local/tmp/`, copied to `/data/user/0/top.niunaijun.blackbox/files/` and `chmod 700` (external storage is `noexec`).
- `dualfs-tool` config keys: `pool`, `root`, `tmp`, `locks`, `pkg`, `user`, `minbytes`, `include=`, `exclude=`. (`libdualfs` runtime config also supports multiple `root=` lines and `tmp_int=` for internal f2fs copy-up.)
- **Filter adb output before reading it.** `dumpsys`, logcat and `ps` are huge. Never dump raw `dumpsys window` (it once produced 40k lines).
- **`uiautomator dump` fails with "null root node"** on the engine's guest window and sometimes on normal apps. Use screenshots and coordinate taps; engine UI pages are at (180,518) Canary and (540,518) MLBB on User 0. Canary v0.3 has **E** (env page), **T** (T3 probe), **X** (escape test), and **C** (runs T6 probe command from `<extFilesDir>/t6_cmd.txt`) shortcuts.
- **Termux `DirtyFrag` root daemon (when active):** `rm -f /sdcard/cmd.out`, write the script to `/sdcard/cmd.tmp`, then atomically `mv /sdcard/cmd.tmp /sdcard/cmd.sh` (writing directly to `cmd.sh` can race the polling daemon), and poll until `/sdcard/cmd.out` ends with `__DONE__`. For multi-line scripts from Windows, base64-encode locally and decode with `echo <b64> | base64 -d > /sdcard/cmd.tmp && mv /sdcard/cmd.tmp /sdcard/cmd.sh`. Never touch or create files inside engine/guest directories as root.

## Rules (from the owner; follow exactly)

- **The phone:** ask before anything that changes it — installing, uninstalling, launching a guest app, changing settings, deleting data, or rebooting. Read-only adb is fine (`pull`, `dumpsys`, `getprop`, logcat).
- **Non-root product requirement:** the Termux `DirtyFrag` root daemon is **strictly for read-only recon and `drop_caches`**. DualApp, NewBlackbox, `dualfs-tool`, and `dfaudit` must work **100% non-root** (`run-as top.niunaijun.blackbox`, UID `10057`). Never create root-owned (`uid=0`) files in engine or guest paths.
- **Defer phone reboots to the end:** skip all phone reboot checks (T6 C22b, T7 step 3, T11 step 5) until the final step so Termux `DirtyFrag` root stays alive during the remaining research and benchmarks.
- **Never uninstall NewBlackbox:** that deletes every clone and the pool.
- **Never uninstall or clear data of the real MLBB / Instagram / Secure Folder.** Read only.
- **Instagram login** only with the owner's approval, a throwaway account, and the owner typing the password.
- **Engine source:** never hand-edit it without saving the change as a numbered patch in `phase0/engine/patches/`. 0002/0003 are DualFs; compat fixes are `00NN-compat-*`. A patch must apply cleanly to a fresh `89b5983`; keep the engine's CRLF line endings.
- **Spoofing / anti-detection:** keep it as shipped. Label every result "upstream engine, spoofing on".
- **Commits:** Conventional Commits (`type(scope): summary`, lowercase, imperative, ~72 chars). Small commits, push to `main` often. GitHub account **Horali** (`gh auth switch -h github.com -u Horali`). **No AI attribution** of any kind.
- **Never commit:** APKs, pulled phone jars/dex, screenshots of the phone, `.env`, build outputs, large generated data. `.gitignore` covers `phase0b/work/`, `**/build/`, `**/.cxx/`, `*.apk`, the tool binaries.
- **Explaining:** plain, simple English; the owner is learning. Show results (tables, screenshots), not just descriptions. Say honestly when an idea is weak.

## Key risks to keep in mind

1. **`:black` freezer kill** (T2): the engine's service process is frozen and killed on a sync binder call; a guest `startActivity` can crash/ANR. Not fixed by the 0002/0003 update; needs T12/T13 work. It may explain MLBB fragility.
2. **Identity / ban risk** (T2/T4): all clones share `ANDROID_ID`; spoofing is inconsistent and mostly fake; the two-account use case needs Phase 1 identity work.
3. **Value is still unproven** (T10): does a fresh MLBB clone actually skip seeded downloads? T4 shows 99.79% identical bytes and gives the download order, but only T10 proves L2.
4. **Hook surface & pending verification** (T5/T6/T7–T9): `__openat` and the 15-hook CoW/guard surface passed C1–C21 + force-stop C22 (`FAILS=0`), while raw-syscall cases (C18/C19) are a designed fail-private (not protection). Crash recovery (T7), multi-process concurrency + `instance.lock` (T8), and performance (T9) remain to be measured.
5. **Performance** (T9): not yet measured with DualFs on.
6. **Disk** (now): 6.8 GB free; T10 needs ≥ 8 GB.
