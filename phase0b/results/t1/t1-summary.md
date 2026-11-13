# T1 result: Android 17 framework diff (API 37, One UI 9)

Date: 2026-11-14. Read-only on the phone (only `adb pull` of the 61 BOOTCLASSPATH jars).

- **Phone:** SM-S936B, `samsung/pa2qxxx/pa2q:17/CP2A.260605.016/S936BXXUDDZJB_OXMDDZJB`, API 37.
- **Engine:** NewBlackbox `89b5983` + patch 0001, read from `phase0/engine/NewBlackbox/Bcore/src/main`.
- **Method:** `phase0b/scripts/framework-diff.sh`. It pulls the jars, dumps them with build-tools 36 `dexdump`, and builds a 72,022-class database (`t1_dexdb.py`). Then `t1_analyze.py` compares it with the engine source.

## Verdict

**No new crash source was found.** Every framework interface the engine implements now matches the phone exactly. That includes the 4-argument `IServiceConnection.connected` from patch 0001. So no new `AbstractMethodError` waits on the phone.

T1 did find **two real silent bugs**:

- guest **JobScheduler / WorkManager jobs probably never get scheduled**;
- **new intents are dropped** (the `handleNewIntent` lead, now confirmed on the phone).

It also found a few smaller silent gaps. Nothing in this list stops MLBB from starting, which matches what Phase 0 saw.

| Plan criterion (Section 7) | Expected | Result | Pass? |
|---|---|---|---|
| Unimplemented methods in engine-implemented framework interfaces | 1 known + 0–3 more | **0 left** (4 interfaces checked) | **Pass** |
| Changed parameter lists behind proxies; missing mirror members | a few | 2 proxy mismatches + 4 mirror gaps that matter; the rest are dead code for old Android versions. All explained (below). The two that need code are **not patched yet** (this run was told not to write patches). | **Open** until the patches exist |
| Samsung / One UI additions in watched types | a few, e.g. `handleCoreStatesChanged(Bundle)` | 2,858 phone-only methods, every one classified; **0 crash**, 0 silent, 924 pass-through, 1,934 harmless | **Pass** (none abstract on an engine-implemented type) |
| Engine `handleNewIntent` form matching the phone | none | **none matches** (confirmed) | Recorded; the compatibility patch is still to write (K9) |

## Ranked list: what would need patching

Ranked by risk to our two target apps. "Crash" means an exception that kills the guest. "Silent" means a feature quietly doesn't work.

