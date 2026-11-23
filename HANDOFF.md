# DualApp handoff (2026-11-23)

Read this first. It is the whole state of the project: what it is, what is done, what is next, the rules, and how to build/run everything. The next session should not need the old chat.

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
- **T0–T4 are DONE. T5 is IN PROGRESS.** T6–T13 are not started.

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
- Installed engine: **0001 + 0004 + 0005**, versionCode 400, targetSdk 28, UID 10057, updated 2026-11-14 02:26:31.
- Engine users: User 0 = MLBB + Canary; User 1 = Canary. Engine data ≈ 2.19 GiB (154 MiB internal + 2.04 GiB external at that time).
- Per-UID network counter: **`dumpsys netstats detail`** works (828 UID entries, `uid=<n> ... rb/tb`). `dumpsys connectivity trafficcontroller` has **no byte counters** on this firmware.

### T2 — engine runtime audit (DONE, 2026-11-15)

`phase0b/results/t2/t2-runtime-audit.txt`

- **VirtualSpoof is active** (`__system_property_get` hook installed; `[spoof] ro.hardware = qcom`, etc.).
- **AntiDetection, FileSystemHook and ResourceLoading log "installed" but install nothing** (source confirms no `DobbyHook`).
- **HiddenAPI exemption fails on Android 17**: `setHiddenApiExemptions` is no longer an exported libart symbol. No crash seen.
- **UnixFileSystem hooks 8 of 9**: `canonicalize0(String)` is now a non-native wrapper; the native form is `canonicalize0(String,Z)`. `File.getCanonicalPath()` is not redirected.
- **GmsProxy inactive** ("Failed to get gms service binder"). **AntiVirtualDetect proxy never registered** (only its provider-skip helper is used).
- Property readers: Java `Build`, modern native `__system_property_find`/`read_callback`, and `getprop` children all see the **real Samsung** values. Only the old native API sees **Pixel 6 / Android 12**, and only in the clones.
- **`ANDROID_ID` is identical in the normal app, User 0 and User 1** (hash `2746e481e11f`): all clones share one ID (N6 identity link).
- **Native hard-coded `/sdcard` escape**: `/data/data/<other app>` is denied, but `/sdcard/Download/...` write succeeds (engine has All Files Access).
- **New engine bug: `:black` freezer kill.** Android's app freezer freezes the engine's `:black` process and the system kills it on the first sync binder call (`Sync transaction while frozen`). A guest `startActivity` then fails with `SecurityException` and an ANR. Needs a T12/T13 fix (foreground service / binding / freezer exemption). This is why the T2 env page test crashed the first time.
- BTI/PAC present in libc; Dobby hook still installed.

### T3 — filesystem probe (DONE, 2026-11-15)

`phase0b/results/t3/t3-fs-probe.txt`

- **Tag storage: xattr** (`user.dualapp.blob`). Works on the external path and from a guest; survives rename.
- **Pool must be external**: L-ext/L-guest are FUSE (`st_dev=182`), internal is f2fs (`st_dev=65099`). Raw `rename(2)` across them is `EXDEV`.
- **No need to move MLBB's external data to internal**: sparse files, xattrs, inotify and `copy_file_range` all work on external.
- Sparse placeholders: a bare 64 MiB sparse file has `st_blocks=0`; **a real placeholder created by the tool has `st_blocks=8` (4 KiB inode block)** — still ~4 KiB per file (T3 budget ≤ 8 KiB). The placeholder test uses `st_blocks*512 < size/2`, not `st_blocks==0`.
- 10,000 placeholders cost **40,424 KiB ≈ 4.04 KiB each**.
- Symlinks are **denied** on the external path, so M1 (symlink farm) is impossible there; M8 does not use symlinks.
- `fchmod` on a read-only handle and reopen through `/proc/self/fd` are **both allowed** → the M8 guards are required.
- inotify events work on L-ext and in the guest; the watch limits are not readable (SELinux), so T4 must count folders.
- `copy_file_range`: 64 MiB in 83 ms (**767 MB/s**).
- **`io_uring_setup` is blocked fatally**: seccomp kills the process with `SIGSYS` (syscall 425), not `EPERM`.
- `RLIMIT_NOFILE`: 32768 soft / 524288 hard, same in guest and `run-as`.

### T4 — MLBB inventory (DONE, 2026-11-15)

`phase0b/results/t4/` (summary `t4-mlbb-inventory.txt`, classification `t4-classification.md`, profile `mlbb-sharing-profile.json`, order `t4-download-order-A.txt`)

