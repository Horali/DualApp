#!/usr/bin/env bash
# T0 baseline for DualApp Phase 0b: read-only checks only.
# Run from Git Bash, from the repo root:
#   bash phase0b/scripts/t0-baseline.sh > phase0b/results/t0/t0-baseline.txt 2>&1
# Nothing here installs, launches, changes settings or deletes anything.
set -u
export MSYS_NO_PATHCONV=1
ADB="${ADB:-/c/Users/child/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
PKG=top.niunaijun.blackbox
INT="/data/user/0/$PKG/blackbox"
EXT="/storage/emulated/0/Android/data/$PKG/files/blackbox"

say() { printf '\n===== %s =====\n' "$*"; }
sh_() { "$ADB" shell "$@" 2>&1; }
# adb shell output on Windows ends lines with CRLF; strip the CR.
clean() { tr -d '\r'; }

say "T0 baseline (read-only) - $(date -Is)"

say "adb version"
"$ADB" version 2>&1 | head -n 3

say "adb devices"
"$ADB" devices -l | clean

say "build identity"
sh_ "getprop ro.product.model; getprop ro.product.device; getprop ro.build.fingerprint; getprop ro.build.display.id; getprop ro.build.version.release; getprop ro.build.version.sdk; getprop ro.build.version.security_patch; getprop ro.build.version.incremental; getprop ro.build.version.oneui; getprop ro.build.date" | clean

say "ART module (com.google.android.art)"
sh_ "dumpsys package com.google.android.art | grep -E 'versionCode|versionName|lastUpdateTime|firstInstallTime' | head -n 10" | clean

say "boot time"
sh_ "cat /proc/uptime; date" | clean

say "free space"
sh_ "df -h /data /storage/emulated/0; echo ---; df -k /data | tail -n 1" | clean

say "engine package (installed build)"
sh_ "dumpsys package $PKG | grep -E 'versionCode|versionName|lastUpdateTime|firstInstallTime|targetSdk|codePath' | head -n 20" | clean

say "engine UID"
sh_ "dumpsys package $PKG | grep -E 'appId=|uid=10057' | head -n 5" | clean

say "engine data size (KiB)"
echo -n "internal total: "; sh_ "run-as $PKG du -sk /data/user/0/$PKG 2>&1 | tail -n 1" | clean
echo -n "external total: "; sh_ "run-as $PKG du -sk $EXT 2>&1 | tail -n 1" | clean

say "engine users and packages (internal)"
for u in $(sh_ "run-as $PKG ls -1 $INT/data/user" | clean); do
  echo "-- user $u --"
  sh_ "run-as $PKG ls -1 $INT/data/user/$u" | clean
  for p in $(sh_ "run-as $PKG ls -1 $INT/data/user/$u" | clean); do
    sh_ "run-as $PKG du -sk $INT/data/user/$u/$p" | clean
  done
  sh_ "run-as $PKG du -sk $INT/data/user/$u" | clean
done

say "engine users and packages (external)"
for u in $(sh_ "run-as $PKG ls -1 $EXT/storage/emulated" | clean); do
  echo "-- user $u --"
  sh_ "run-as $PKG ls -1 $EXT/storage/emulated/$u/Android/data" | clean
  for p in $(sh_ "run-as $PKG ls -1 $EXT/storage/emulated/$u/Android/data" | clean); do
    sh_ "run-as $PKG du -sk $EXT/storage/emulated/$u/Android/data/$p" | clean
  done
  sh_ "run-as $PKG du -sk $EXT/storage/emulated/$u" | clean
done

say "network counter source 1: trafficcontroller"
echo -n "byte-counter lines in dump: "
sh_ "dumpsys connectivity trafficcontroller | grep -c -E 'rxBytes|txBytes|rx_bytes|tx_bytes'" | clean
echo "10057 appearances (all are permission tables):"
sh_ "dumpsys connectivity trafficcontroller | grep -n '10057'" | clean

say "network counter source 2: netstats detail"
echo -n "per-UID entries in dump: "
sh_ "dumpsys netstats detail | grep -c 'uid='" | clean
echo -n "entries for uid=10057: "
sh_ "dumpsys netstats detail | grep -c 'uid=10057'" | clean
echo "sample format:"
sh_ "dumpsys netstats detail | sed -n '/^UID stats:/,/^UID tag stats:/p' | sed -n '3,8p'" | clean

say "/proc/net/dev (device-wide snapshot)"
sh_ "cat /proc/net/dev" | clean

say "done"
