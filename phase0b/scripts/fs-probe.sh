#!/usr/bin/env bash
# T3 filesystem capability probe (PC side).
# Pipes phase0b/scripts/t3-device.sh into the engine UID and prints the result.
# Run from the repo root:
#   bash phase0b/scripts/fs-probe.sh > phase0b/results/t3/t3-fs-probe.txt 2>&1
set -u
export MSYS_NO_PATHCONV=1
A="${ADB:-/c/Users/child/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
PKG=top.niunaijun.blackbox

echo "T3 filesystem probe - $(date -Is)"
echo "== inotify limits (shell) =="
"$A" shell "cat /proc/sys/fs/inotify/max_user_watches; cat /proc/sys/fs/inotify/max_user_instances" 2>&1 | tr -d '\r'
echo "== device probe (engine UID) =="
"$A" shell run-as "$PKG" sh -s < phase0b/scripts/t3-device.sh 2>&1 | tr -d '\r'