| # | Risk | Where | What changed on the phone | Effect | Fix idea (not written) |
|---|---|---|---|---|---|
| 1 | **High, silent** | `fake/service/IJobServiceProxy.java` hooks `schedule`, `enqueue`, `cancel` | `IJobScheduler.schedule(String namespace, JobInfo)`, `enqueue(String, JobInfo, JobWorkItem)`, `cancel(String, int)`. The namespace argument (added in Android 14) comes **first**. The engine still expects `JobInfo` / the job id in `args[0]`. | Apps normally pass `namespace = null`. The hook then sees `args[0] == null` and returns `RESULT_FAILURE` **without calling the system**. So every guest job, including WorkManager (which Instagram uses for uploads and sync), silently fails to schedule. A non-null namespace makes the hook put a `JobInfo` where the `String` goes; `invoke` then fails, and that also returns 0. `cancel` casts the namespace to `Integer`, so it fails over to an unmodified call. | Find the `JobInfo` / job id by type, not by position (the engine already has `MethodParameterUtils` helpers). Confirm on the phone in T11 step 3. |
| 2 | **Medium, silent** | `app/BActivityThread.java:1122` `handleNewIntent` | The phone has only `ActivityThread.handleNewIntent(ActivityClientRecord, List)`. None of the engine's 3 reflection forms exist: `performNewIntents(IBinder, List)`, `performNewIntents(IBinder, List, boolean)`, `handleNewIntent(IBinder, List)`. | A guest activity that is already running never gets `onNewIntent`. This affects notification taps, deep links and `singleTop`/`singleTask` relaunches. It may be part of the N5 Recents double-launch. | Add a 4th form: `getActivityClient(token)` (that mirror exists on the phone) and then `handleNewIntent(record, list)`. Test with kit case K9. |
| 3 | Medium-low, silent | `core/system/pm/PackageManagerCompat.java:209` | `SigningInfo` now takes `android.content.pm.SigningDetails`. The `PackageParser$SigningDetails` constructor the mirror asks for is gone. | `PackageInfo.signingInfo` is `null` for a guest package **with no host install** (`base == null`) when the app asks for `GET_SIGNING_CERTIFICATES`. MLBB and Instagram reuse the host install, so they take the `base` branch and are **not** affected today. | Build `SigningInfo(SigningDetails)` on API 33+. |
| 4 | Low, silent | `BaseInstrumentationDelegate` / `AppInstrumentation` | 16 `Instrumentation` methods are not overridden. They include the Android 15+ `callActivityOnNewIntent(Activity, ReferrerIntent, ComponentCaller)` (the form `ActivityThread` calls now), `execStartActivity(..., String, ..., UserHandle)` and `execStartActivitiesAsUser`. | They run the stock `Instrumentation` code on the delegate instead of the wrapped original. Activity starts still go through the hooked `IActivityTaskManager`, so they stay virtualized. Only a guest's own `Instrumentation` subclass would be skipped. | Forward the 3 `callActivityOnNewIntent` variants together with fix 2. |
| 5 | Low, silent | `fake/service/IActivityManagerProxy.java:311` `setActivityLocusContext` | `args[1]` is now a `LocusId`; the hook casts it to `String`. | `ClassCastException`, which the hook catches, so the call is dropped. Only affects Android's "locus" (conversation shortcuts) hints. | Compare `LocusId` / skip. |
| 6 | Low, silent | Location: `BLocationManagerService.java:254`, mirror `ILocationListener.onLocationChanged(Location)` | The phone has `onLocationChanged(List<Location>, IRemoteCallback)` | The engine's **fake-location** pushes do nothing. This is a spoofing feature we don't use. Real location still passes through. | None needed (feature unused) |
| 7 | Low, silent | `utils/compat/StrictModeCompat.java:18` | `StrictMode.sVmPolicyMask` is gone. The first branch (`disableDeathOnFileUriExposure`) still exists, so the fallback is never reached. | None | None |
| 8 | Info | 57 legacy proxy hooks, plus 19 whose method moved to another interface that is also hooked (list in `t1-framework-diff-api37.txt` Section C). Examples: `IAppOpsService.noteOp/startOp`, `IConnectivityManager.registerNetworkCallback`, `ILocationManager.requestLocationUpdates`, `IAudioService.*Recording*`, `IPackageManager.checkSelfPermission` | The method names no longer exist on the phone | The hook never fires. The modern methods are mostly hooked under their new names (`noteOperation`, `requestNetwork`, `getLastLocation` ...). These are leftovers, not regressions. | Clean-up only |
| 9 | Info | 91 mirror members are missing; 70 of them are never called, and 21 are called. Rows 2, 3, 6 and 7 cover 6 of the called ones (row 2 accounts for 3). The other called ones only matter on older Android versions (`ActivityManagerNative.gDefault`, `H.LAUNCH_ACTIVITY`, `PackageParser.collectCertificates` pre-P forms, `ContextImpl.mExternal*Dir` ...) | Gone or renamed | Every call site found is behind a `BuildCompat` / `SDK_INT` check for an older release, or the mirror is never called. | None |

**Crash risks from Section A: none.** The four implemented interfaces are:

- `IServiceConnection` (4-argument `connected`, patch 0001);
- `IIntentReceiver.performReceive` (unchanged, 7 arguments);
- `IAccountAuthenticatorResponse` and `IAccountManagerResponse` (unchanged).

**Local AIDL copies (Section B):** 14 of 24 differ from the phone. Examples: `IJobService` has 3 new methods, `IJobCallback` 6, `IPackageInstallerSession` 25; `IPackageInstallerSession.commit` changed; `ISyncAdapter.initialize` was removed; and the Samsung `IWallpaperManagerCallback.onSem*` methods are new. **None of these matter today.** The engine never implements those Stubs and never calls the changed methods. At runtime the phone's own copy of each class wins, so transaction codes stay correct.