- Host tree: **161,316 files / 175 folders / 11.77 GiB**. `assets/Art` 8.36 GiB (69%), `Audio` 1.10 GiB, `img` 1.01 GiB, `UI` 768 MiB, `Document` 164 MiB, `comlibs` 136 MiB.
- Content types ≥ 64 KiB: UnityFS 15,565, other 4,441, **ELF 12** (all `comlibs`), dex 0, zip 0.
- Clone A: 58,281 files / **1,958.2 MiB**; `img/` copy 47,069 files / 906.7 MiB.
- Overlap (img excluded, full SHA-256): 10,954 files in both, **10,931 identical (99.79%)**, **23 different** (metadata, login, newbie, 3 `DecompressNative/*.meta`). This confirms F9 at full scale.
- 4-minute run (no login): **8 files changed in place** (metadata/login), **8,042 new files (~462 MiB) downloaded**, 0 deleted; network ~480 MiB. Order: **UI → Art → Audio → Video**.
- **N1 confirmed**: `__SharedPreference__.xml` (`moba_loading_bg`, `__Extracted_PersistentFilesPath__`), PlayerPrefs `RWPathRec`, `DeviceSettings.xml`, and `android/base_loadres_version_info` all cache **clone A's own absolute root**. `IphonePath` and `__SDKOverrideAssetsPath__` were absent.
- **N2/N6**: `login_data/` is external and instance-specific (sizes differ); identity files found are internal only (`files/__app_uuid__`, `snssdk_openudid.xml`, `DeviceSettings.xml`). `.DNA_`, `.DNA_FLAG`, `.gp_uuid`, `INSTALLATION`, `AdjustAttribution`, `Cdid.xml` not found. `_finish` is absent on A.
- **N3**: APK payloads are **misspelled** `assets/Resouces4-1..4.dat` (20.5–29.2 MB) and `lib/arm64-v8a/libResouces_z*.so` (12 ELF files). No file with those names is in either tree; the output is renamed/repacked into identical, non-ELF bundles (shareable). The ELF payloads are private.
- Draft profile: include `files/dragon2017/assets/**`, hard-exclude login data, identity files, metadata, `comlibs`, `LuaServerUpdate`, caches, ELF/dex/zip; `minFileBytes` 65536.

### T5 — M8 prototype (IN PROGRESS, 2026-11-15)

`phase0b/results/t5/t5-backend.txt`

**Done:**

1. **Hook-technique spike passed** (`phase0b/apps/packloader`, PackLoader v0.1):
   - Dobby installed hooks on `open`, `openat`, `__openat`, `fchmod`, `fopen`, `opendir`: **all OK, 0 crashes**.
   - 1M opens: 3,071 ns/call baseline → 3,111 ns/call hooked = **+40 ns/call** (budget 1 µs).
   - `__openat` caught **1,011,014** opens; `open` alone only ~10k. **`__openat` is the primary hook target.**
   - Technique A (Dobby) selected. Technique B (ByteHook) not tested.
2. **Patch 0002** (`0002-dualfs-init.patch`): `NativeCore.enableDualFs`, call in `BActivityThread.handleBindApplication` after `IOCore.enableRedirect`, only if `<external files>/blackbox/dualfs/inst/<user>/<pkg>/config` exists; JNI registration in `BoxCore.cpp`; sources in `Android.mk`.
3. **Patch 0003** (`0003-dualfs-native.patch`): `Bcore/src/main/cpp/dualfs/` — `DualFs.h/.cpp` (config, counters, start-up), `Placeholder.cpp` (xattr tag, placeholder test), `CopyUp.cpp` (striped flock, temp, copy_file_range/fallback, fsync, futimens, rename, dir fsync), `HookBackend.cpp` (hooks `open`, `openat`, `__openat`, `fchmod`, `truncate`, `chmod`; blob handle table; blob read redirect; O_TRUNC; copy-up).
   - The engine **builds cleanly**, and **0001+0004+0005+0002+0003 apply cleanly to a fresh `89b5983`**.
4. **`dualfs-tool`** (`phase0b/tools/dualfs-tool/`, built with `build.sh`):
   - Commands: `harvest`, `seed <manifest>`, `rehydrate`, `verify-tree`, `scrub`, `stats`, `cleanup`, `recover`.
   - Self-test on 101 × 64 KiB files: **101/101 harvest**, **101/101 verify**, **101/101 rehydrate**; tree **6,472 → 412 KiB** and back to 6,472 KiB. Hard-excludes `login_data/` and identity files; journal at `pool/journal/harvest.log`.
   - Found the 4 KiB inode block cost; placeholder test updated.
