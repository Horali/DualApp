#!/system/bin/sh
# T4 step 9: unpack payloads in the host APK splits and their extracted output.
HOST=/storage/emulated/0/Android/data/com.mobile.legends/files/dragon2017
CLONE=/storage/emulated/0/Android/data/top.niunaijun.blackbox/files/blackbox/storage/emulated/0/Android/data/com.mobile.legends

echo "== APK entries matching Resources / libResources (both spellings) =="
for p in $(pm path com.mobile.legends | sed 's/package://'); do
  hits=$(unzip -l "$p" 2>/dev/null | grep -E 'Resouces|Resources|libResouces|libResources')
  if [ -n "$hits" ]; then
    echo "-- $p"
    echo "$hits"
  fi
done

echo "== payload files in host tree =="
find "$HOST" \( -name 'Resouces*.dat' -o -name 'Resources*.dat' -o -name 'libResouces*.so' -o -name 'libResources0.so' \) -exec stat -c '%s %n' {} + 2>/dev/null

echo "== payload files in clone A tree =="
find "$CLONE" \( -name 'Resouces*.dat' -o -name 'Resources*.dat' -o -name 'libResouces*.so' -o -name 'libResources0.so' \) -exec stat -c '%s %n' {} + 2>/dev/null

echo "== recently extracted candidates in clone A (assets/temp, mini_patch) =="
find "$CLONE/files/dragon2017/assets/temp" "$CLONE/files/mini_patch" -type f -exec stat -c '%s %Y %n' {} + 2>/dev/null | head -n 20

echo "== done =="
