# NewBlackbox pre-build audit (Phase 0, S3)

Audited before any build or device use, 2026-11-13.

## Provenance

| Item | Value |
|---|---|
| Repository | https://github.com/ALEX5402/NewBlackbox |
| Commit audited | `89b59836c66f173756a4ae258cf379a957649820` (merge of PR #54, 2026-07-19) |
| History | 58 commits, first commit 2025-08-16. Re-upload of BlackBox, not its full history. |
| Last activity | 2026-07-19 (about four months before this audit) |
| License | Apache-2.0 (LICENSE file present) |
| CI | `.github/workflows/build_and_telegram.yml` (publishes builds to Telegram; not used here) |

## Build configuration

| Item | Value | Note |
|---|---|---|
| Gradle wrapper | 8.14.5 | Used as shipped, no global Gradle |
| Android Gradle Plugin | 8.13.2 | |
| Kotlin | 1.9.23 | |
| compileSdk | 35 | Below the device's API 37 |
| **targetSdk** | **28** | Opts the engine (and every guest it hosts) out of modern behavior changes. Android 17 install and runtime restrictions for very old targets must be checked on the device. |
| minSdk | 21 | |
| NDK | 29.0.13846066 | ndk-build (Android.mk), not CMake |
| Java target | 21 | |
| ABIs | arm64-v8a, armeabi-v7a | |
| 16 KB pages | `-z max-page-size=16384` on the engine library | Good |
| Release signing | Uses the **debug** key | Not suitable for daily use |
| Repositories | google, mavenCentral, **jitpack.io** (also in plugin resolution) | JitPack builds artifacts from arbitrary GitHub repos; weaker supply chain |

## Dependencies of note

| Dependency | Where | Concern |
|---|---|---|
| `com.github.tiann:FreeReflection:3.2.2` | Bcore | Hidden-API restriction bypass. Expected for any engine; unsupported by Google. |
| **Dobby (prebuilt `libdobby.a`)** | Bcore native | Inline-hook library shipped **only as binary**. Cannot be audited or rebuilt from this repo. |
| xDL (source) | Bcore native | Symbol lookup in system libraries. Expected. |
| `toml4j` | Bcore | Config parsing |
| Prebuilt AARs: `StateView`, `catloading`, `floatingview` | app/libs | Unaudited binaries in the demo app |
| `com.gitee.cbfg5210:RVAdapter`, `CornerLabelView`, `SimpleSearchView` | app (via JitPack) | Small third-party UI libraries |
| `osmdroid` | app | Map UI for the fake-location feature |

## Spoofing, anti-detection and bypass code (conflicts with the plan's transparency principle)

**These are active by default in the unmodified engine. None of them can be switched off by configuration.**

| Component | What it does | How it activates |
|---|---|---|
| `Bcore/src/main/cpp/Utils/AntiDetection.cpp` | Hides root (su, Magisk paths), virtualization apps (BlackBox itself, Parallel Space, VirtualApp, DualSpace and others), emulator traces and Xposed from guest apps by hooking file and property lookups | `__attribute__((constructor))`, runs automatically whenever the engine library loads in a guest process |
| `Bcore/src/main/cpp/Utils/VirtualSpoof.cpp` | Hooks `__system_property_get` so guests see a **Google Pixel 6 on Android 12**, with a fake serial number and fake build fingerprint | `__attribute__((constructor))`, automatic |
| `fake/service/AntiVirtualDetectProxy.java` | When a guest detects the sandbox and calls `System.exit()` / `Runtime.exit()`, the engine **blocks the exit** so the app keeps running | Registered by the hook manager |
| `fake/service/DeviceIdProxy.java`, `AndroidIdProxy.java`, `ITelephonyManagerProxy.java` | Replace device identifiers (device ID, ANDROID_ID, telephony identifiers) | Registered in `HookManager` |
| `utils/UIDSpoofingHelper.java`, `utils/StackTraceFilter.java` | Disguise caller UID and filter stack traces | Used by the core |
| `fake/service/ILocationManagerProxy.java`, `BLocationManagerService`, app `view/fake/*` | Fake GPS location per app (demo app UI with a map) | Registered in `HookManager` |

## Consequence for Phase 0

- The owner's instruction is to build the unmodified upstream first **and** not to enable spoofing, fake location or anti-detection.
- Compiling the unmodified code on the PC enables nothing on any device. The compile check is therefore done as instructed.
- **Running the unmodified build on the phone would enable all of the above in every guest process.** It must not be installed on the phone as-is.
- Before any on-device test, these components would have to be removed or disabled in a clearly documented patch. That is an engine modification and needs the owner's decision.

## Owner decision (2026-11-13)

Keep everything as shipped. No patch. On-device results will be labelled "upstream engine, spoofing on". Installing on the phone still needs separate approval.
