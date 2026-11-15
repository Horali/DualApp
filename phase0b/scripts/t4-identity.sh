#!/system/bin/sh
# T4 step 8: identity and login files, names and sizes only (never contents).
P=top.niunaijun.blackbox
INT=/data/user/0/$P/blackbox/data/user/0/com.mobile.legends
EXT=/storage/emulated/0/Android/data/$P/files/blackbox/storage/emulated/0/Android/data/com.mobile.legends
HOST=/storage/emulated/0/Android/data/com.mobile.legends/files/dragon2017

echo "== identity files in clone A internal =="
find "$INT" \( -name '.DNA*' -o -name '.gp_uuid' -o -name 'INSTALLATION' -o -name 'AdjustAttribution' -o -name '__app_uuid__' -o -name 'Cdid.xml' \) -print 2>/dev/null | xargs stat -c '%s %n' 2>/dev/null

echo "== identity files in clone A external =="
find "$EXT" \( -name '.DNA*' -o -name '.gp_uuid' -o -name 'INSTALLATION' -o -name 'AdjustAttribution' -o -name '__app_uuid__' -o -name 'Cdid.xml' \) -print 2>/dev/null | xargs stat -c '%s %n' 2>/dev/null

echo "== login_data host (names and sizes) =="
find "$HOST/login_data" -type f -exec stat -c '%s %n' {} + 2>/dev/null

echo "== login_data clone A (names and sizes) =="
find "$EXT/files/dragon2017/login_data" -type f -exec stat -c '%s %n' {} + 2>/dev/null

echo "== done =="