5. **`dfaudit`** (`phase0b/tools/dfaudit/`, built with `build.sh`):
   - Inotify watcher; prints `<time> <masks> <path> <placeholder|private>`.
   - Reading a placeholder shows `OPEN` + `ACCESS` with `placeholder` (the bypass signal); private files show `CREATE`/`OPEN`/`MODIFY`/`CLOSE_WRITE` with `private`.

**Done (2026-11-23):**

- **Engine update installed (`0001 + 0004 + 0005 + 0002 + 0003`) & R7 PASS** (`phase0b/results/t5/t5-r7.txt`, **upstream engine, spoofing on**):
  - APK SHA-256 `f3079e6a2cd4eae217c97c0b0c59dd68ebfec0308cca9187701d216083767fc3` (`adb install -r`, data intact: 154M internal / 2.5G external, uid 10057).
  - **Install note:** Play Protect returned `VERIFICATION_ALLOW`, while **Samsung Device security / App Protection** (`com.samsung.android.sm.devicesecurity` / `lool` `AppVerificationDialog`) separately prompts and must be approved by the owner on every engine install.
  - With no DualFs config, Canary reported `consistent=true` in User 0 (`74d0626c`, run 21) and User 1 (`9180c35e`, run 6), MLBB clone A reached Create Character (~35 s), and DualFs stayed dormant (0 crashes, 0 DualFs lines; 1 known `:black` freezer kill recovered).

**Pending:**

- Remaining T5 work: **Canary v0.3 attack matrix** (T6 cases C1–C22), **PackLoader workloads** (T9), **M9 spike** (PoolHost), then T6–T13.

**Phone state on 2026-11-23:**

- Engine on the phone is now **0001 + 0004 + 0005 + 0002 + 0003** (DualFs built-in, dormant with no config).
- Free space on `/data`: **7.1 GB** (7.3 GB pre-install) — above the 4 GB stop line, **below the ≥ 8 GB T10 needs** (must free space before T10).
- Installed test apps: **Canary v0.3** (normal + clones in engine Users 0/1) and **PackLoader v0.1** (normal app, no data).
- MLBB clone A is in engine **User 0**, ~2.5 GB external blackbox tree. The real MLBB, Instagram and Secure Folder were only read; never changed.
- The phone may be locked. The owner unlocks it when a task needs input.

## What to do next (in order)

1. ~~Engine update & R7~~ — **DONE (2026-11-23, PASS)**.
2. **T5 attack matrix / T6:** add the C1–C22 cases to Canary v0.3 (Java + JNI), seed placeholders into Canary's trees in Users 0/1 (internal and external), run the matrix, record pool hash and User 1 view after every case.
3. **T5 PackLoader workloads / T9:** generate the 1.5 GiB deterministic pack, the simulated downloader, the three index modes, and W1–W5; run the DualFs on/off benchmarks.
4. **T5 M9 spike:** PoolHost app owns a 256 MiB blob; measure `ContentProvider.openFile` vs `SCM_RIGHTS`, `fchmod` → `EPERM`, reopen → `EACCES`.
5. Then **T6–T13** per the plan. Record each task's text results under `phase0b/results/<task>/` and add a short "T<n> result" note under that task in `phase-0b-plan.md`.

## Read in this order

1. `HANDOFF.md` (this file)
2. `phase-0b-plan.md` — Sections **2.1, 3.8, 5, 7, 10** matter most for the current work.
3. `phase0/REPORT.md` — why Phase 0 was NO-GO.
4. `phase0b/results/t5/t5-backend.txt` — the latest work.
5. For a task, its own results and the related plan section:
   - T0 `phase0b/results/t0/`, T1 `t1/`, T2 `t2/`, T3 `t3/`, T4 `t4/`.
   - `plan.md` Section 9 (storage design) and Section 20 (Phase 0 log), only as needed.

## Environment (Windows PC + USB phone)

