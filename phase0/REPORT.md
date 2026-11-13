# DualApp Phase 0 report

Date: 2026-11-13. Device: Samsung Galaxy S25+ (SM-S936B), Android 17, One UI 9.
Every result below was measured on that phone over ADB, not on an emulator.
Raw data is in `phase0/results/`; the day-by-day log is in `plan.md` Section 20.1.

**Bottom line: NO-GO for Phase 1 as currently planned.** Running separate copies of an app works, and even MLBB runs. But the shared-storage design needs a part that doesn't exist yet: an engine hook that copies a shared file before anyone writes to it. And the only engine tested is fragile on Android 17. Section 10 lists a short follow-up (Phase 0b) that would settle both.

Labels used: **Tested** = measured on the phone. **Partial** = measured but not conclusive. **Untested** = not done.

---

## 1. Device specifications (Tested)

| Item | Value |
|---|---|
| Model / SoC | SM-S936B (`pa2q`), Snapdragon SM8750 |
| Android / API / One UI | 17 / API 37 / One UI 9.0 |
| Firmware build / security patch | `S936BXXUDDZJB` (`CP2A.260605.016`) / 2026-10-05 |
| ART module | `com.google.android.art` 372042720 |
| Kernel / page size | 6.6.127-android15-8 (aarch64) / 4 KB |
| ABIs | `arm64-v8a` only (no 32-bit) |
| RAM / storage | 11.4 GB / 219 GB, **only 16-19 GB free during the tests** |
| Users | Owner (0) and Secure Folder (150, a private profile). Max switchable users: 1. |
| SELinux | Enforcing |
| MLBB / Instagram | 2.2.23 (targetSdk 36) / 455.0 (targetSdk 37), both arm64 |

## 2. Engine results: NewBlackbox (Tested)

- **Source:** official repo, commit `89b5983`. It builds cleanly on the PC.
- **Built-in spoofing (audit):** the engine has spoofing and anti-detection code that is always on. At your decision it was kept, so **every result here is "upstream engine, spoofing on"**.
- **Play Protect:** it **rejected the install as harmful**. You turned Play Protect off yourself to install it.
- **Unmodified on Android 17:** it runs simple apps, but **crashes any app that binds a background service**. Android 17 changed a hidden callback (`IServiceConnection.connected` gained an `IBinderSession` argument) that the engine doesn't implement. MLBB crash-looped about once a second.
- **Patch 0001** (24 lines, compatibility only, `phase0/engine/patches/`) fixed that crash. After it, MLBB ran with zero crashes.
- **Other observations:**
  - The engine reuses the installed APK, so a clone costs almost no space for code.
  - It renames guest processes to the guest app's name.
  - Guests run in the `untrusted_app_27` sandbox.
  - The **+** button sometimes fails to open the app list.
- **Can one person maintain it?** Weak:
  - Single maintainer.
  - The hook library (Dobby) ships as a closed binary.
  - Anti-detection is woven into the core.
  - It targets SDK 28.
  - Android 17 has already broken it once.

  Expect a new compatibility patch with each Android release.

## 3. Isolation results (Tested, pass)

The Canary app checks four storage types: preferences, SQLite database, internal file and external file. I ran it in User 0, User 1 and as a normal app.

| Check | Result |
|---|---|
| Each copy gets its own ID and data | Pass (`74d0626c`, `9180c35e`, normal app `1d163fe9`) |
| No copy sees another's data | Pass |
| Survives an engine force-stop | Pass |
| Survives a phone reboot | Pass |
| Normal app unaffected by the clones | Pass |

**Caveat:** all guest data lives inside the engine's folder. Uninstalling NewBlackbox deletes every copy.

## 4. Shared storage and copy-on-write (Tested)

I ran a shared pool of files addressed by SHA-256 hash, linked into each copy with symlinks. The tests ran both from a shell with the engine's user ID and from inside real guest apps.

| Test | Result |
|---|---|
| Read shared file through a symlink, both copies | Pass |
| Plain write to a read-only (`0444`) file | Denied |
| **Guest `chmod`s it, then writes** | **Allowed, from inside real guests in both users. The shared file was corrupted for every copy.** |
| Delete from a read-only (`0555`) pool folder | Denied, but `chmod` gets around it too |
| Hard links, `chattr +i`, fs-verity | All denied (no immutability available) |
| Copy-on-write done by Android itself | None. The engine must copy the file first (64 MB in 0.1 s). |
| Readers during a copy-up | Consistent |
| `kill -9` during a copy-up | Symlink intact, partial temp file left behind (needs a cleanup journal) |
| Hash scrub detects corruption | Works |

**Conclusion:** the plan's assumption that "`0444` protects shared files" is **false**. Every guest shares one user ID, so read-only modes protect nothing. Safe sharing needs the engine to intercept `open`-for-write, `chmod`, `truncate`, `rename` and `unlink` and copy the file before the change (mechanism M2), plus hash scrubbing. **None of that exists yet.**

## 5. L1 and L2 measurements

