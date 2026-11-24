# Starting prompt for a new harness session

Copy everything between the lines into the new session as the first message. It assumes no memory of the old chat; `HANDOFF.md` in the repo is the source of truth.

---

You are continuing **DualApp**, a no-root Android multi-instance (app cloning) project.

Repo: `C:\Users\child\PROJECTS\DualApp` (branch `main`, remote `github.com/Horali/DualApp`, private).
Phone: Samsung SM-S936B, Android 17 / API 37, USB serial `RRCY1032Y6F`. A second device (`0953925324001286`) is sometimes on the bus — always pass `adb -s RRCY1032Y6F`.
Global instructions: follow `C:\Users\child\AGENTS.md` (address the owner as Queen Hora when it fits, plain English, show tables, no AI attribution in commits).

**Before doing anything, read these files in this order:**

1. `HANDOFF.md` — the full state, rules, environment, build commands and what is next. This is the source of truth; do not assume anything from an old chat. If a message contradicts it, verify against the repo and the phone first.
2. `phase-0b-plan.md` — the phase plan. Sections **2.1, 3.8, 5, 7, 10** matter most.
3. `phase0/REPORT.md` — why Phase 0 ended NO-GO.
4. `phase0b/results/t4/t4-host-internal-supplement.txt`, `phase0b/results/t5/t5-backend.txt`, and `phase0b/results/t6/t6-conformance.txt` — the latest finished work.

**Where the project stands (read `HANDOFF.md` for detail):** Phase 0b has 14 tasks, T0–T13. T0–T4 are DONE, T6 is PASS for C1–C21 + force-stop C22 (`FAILS=0`, upstream engine, spoofing on), and T5 has hook spike + patches `0002`/`0003` + `dualfs-tool` + `dfaudit` + R7 + Canary v0.3 done. Patches `0001+0004+0005+0002+0003` are installed on the phone (`6.8 GB` free on `/data`; `0953925324001286` on USB is an unrelated Infinix phone — always pass `-s RRCY1032Y6F`). Remaining work: T5 M9 PoolHost spike, pre-T7/T8 code gaps (`instance.lock` in `DualFs.cpp` + `dualfs-tool.c`, P9 `scrub` placeholder removal, `*Cdid*.xml` / `com.dvi.internal.xml` hard excludes), T5 PackLoader workloads + T9 1.5 GiB benchmark, T7 (without reboots), T8, T10–T13, and then the deferred phone-reboot checks (T6 C22b, T7 step 3, T11 step 5) at the very end.

**Your first action after reading:** summarize in a few lines what you understood, note that `/data` has `6.8 GB` free (T9 fits if Native control's 1.5 GiB data is cleaned before P1/P2; T10 needs ≥ 8 GB), and ask the owner for approval to start the **non-reboot** batch in order: (a) clean ~192 MB T4/T6 scratch + add hard excludes, (b) T5 M9 PoolHost spike, (c) wire `instance.lock` + P9 `scrub` placeholder removal and install the engine update, (d) T5 PackLoader workloads + T9 1.5 GiB benchmark, followed by T7 (no reboot) and T8. Do not install, uninstall, launch a guest app, change settings, delete data, or reboot before the owner says yes. Read-only adb (and read-only root recon via `/sdcard/cmd.sh`) is fine. The phone may be locked; ask the owner to unlock it if a task needs input.

**Rules (from the owner; follow exactly):**

- Ask before anything that changes the phone: installing, uninstalling, launching a guest app, changing settings, deleting data, or rebooting. Read-only adb (pull, dumpsys, getprop, logcat) is fine. Never uninstall NewBlackbox — that deletes every clone. Never uninstall or clear the real MLBB / Instagram / Secure Folder.
- **Defer all phone reboots until the very last step:** the owner has an active Termux `DirtyFrag` root daemon (`rm -f /sdcard/cmd.out`, write `/sdcard/cmd.tmp`, atomically `mv /sdcard/cmd.tmp /sdcard/cmd.sh`, wait for `__DONE__` in `/sdcard/cmd.out`) that stops on reboot. Use it **only** for read-only inspection or `drop_caches` before cold-read benchmarks — DualApp, the engine, `dualfs-tool`, and `dfaudit` must stay **100% non-root** (`run-as top.niunaijun.blackbox`, UID `10057`), and never write root-owned files into engine/guest folders.
- Use **Git Bash** for adb: `C:\Program Files\Git\bin\bash.exe`, called by full path, with `export MSYS_NO_PATHCONV=1` first. `C:\WINDOWS\system32\bash.exe` is WSL and silently eats `VAR=value` assignments — never use it. Put tricky commands in `.sh` files; PowerShell mangles quotes, and a pipe written in PowerShell stays in PowerShell.
- Filter adb output (grep/head/tail or a script) before reading it. Never dump raw `dumpsys` or logcat.
- Never hand-edit the engine without saving the change as a numbered patch in `phase0/engine/patches/`. Patches must apply cleanly to a fresh `89b5983`; keep CRLF line endings.
- Keep the engine's spoofing/anti-detection code as shipped; label every result "upstream engine, spoofing on".
- Commits: Conventional Commits, small, push to `main` often, GitHub account `Horali` (`gh auth switch -h github.com -u Horali`), **no AI attribution of any kind**.
- Never commit APKs, pulled phone binaries, screenshots, build outputs or large generated data.
- Explain in plain, simple English; show results in tables; say honestly when an idea is weak.

**How to work:**

- One task at a time, exactly as the plan's procedure says. The Section 7 pass/fail criteria are fixed; do not reinterpret them.
- Save text results under `phase0b/results/<task>/`. Add a short "T<n> result" note under that task in `phase-0b-plan.md`. Update `HANDOFF.md` when a task finishes.
- Build commands, device paths, Wi-Fi ADB (`192.168.1.17:5555` if reconnected), root daemon safety rules, and tool usage are in `HANDOFF.md`.

**What done looks like:** finish T5 (M9 spike + PackLoader workloads), wire pre-T7/T8 `instance.lock` / `scrub` / hard excludes, run T9, T7 (without reboots), T8, T10–T13, and finally run the deferred phone-reboot checks at the very end, recording results and committing as each deliverable lands.

---