- **Repo:** `C:\Users\child\PROJECTS\DualApp`, branch `main`, remote `github.com/Horali/DualApp` (private).
- **Phone:** serial `RRCY1032Y6F`, USB, authorized.
- **adb:** `C:/Users/child/AppData/Local/Android/Sdk/platform-tools/adb.exe`.
- **Use Git Bash, not PowerShell** (PowerShell mangles quotes; it silently drops `r` from `tr -d "\r"` and expands `$` inside commands). Put commands in a `.sh` file and run `bash file.sh` when quoting gets tricky. Always `export MSYS_NO_PATHCONV=1` first.
- **Build-tools:** `C:/Users/child/AppData/Local/Android/Sdk/build-tools/36.0.0/` (`aapt2`, `dexdump`).
- **NDK:** `C:/Users/child/AppData/Local/Android/Sdk/ndk/29.0.13846066/` (arm64 clang at `toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android26-clang`).
- **Gradle:** wrapper in each project; offline Gradle cache at `phase0/.gradle-home`; JDK `C:/Program Files/Android/Android Studio/jbr`.

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

- The engine's external root: `/storage/emulated/0/Android/data/top.niunaijun.blackbox/files/blackbox`.
- The MLBB clone tree: that root + `/storage/emulated/0/Android/data/com.mobile.legends`.
- The host MLBB tree: `/storage/emulated/0/Android/data/com.mobile.legends/files/dragon2017` (readable as `shell`, **not** via `run-as`).
- `du`/`ls` via `run-as top.niunaijun.blackbox` for engine-owned paths; via `adb shell` for the host MLBB tree.
- The tools are pushed to `/data/local/tmp/`, then copied to `/data/user/0/top.niunaijun.blackbox/files/` and `chmod 700` (external storage is `noexec`).
- The `dualfs-tool` config keys: `pool`, `root`, `tmp`, `locks`, `pkg`, `user`, `minbytes`, `include=`, `exclude=`.
- **Filter adb output before reading it.** `dumpsys`, logcat and `ps` are huge. Use a script that greps/summarizes; never dump raw `dumpsys window` (it once produced 40k lines).
- **`uiautomator dump` fails with "null root node" on the engine's guest window and sometimes on normal apps.** Use screenshots and coordinate taps; the engine UI pages are at (180,518) Canary cell and (540,518) MLBB cell on User 0. The guest's own buttons may be under the nav bar; Canary v0.3 has **E** (env page), **T** (T3 probe) and **X** (escape test) key shortcuts.

## Rules (from the owner; follow exactly)

- **The phone:** ask the owner before anything that changes it — installing, uninstalling, launching a guest app, changing settings, deleting data. Read-only adb is fine (`pull`, `dumpsys`, `getprop`, logcat).
- **Never uninstall NewBlackbox:** that deletes every clone and the pool.
- **Instagram login** only with the owner's approval, a throwaway account, and the owner typing the password.
- **Engine source:** never hand-edit it without saving the change as a numbered patch in `phase0/engine/patches/`. 0002/0003 are DualFs; compat fixes are `00NN-compat-*`. A patch must apply cleanly to a fresh `89b5983`; keep the engine's CRLF line endings.
- **Spoofing / anti-detection:** keep it as shipped. Label every result "upstream engine, spoofing on".
- **Commits:** Conventional Commits (`type(scope): summary`, lowercase, imperative, ~72 chars). Small commits, push to `main` often. GitHub account **Horali** (`gh auth switch -h github.com -u Horali`). **No AI attribution** of any kind in commits or PRs.
- **Never commit:** APKs, pulled phone jars/dex, screenshots of the phone, `.env`, build outputs, large generated data. `.gitignore` already covers `phase0b/work/`, `**/build/`, `**/.cxx/`, `*.apk`, the tool binaries.
- **Explaining:** plain, simple English; the owner is learning. Show results (tables, screenshots), not just descriptions. Say honestly when an idea is weak.

## Key risks to keep in mind

1. **`:black` freezer kill** (T2): the engine's service process is frozen and killed on a sync binder call; a guest `startActivity` can crash/ANR. Not fixed by the 0002/0003 update; needs T12/T13 work. It may explain MLBB fragility.
2. **Identity / ban risk** (T2/T4): all clones share `ANDROID_ID`; spoofing is inconsistent and mostly fake; the two-account use case needs Phase 1 identity work.
3. **Value is still unproven** (T10): does a fresh MLBB clone actually skip seeded downloads? T4 shows the files are 99.79% identical and gives the download order, but only T10 proves L2.
4. **Hook surface** (T5): the spike proved `__openat` works, but T6/T9 must prove the full CoW surface (write, truncate, chmod, xattr, timestamps, concurrency) and no bypasses.
5. **Performance** (T9): not yet measured with DualFs on.
