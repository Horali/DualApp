#!/usr/bin/env bash
# Phase 0 / S0: device baseline for the DualApp feasibility study.
#
# READ-ONLY. Every command below only reads properties or lists state.
# Nothing is installed, cleared, created or modified on the phone.
#
# Usage (Git Bash on Windows):  bash phase0/scripts/baseline.sh
# Optional: ADB=/path/to/adb.exe bash phase0/scripts/baseline.sh

set -u
export MSYS_NO_PATHCONV=1  # stop Git Bash rewriting /data into a Windows path
ADB="${ADB:-adb}"
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="$HERE/../results"
mkdir -p "$OUT_DIR"
OUT="$OUT_DIR/baseline-$(date +%Y%m%d-%H%M%S).txt"

sh_() { "$ADB" shell "$@" 2>&1 | tr -d '\r'; }

section() { printf '\n===== %s =====\n' "$1"; }

{
  section "adb devices"
  "$ADB" devices -l 2>&1 | tr -d '\r'

  section "Identity and firmware"
  for p in ro.product.model ro.product.device ro.product.name \
           ro.build.version.release ro.build.version.sdk ro.build.version.sdk_full \
           ro.build.version.security_patch ro.build.display.id ro.build.fingerprint \
           ro.build.PDA ro.build.version.oneui ro.build.version.incremental \
           ro.boot.hardware ro.soc.model ro.board.platform; do
    printf '%-36s %s\n' "$p" "$(sh_ getprop "$p")"
  done

  section "Kernel"
  sh_ uname -a

  section "CPU ABIs"
  printf 'abilist   %s\n' "$(sh_ getprop ro.product.cpu.abilist)"
  printf 'abilist32 %s\n' "$(sh_ getprop ro.product.cpu.abilist32)"
  printf 'abilist64 %s\n' "$(sh_ getprop ro.product.cpu.abilist64)"

  section "Page size"
  sh_ getconf PAGE_SIZE

  section "Mainline modules (ART and others)"
  sh_ pm list packages --apex-only --show-versioncode
  sh_ cmd package list packages --apex-only --show-versioncode 2>/dev/null | grep -i -E 'art|runtime' || true

  section "Storage"
  sh_ df -h /data
  sh_ df -h /storage/emulated/0 2>/dev/null || true

  section "Users and profiles (read-only)"
  sh_ pm list users
  sh_ pm get-max-users
  sh_ getprop fw.max_users
  sh_ dumpsys user | grep -E -i 'UserInfo|type=|Max users|profile|flags=' | head -60

  section "Memory"
  sh_ cat /proc/meminfo | head -5

  section "SELinux mode"
  sh_ getenforce

  section "Target app metadata (no data access)"
  for pkg in com.mobile.legends com.instagram.android; do
    printf -- '--- %s\n' "$pkg"
    sh_ dumpsys package "$pkg" | grep -E 'versionName|versionCode|targetSdk|minSdk|primaryCpuAbi|codePath|splits|extractNativeLibs|pageSizeCompat|flags=' | head -20
  done
} > "$OUT"

echo "Baseline written to $OUT"