| Item | Status | Result |
|---|---|---|
| L1, synthetic | **Tested** | 2 copies: 64 MB shared vs 128 MB private (50% saved) |
| L1, MLBB | **Tested (precondition)** | 300 of 300 sampled files were byte-identical to the normal app's, so MLBB assets are dedupable. Each extra MLBB copy would otherwise store about **12 GB** (`files/dragon2017`). |
| L2, MLBB seeded | **Partial** | Seeded 1.04 GB of the normal app's `img/` into the clone. The game kept all 47,069 seeded files and downloaded 318 MB of others, but never reached `img/` in 4 minutes. **Not proven that it skips seeded files.** |
| L2, index mode A vs B | **Untested** | MLBB's index is private and binary, so the comparison can't be made with it. Needs the synthetic pack test (P5). |
| Network for a fresh MLBB copy | **Tested** | About 1.2-1.9 MB/s in the background on the Create Character screen. Full download ~12 GB, roughly 2-3 h. |

## 6. MLBB and Instagram findings

**MLBB:**

| Item | Status |
|---|---|
| Runs in the container | **Tested:** reaches Create Character with patch 0001 |
| Anti-cheat refusal or warning before login | **Tested:** none seen, but **spoofing was on**, so this says nothing about an honest container |
| Login, matchmaking, a real match, bans | **Untested** (no login, per your rules; no throwaway account used) |
| Frame rate in a match | **Untested** |

**Instagram:**

| Item | Status |
|---|---|
| Package metadata | **Tested:** version 455.0, targetSdk 37 |
| Runs in the container, login persists, push | **Untested** |

## 7. Performance (Tested for Canary only)

| | Normal app | In NewBlackbox |
|---|---|---|
| Cold start | 112 ms (median) | 94 ms (engine UI already open) |
| Memory (PSS) | 71 MB | 96 MB guest (+35%), +39 MB engine service, +145 MB engine UI when open |

MLBB load time and frame pacing are **untested**.

## 8. Remaining risks

1. **Engine fragility.** NewBlackbox already broke once on Android 17, and every Android update can break it again. Play Protect flags it as harmful.
2. **Write protection isn't built.** Without an M2 hook layer, shared storage can be silently corrupted by any guest. This is the core of the project and the largest unbuilt piece.
3. **Spoofing conflict.** The engine's always-on spoofing contradicts the plan's transparency principle. Results with it on can't show how MLBB or Instagram treat an honest container.
4. **Account and ban risk.** MLBB login inside a modified container is untested, and the terms-of-service risk remains.
5. **Disk space.** 16-19 GB free is too little to test (or use) a second full MLBB copy without deduplication.
6. **Single point of loss.** Uninstalling the engine deletes all clone data.

## 9. Gates G1-G5

| Gate | Question | Verdict |
|---|---|---|
| **G1** | Does an engine run Canary and a real app on this phone? | **Pass, with patch 0001.** The unmodified engine fails for apps that use services. |
| **G2** | Do symlink copy-on-write and redirection pass the storage suite? | **Fail as designed.** Symlinks and redirection work, but the kernel gives no copy-on-write and `0444` is bypassable. The fallback path is M2 (copy-up inside the engine's hooks), which is **not built or tested**. |
| **G3** | Does MLBB run without refusal or warning? | **Partial.** It runs to the character screen with no refusal, but only with spoofing on and without login. |
| **G4** | Second MLBB copy: L1 and L2 measured? | **Partial.** L1 is viable (identical bytes, ~12 GB per copy). L2 is unproven. |
| **G5** | Does an Instagram login persist in the container? | **Untested.** |

## 10. GO / NO-GO

**NO-GO for Phase 1 today.** The deciding failure is **G2**: shared storage is only safe with an engine-level copy-up layer, and that layer is the riskiest part of the project, still unbuilt. G1 passing depends on a patched, spoofing-heavy, single-maintainer engine that Android 17 has already broken.

**Recommended Phase 0b** (small, before any Phase 1 decision):

1. **Engine decision.** Either keep NewBlackbox with a maintained patch set (and decide about its spoofing), or evaluate another engine.
2. **M2 prototype.** Hook `open`-for-write, `chmod`, `truncate`, `rename` and `unlink` in the engine and copy-up the file first. Re-run the storage suite until the `chmod` attack fails.
3. **Synthetic L2 test (P5)** with a 1-2 GB pack, in both index modes.
4. **Instagram G5** with a throwaway account, if you approve.
5. **Optional: long MLBB L2 run** once there is about 15 GB free beyond the clone's needs.

If items 1 and 2 succeed, Phase 1 becomes a realistic GO. If M2 can't be made reliable, the plan's fallback applies: separate copies with **no** shared storage, or a work-profile backend.

## State left on the phone

- **NewBlackbox (with patch 0001):** installed and stopped. All Files Access granted.
- **Clones:** Canary in Users 0 and 1, and MLBB in User 0 with about 2.3 GB of data inside the engine's folders.
- **DualApp Canary:** installed as a normal app.
- **Clean-ups done:** the test pool was removed after each storage test. Your normal MLBB, Instagram and Secure Folder were only read, never changed.
- **Settings for you to check:**
  - Play Protect scanning: was off for the install; turn it back on if it still is.
  - Stay awake: on.
  - Screen timeout: 30 s.
