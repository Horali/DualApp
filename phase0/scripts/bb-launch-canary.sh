#!/usr/bin/env bash
# Open Canary inside NewBlackbox virtual user N (0 or 1) through the UI and print its log summary.
# Usage: bash bb-launch-canary.sh <user>
set -u
export MSYS_NO_PATHCONV=1
A="${ADB:-adb}"
U="$1"
pause() { ping -n "$1" 127.0.0.1 >/dev/null 2>&1 || sleep "$1"; }
header() {
  "$A" shell uiautomator dump /sdcard/Download/bb_ui.xml >/dev/null 2>&1
  "$A" shell cat /sdcard/Download/bb_ui.xml | tr '>' '\n' | grep -o -E 'text="User [0-9]+"' | grep -o -E '[0-9]+'
  "$A" shell rm -f /sdcard/Download/bb_ui.xml
}
"$A" shell input keyevent KEYCODE_WAKEUP
"$A" shell monkey -p top.niunaijun.blackbox -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
pause 5
for i in 1 2 3 4; do
  cur="$(header)"
  [ "$cur" = "$U" ] && break
  if [ "${cur:-0}" -lt "$U" ]; then "$A" shell input swipe 1300 1800 100 1800 250; else "$A" shell input swipe 100 1800 1300 1800 250; fi
  pause 3
done
[ "$(header)" = "$U" ] || { echo "could not reach User $U"; exit 1; }
"$A" logcat -c
"$A" shell input tap 180 480
pause 10
"$A" logcat -d -s CANARY | tr -d '\r' | grep -E 'SUMMARY|dataDir'
