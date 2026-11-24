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
4. `phase0b/results/t5/t5-backend.txt` and `phase0b/results/t6/t6-conformance.txt` — the latest finished work.

**Where the project stands (read HANDOFF.md for detail):** Phase 0b has 14 tasks, T0–T13. T0–T6 are done (T6 except its reboot half). The M8 DualFs prototype works: patches `0001+0004+0005+0002+0003` are installed on the phone, R7 passed, and the C1–C21 conformance matrix passed with `FAILS=0` (upstream engine, spoofing on). Remaining work: T6's reboot check, T5's PackLoader workloads and M9 PoolHost spike, then T7–T13.

**Your first action after reading:** summarise in a few lines what you understood, then ask the owner what to start with — offer, in order: (a) finish T6's reboot check (needs approval to reboot; the T6 pool is still on the phone), (b) free `/data` space (6.8 GB now, T10 needs ≥ 8 GB), (c) T5 PackLoader workloads, (d) T5 M9 spike. Do not install, uninstall, launch a guest app, change settings or delete data before the owner says yes. Read-only adb is fine. The phone may be locked; ask the owner to unlock it if a task needs input.

**Rules (from the owner; follow exactly):**

- Ask before anything that changes the phone: installing, uninstalling, launching a guest app, changing settings, deleting data. Read-only adb (pull, dumpsys, getprop, logcat) is fine. Never uninstall NewBlackbox — that deletes every clone. Never uninstall or clear the real MLBB / Instagram / Secure Folder.
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
- Build commands, device paths and tool usage are in `HANDOFF.md`.

**What done looks like:** finish T5 (PackLoader workloads + M9 spike) and T6's reboot check, then T7–T13, recording results and committing as each deliverable lands.

---
