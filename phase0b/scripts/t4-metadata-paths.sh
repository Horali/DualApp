#!/system/bin/sh
# T4 step 7b: absolute paths inside the metadata files (binary-safe grep).
HOST=/storage/emulated/0/Android/data/com.mobile.legends/files/dragon2017
CLONE=/storage/emulated/0/Android/data/top.niunaijun.blackbox/files/blackbox/storage/emulated/0/Android/data/com.mobile.legends/files/dragon2017

for side in HOST CLONE; do
  case $side in
    HOST) ROOT=$HOST ;;
    CLONE) ROOT=$CLONE ;;
  esac
  echo "== $side =="
  for rel in \
    assets/last_download_state assets/loadres_checking_progress \
    assets/loadres_module_info_high assets/option_module_info \
    assets/PatchFileList.xml assets/PatchFileList_SDK.xml \
    assets/onceflag.dat assets/module_nouse_time assets/module_remove_time \
    assets/loadres_skin_version.dat assets/game_data_30028189 \
    assets/last_mode_check_state assets/_extractor_ assets/_finish \
    assets/prefs_int assets/prefs_string assets/prefs_long assets/prefs_float \
    assets/mode_versions_build_276 android/UpdateFile_WriteCache_xxh.txt \
    android/UpdateFile_xxh.txt android/base_loadres_version_info; do
    f="$ROOT/$rel"
    [ -f "$f" ] || continue
    sz=$(stat -c '%s' "$f")
    hits=$(grep -a -o -E '/(storage|sdcard|data)/[^"[:space:]]*' "$f" 2>/dev/null | head -n 3)
    if [ -n "$hits" ]; then
      echo "$rel size=$sz"
      echo "$hits" | sed 's/^/  /'
    else
      echo "$rel size=$sz paths=none"
    fi
  done
done
echo "== done =="
