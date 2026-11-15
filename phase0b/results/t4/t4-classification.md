# T4 MLBB classification

Date: 2026-11-15. Host MLBB 2.2.23 (targetSdk 36), clone A in engine User 0.
Evidence: host inventory, full overlap hash (10,954 files), 4-minute clone A run.

## Headline numbers

| Item | Value |
|---|---|
| Host tree | 161,316 files / 175 folders / **11.77 GiB** |
| Clone A tree | 58,281 files / **1,958.2 MiB** |
| Clone A `img/` copy (Phase 0) | 47,069 files / **906.7 MiB** |
| Non-img overlap (host vs A) | 10,954 files |
| **Identical** | **10,931 (99.79%)** |
| **Different** | **23** |
| 4-minute run: changed in place | **8 files / 599,570 bytes** |
| 4-minute run: new files downloaded | **8,042 files / 484,670,121 bytes (~462 MiB)** |
| 4-minute run: deleted | 0 |
| Network received by UID 10057 in the run | 503,367,541 bytes (~480 MiB) |

## Classification

| Path (under `files/dragon2017/`) | Host size | Class | Evidence |
|---|---|---|---|
| `assets/Art` | 8.36 GiB | **Shareable** | UnityFS bundles; all sampled overlap identical |
| `assets/Audio` | 1.10 GiB | **Shareable** | identical in the overlap |
| `assets/img` | 1.01 GiB | **Shareable** | Phase 0 copy; untouched during the 4-minute run |
| `assets/UI` | 768 MiB | **Shareable** | identical in the overlap; the game downloads more here first |
| `assets/Document` | 164 MiB | **Shareable, except 3 `.meta` files** | 3 `DecompressNative/*.meta` differ → private |
| `assets/Scenes`, `Video`, `AstcInPack`, `VeryHighInPack`, `TextAsset`, `UnityData_NEW`, `RawImg`, `AStarPath` | 172 MiB total | **Shareable** | identical in the overlap; UnityFS/other, not ELF |
| `assets/comlibs` | 136 MiB | **Private** | 12 ELF files; loaded by the dynamic linker, which bypasses hooks |
| `assets/LuaServerUpdate` | 288 KiB | **Private** | server-pushed updates (mutable) |
| `assets/loadres_*`, `prefs_int`, `prefs_string`, `option_module_info`, `game_data_*`, `PatchFileList*.xml`, `version/`, `mode_versions_build_276`, `_other_download_statistics_` | small | **Private** | 23-file "different" set; download state and IDs |
| `assets/_finish`, `_extractor_`, `onceflag.dat` | tiny | **Private** | `_finish` is absent on A; markers |
| `login_data/**` | 8.5 KiB | **Private, hard exclude** | N2; names/sizes only, never read |
| `files/__app_uuid__`, `shared_prefs/snssdk_openudid.xml`, `shared_prefs/DeviceSettings.xml` | tiny | **Private, never copied** | N6 identity; `.DNA_`, `.DNA_FLAG`, `.gp_uuid`, `INSTALLATION`, `AdjustAttribution`, `Cdid.xml` were not found |
| `android/UpdateFile_*.txt`, `android/base_loadres_version_info` | 5.1 MiB | **Private** | changed in place; `base_loadres_version_info` caches an absolute path |
| `newbieoffline/*`, `newbieofflineData/*` | tiny | **Private** | changed in place during the run |
| `cache/**`, `files/UnityCache/**`, `files/ModeVersion/**`, `files/mini_patch/**` | small | **Private** | engine/game caches and patches |

## The 23 different files (private)

```text
android/UpdateFile_WriteCache_xxh.txt
android/UpdateFile_xxh.txt
android/base_loadres_version_info
assets/Document/android/DecompressNative/AIHeroSkill_native.unity3d.meta
assets/Document/android/DecompressNative/PropertyAlternative_native.unity3d.meta
assets/Document/android/DecompressNative/RankNewLevel_native.unity3d.meta
assets/_other_download_statistics_
assets/loadres_checking_progress
assets/loadres_module_info_high
assets/loadres_skin_version.dat
assets/module_remove_time
assets/option_module_info
assets/prefs_int
assets/prefs_string
login_data/check_upgrade_cache
login_data/login_auth_cache
login_data/login_auth_md5
login_data/login_upgrade_md5
login_data/native_login
login_data/prefs/_login_RecordStep_.dat
newbieoffline/Newbie495Data.bin
newbieoffline/newbieOffline.bin
newbieofflineData/RankOfflineIdentity.bin
```