**`ClientTransactionHandler`:** the phone does have Samsung's abstract `handleCoreStatesChanged(Bundle)` (BLOCKED hidden API), confirming lead N4 on our firmware. NewBlackbox doesn't subclass `ClientTransactionHandler`, so for us it is **harmless**.

## OEM (Samsung / One UI) additions

For each of the 253 `android.*` / `com.android.*` types the engine implements, proxies, extends or mirrors, `t1_analyze.py` compared the phone's methods with the AOSP `main` source of that type. (It ran at about 1 request per second to stay under the gitiles rate limit.) It found **2,858 phone-only methods**. Each one is classified in `t1-oem-additions-api37.txt`:

| Class | Count | Meaning |
|---|---|---|
| **CRASH** (abstract method on an interface the engine implements) | **0** | `IServiceConnection`, `IIntentReceiver`, `IAccountAuthenticatorResponse` and `IAccountManagerResponse` have no Samsung additions |
| **SILENT** (method on `Instrumentation`, which the engine extends, not forwarded) | **0** | The only Samsung addition on `Instrumentation` is `checkPendingExceptionOnRavenwood()` (test harness; never called) |
| **PASS-THROUGH** (method on a proxied service interface; the call goes to the real service unchanged) | 924 | 63 have a `sem`/`samsung`/`knox` prefix. The biggest counts are `INetworkManagementService` 129, `INotificationManager` 95, `IWindowManager` 91, `IActivityTaskManager` 88, `IAudioService` 81, `IActivityManager` 62, `IDevicePolicyManager` 59 (32 `sem*`), `IPackageManager` 36. These are Samsung system features: App Lock, Cover-screen launcher, MDM/Knox, "long-live" apps, Dual DAR. A normal app doesn't call them, except through Samsung SDKs. **Harmless for MLBB and Instagram.** If a guest did call one, it would run with the engine's identity rather than the guest's. |
| harmless (mirror-only types, plus fields) | 1,934 | The engine only reads the named members of these types |

The engine can't be hurt by `ClientTransactionHandler.handleCoreStatesChanged(Bundle)`, because it doesn't subclass `ClientTransactionHandler` (see above). The 9 types whose AOSP source couldn't be found are listed at the end of the OEM file:

- `IBinderSession` is new in Android 17 and not yet public;
- the others are generated `R` classes, RenderScript, `AttributionSourceState` and `IMms$Stub`.

None of the 9 is implemented or extended by the engine.

## Files

| File | What |
|---|---|
| `t1-framework-diff-api37.txt` | Sections A (implemented interfaces), B (24 AIDL copies), C (proxy hooks vs phone, with each phone overload), E (`Instrumentation`, `handleNewIntent`) |
| `t1-mirror-check-api37.txt` | Section D: 216 mirrored classes, 626 members, each missing one with its call sites |
| `t1-oem-additions-api37.txt` | Phone methods not in AOSP `main`, classified |
| `engine/api-baseline/api37-signatures.txt` | API 37 baseline: every member of the watched types (reused by T12 `check-framework.sh`) |
| `phase0b/scripts/framework-diff.sh`, `t1_dexdb.py`, `t1_analyze.py` | Re-runnable tooling |

Pulled jars and dex dumps stay in `phase0b/work/` (gitignored: Google/Samsung binaries).

## Limits of this check

- The proxy check (C) finds positional bugs only where a hook casts or tests `args[i]`. Hooks that pass `args` through untouched were not reviewed line by line.
- The OEM scan compares method **names** with AOSP `main`. A Samsung overload of an existing AOSP name isn't flagged there. Sections A and B compare full signatures for the types where that matters.
- AOSP `main` lags Android 17 (Section 3.8), so some "OEM" entries may be unreleased Android 17 changes rather than Samsung's. Either way, they behave the same for the engine.
- Nothing was run on the phone. Fixes 1 and 2 are static findings. T11 step 3 (jobs) and kit case K9 (new intents) confirm them at runtime.
