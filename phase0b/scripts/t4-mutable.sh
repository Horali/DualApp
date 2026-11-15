#!/usr/bin/env bash
# T4 step 6: mutable-file check. Hash clone A, run it for 4 minutes, hash again.
set -u
export MSYS_NO_PATHCONV=1
A=/c/Users/child/AppData/Local/Android/Sdk/platform-tools/adb.exe
PKG=top.niunaijun.blackbox

echo "=== hash before ==="
"$A" shell sh -s before < phase0b/scripts/t4-hash.sh 2>&1 | tr -d '\r'

echo "=== netstats before (uid 10057) ==="
"$A" shell "dumpsys netstats detail | grep -A2 'uid=10057'" 2>&1 | tr -d '\r' | head -n 5

echo "=== launch MLBB A ==="
"$A" shell input keyevent KEYCODE_WAKEUP
"$A" shell monkey -p "$PKG" -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
sleep 3
"$A" shell uiautomator dump /sdcard/Download/bb_ui.xml >/dev/null 2>&1
"$A" shell cat /sdcard/Download/bb_ui.xml 2>/dev/null | tr '>' '\n' | grep -o -E 'text="User [0-9]+"' | head -n 1
"$A" shell rm -f /sdcard/Download/bb_ui.xml
"$A" shell input tap 540 518
sleep 15
echo "=== processes after launch ==="
"$A" shell "ps -A -o PID,STAT,NAME | grep -E 'com.mobile.legends|niunaijun'" 2>&1 | tr -d '\r'
"$A" exec-out screencap -p > phase0b/work/ui/t4-run-15s.png

echo "=== run 240 s ==="
sleep 240
"$A" exec-out screencap -p > phase0b/work/ui/t4-run-240s.png

echo "=== netstats after (uid 10057) ==="
"$A" shell "dumpsys netstats detail | grep -A2 'uid=10057'" 2>&1 | tr -d '\r' | head -n 5

echo "=== stop the engine ==="
"$A" shell am force-stop "$PKG"
sleep 2

echo "=== hash after ==="
"$A" shell sh -s after < phase0b/scripts/t4-hash.sh 2>&1 | tr -d '\r'

echo "=== pull hash files ==="
mkdir -p phase0b/work/t4
"$A" pull /data/local/tmp/t4/a-before.sha phase0b/work/t4/a-before.sha 2>&1 | tail -n 1
"$A" pull /data/local/tmp/t4/a-after.sha phase0b/work/t4/a-after.sha 2>&1 | tail -n 1
"$A" pull /data/local/tmp/t4/a-sizes-before.txt phase0b/work/t4/a-sizes-before.txt 2>&1 | tail -n 1
"$A" pull /data/local/tmp/t4/a-sizes-after.txt phase0b/work/t4/a-sizes-after.txt 2>&1 | tail -n 1
echo "done"