## The 8 files changed in place by the 4-minute run (mutable → private)

```text
android/UpdateFile_WriteCache_xxh.txt   140,085 -> 91,142 bytes
android/UpdateFile_xxh.txt              167,351 -> 499,465 bytes
assets/loadres_module_info_high           3,114 -> 3,699 bytes
assets/prefs_int                             103 -> 103 bytes (content changed)
login_data/check_upgrade_cache            2,379 -> 2,135 bytes
login_data/login_auth_cache               1,156 -> 1,156 bytes (content changed)
login_data/login_upgrade_md5              1,535 -> 1,633 bytes
login_data/native_login                     237 -> 237 bytes (content changed)
```

## What the run downloaded (new files, by folder)

| Folder | Bytes | Files |
|---|---|---|
| assets/UI | 245,035,939 | 2,544 |
| assets/Art | 128,640,215 | 3,813 |
| assets/Audio | 75,292,414 | 1,014 |
| assets/Video | 27,753,073 | 9 |
| assets/temp | 5,095,424 | 1 |
| assets/RawImg | 1,354,760 | 8 |
| assets/TextAsset | 1,264,336 | 636 |
| assets/Document | 188,777 | 2 |
| mini_patch | 37,203 | 11 |
| cache/UnityShaderCache | 12,105 | 3 |

The game downloads **UI first, then Art, Audio and Video** on the Create Character screen. That is the order T10 will seed.

## Absolute-path caches (N1 confirmed)

All of these name **clone A's own instance root** (`.../storage/emulated/0/...` under the engine), never the host root:

| File | Key | Value (shortened) |
|---|---|---|
| `shared_prefs/__SharedPreference__.xml` | `moba_loading_bg` | `.../storage/emulated/0/Android/data/com.mobile.legends/files/dragon2017/assets/RawImg/android/loadproeff_02_add` |
| `shared_prefs/__SharedPreference__.xml` | `__Extracted_PersistentFilesPath__` | `.../storage/emulated/0/Android/data/com.mobile.legends/files` |
| `shared_prefs/com.mobile.legends.v2.playerprefs.xml` | `RWPathRec` (URL-encoded) | `%2Fstorage%2Femulated%2F0%2F...%2Fcom.mobile.legends%2Ffiles` |
| `shared_prefs/DeviceSettings.xml` | `__x__PersistPath_x__KEY__` | `.../storage/emulated/0/Android/data/com.mobile.legends/files` |
| `android/base_loadres_version_info` | (line) | `.../assets/Art/android/s/ShaderVariants_add.unity3d;1235.1` |

`IphonePath` and `__SDKOverrideAssetsPath__` were **not present** in clone A's prefs. `__SDKOverrideError__` is `SDKOverride_AssetsPathEmpty` (no override).

**Consequence:** copying A's preferences or `base_loadres_version_info` into B would plant A's root in B. The Sharing Profile must keep all preferences and metadata private, and the T10 path scan must find none of A's paths in B.

## Unpack payloads (N3)

- The base APK contains **misspelled** `assets/Resouces4-1.dat` (20,522,213 B), `Resouces4-2.dat` (21,017,693 B), `Resouces4-3.dat` (25,913,687 B), `Resouces4-4.dat` (29,187,304 B).
- `split_config.arm64_v8a.apk` contains `lib/arm64-v8a/libResouces_z0_0..z1_4.so` (12 files, 1.0–2.1 MB each, **ELF**).
- Neither tree contains files with those names, so the extracted output is renamed or repacked. The likely outputs are the `assets/Art/android/ArtResource.unity3d` (29.3 MB), `assets/UnityData_NEW/Managed/Metadata/global-csharp-metadata.dat` (20.1 MB) and similar bundles; these are byte-identical between host and A and are **not ELF**, so they are shareable.
- The `libResouces_z*.so` payloads are ELF → **private** (they are loaded from the APK's native library dir, not from the shared tree).
