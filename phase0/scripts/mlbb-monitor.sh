#!/usr/bin/env bash
# Launch MLBB inside NewBlackbox User 0 (icon already on the User 0 page) and
# record guest disk growth and device network traffic every 15 s.
# Stops MLBB (force-stops the engine) if the guest grows past CAP_MB or after MAX_S.
# Usage: bash mlbb-monitor.sh <label>   (ADB=path/to/adb.exe)
set -u
export MSYS_NO_PATHCONV=1
A="${ADB:-adb}"
LABEL="${1:-cold}"
CAP_MB="${CAP_MB:-2048}"
MAX_S="${MAX_S:-300}"
OUT="$(dirname "$0")/../results/mlbb-$LABEL"
mkdir -p "$OUT"
EXT=/storage/emulated/0/Android/data/top.niunaijun.blackbox/files/blackbox/storage/emulated/0/Android/data/com.mobile.legends
INT=/data/user/0/top.niunaijun.blackbox/blackbox/data/user/0/com.mobile.legends
pause() { ping -n "$1" 127.0.0.1 >/dev/null 2>&1 || sleep "$1"; }
netbytes() { "$A" shell cat /proc/net/dev | tr -d '\r' | awk '/wlan0|rmnet_data/ {rx+=$2} END {print rx+0}'; }
extkb() { "$A" shell "du -sk $EXT 2>/dev/null" | tr -d '\r' | cut -f1; }
intkb() { "$A" shell run-as top.niunaijun.blackbox du -sk $INT 2>/dev/null | tr -d '\r' | cut -f1; }

"$A" shell input keyevent KEYCODE_WAKEUP
"$A" logcat -c
N0=$(netbytes); E0=$(extkb); I0=$(intkb)
echo "t_s ext_MB int_MB net_rx_MB free" | tee "$OUT/log.txt"
"$A" shell input tap 540 487
T=0
while [ $T -le $MAX_S ]; do
  pause 15; T=$((T+15))
  E=$(extkb); I=$(intkb); N=$(netbytes)
  FREE=$("$A" shell df -h /data | tr -d '\r' | tail -1 | awk '{print $4}')
  printf '%s %s %s %s %s\n' $T $(( (${E:-0}-${E0:-0})/1024 )) $(( (${I:-0}-${I0:-0})/1024 )) $(( (N-N0)/1048576 )) $FREE | tee -a "$OUT/log.txt"
  "$A" exec-out screencap -p > "$OUT/t$T.png"
  if [ $(( (${E:-0}-${E0:-0})/1024 )) -ge $CAP_MB ]; then echo "CAP reached, stopping" | tee -a "$OUT/log.txt"; break; fi
  "$A" shell pidof top.niunaijun.blackbox:p0 >/dev/null 2>&1 || { echo "guest process gone at ${T}s" | tee -a "$OUT/log.txt"; }
done
"$A" logcat -d | tr -d '\r' | grep -i -E 'mobile.legends|unity|FATAL|AndroidRuntime|blackbox' | grep -v -E 'SurfaceFlinger|setRequestedFrameRate|Freecess' | tail -200 > "$OUT/logcat.txt"
"$A" shell am force-stop top.niunaijun.blackbox
echo "stopped engine"
