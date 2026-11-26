# DualApp handoff

Read this first. It is the whole state of the project. The full previous handoff is archived unchanged in docs/history.md. If a chat message contradicts this file, this file wins.

## What the project is

DualApp is a no-root Android multi-instance app manager: it runs clones of one app on one phone. Target phone: Samsung Galaxy S25+ (SM-S936B), Android 17 / API 37. Target apps: MLBB (com.mobile.legends) and Instagram (com.instagram.android).

The key goal: MLBB clones share its ~12 GB of game files instead of each downloading its own copy. The design is M8 (placeholder overlay): private sparse placeholders, a native hook, and copy-on-write. The engine is NewBlackbox (upstream commit 89b5983, cloned by phase0/engine/fetch-newblackbox.sh; our patches live in phase0/engine/patches/).

## Current focus

Phase 0b, tasks T0–T13 in phase-0b-plan.md. T0–T6 are done (T6 except its reboot half). T7–T13 are not started. T5 has two items left: PackLoader workloads and the M9 spike. Phase 0 was NO-GO (see phase0/REPORT.md).

## Active tasks

None yet. Each task gets a file handoffs/<task-slug>.md; add its line here. The next steps are under "User to-dos".

## Open problems

1. `:black` freezer kill (T2): the engine's service process is frozen and killed on a sync binder call; a guest startActivity can crash/ANR. Needs T12/T13.
2. Identity / ban risk (T2/T4): all clones share one ANDROID_ID; spoofing is inconsistent. Needs Phase 1.
3. Value still unproven (T10): only T10 proves a fresh MLBB clone skips the seeded downloads.
4. Hook surface passed C1–C21 + force-stop C22 (FAILS=0), but T7 crash recovery, T8 concurrency + instance.lock, and T9 performance are unmeasured.
5. Disk: 6.8 GB free on /data; T10 needs ≥ 8 GB.

## User to-dos (next, in order — no phone reboots until the final step)

1. Ask the owner before the next phone-mutating batch: clean ~192 MB T4/T6 scratch, install PoolHost + Canary v0.3 + PackLoader + the instance.lock engine update, and run M9 / T9 / T7 / T8.
2. Add hard excludes (*Cdid*.xml, com.dvi.internal.xml) to mlbb-sharing-profile.json and dualfs-tool.c.
3. T5 Step 9 (M9 spike): build phase0b/apps/poolhost/ + the Canary M9 probe → phase0b/results/t5/t5-m9-spike.txt.
4. Wire instance.lock and P9 scrub placeholder removal in 0003-dualfs-native.patch and dualfs-tool.c; verify all 5 patches apply clean to 89b5983; adb install -r the engine.
5. T5 Step 6 + T9 (PackLoader 1.5 GiB benchmark) → phase0b/results/t9/.
6. T7 crash/recovery and T8 concurrency.
7. T10 MLBB, T11 Instagram, T12 compat kit, T13 report, then the final deferred phone-reboot checks (T6 C22b, T7 step 3, T11 step 5).

## Decisions and lessons

- Ask the owner before anything that changes the phone (install, uninstall, launch, settings, delete, reboot). Read-only adb is fine.
- Non-root product: DualApp, the engine, dualfs-tool, and dfaudit must work 100% non-root (run-as top.niunaijun.blackbox, UID 10057). The root daemon is for read-only recon and drop_caches only.
- Defer phone reboots to the very end so the Termux DirtyFrag root daemon stays alive.
- Never uninstall NewBlackbox (deletes every clone and the pool). Never touch the real MLBB / Instagram / Secure Folder.
- Engine source: never hand-edit; save every change as a numbered patch in phase0/engine/patches/. A patch must apply cleanly to a fresh 89b5983. Keep CRLF line endings.
- Commits: Conventional Commits, small, push to main often, GitHub account Horali, no AI attribution.
- Label every result "upstream engine, spoofing on".

## Where to look

- Task results: phase0b/results/t0/ … t6/ (T4 host-internal supplement, t5-backend.txt, t6-conformance.txt).
- Plan: phase-0b-plan.md (Sections 2.1, 3.8, 5, 7, 10); plan.md Section 9 (storage design).
- Why Phase 0 was NO-GO: phase0/REPORT.md.
- Full environment, build commands, phone helpers, and phone state: docs/history.md.
- Repo: C:\Users\child\PROJECTS\DualApp, branch main, remote github.com/Horali/DualApp (private).
- Phone: serial RRCY1032Y6F over USB. Always pass -s RRCY1032Y6F (a second device 0953925324001286 may be attached).
