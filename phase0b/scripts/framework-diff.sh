#!/usr/bin/env bash
# Phase 0b T1: diff NewBlackbox's framework expectations against the phone.
# Read-only on the phone: it only runs `adb pull` on BOOTCLASSPATH jars.
#
# Usage (Git Bash, from the repo root):
#   phase0b/scripts/framework-diff.sh [serial]
# Needs: adb, build-tools 36 dexdump, unzip, python 3, network access to
# android.googlesource.com (OEM scan). Outputs:
#   phase0b/results/t1/t1-framework-diff-api37.txt
#   phase0b/results/t1/t1-mirror-check-api37.txt
#   phase0b/results/t1/t1-oem-additions-api37.txt
#   engine/api-baseline/api<level>-signatures.txt
# Scratch (gitignored, never commit: Google/Samsung binaries): phase0b/work/
set -euo pipefail
export MSYS_NO_PATHCONV=1

SERIAL="${1:-RRCY1032Y6F}"
SDK="${ANDROID_SDK:-C:/Users/child/AppData/Local/Android/Sdk}"
ADB="$SDK/platform-tools/adb.exe"
DEXDUMP="$SDK/build-tools/36.0.0/dexdump.exe"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="$ROOT/phase0b/work"
ENGINE="$ROOT/phase0/engine/NewBlackbox/Bcore/src/main"

mkdir -p "$WORK/jars" "$WORK/dex" "$ROOT/phase0b/results/t1" "$ROOT/engine/api-baseline"
LEVEL="$("$ADB" -s "$SERIAL" shell getprop ro.build.version.sdk | tr -d '\r')"
"$ADB" -s "$SERIAL" shell getprop ro.build.fingerprint | tr -d '\r' > "$WORK/fingerprint.txt"
"$ADB" -s "$SERIAL" shell 'echo $BOOTCLASSPATH' | tr -d '\r' | tr ':' '\n' | sed '/^$/d' > "$WORK/bootclasspath.txt"

# 1. pull every BOOTCLASSPATH jar (what an app process actually loads)
while read -r p; do
  name="$(echo "$p" | sed 's#^/##; s#/#__#g')"
  [ -s "$WORK/jars/$name" ] || "$ADB" -s "$SERIAL" pull "$p" "$WORK/jars/$name" >/dev/null
done < "$WORK/bootclasspath.txt"

# 2. unpack classes*.dex and dump them
for j in "$WORK"/jars/*.jar; do
  b="$(basename "$j" .jar)"
  mkdir -p "$WORK/dex/$b"
  unzip -o -q "$j" 'classes*.dex' -d "$WORK/dex/$b" 2>/dev/null || true
  for d in "$WORK/dex/$b"/*.dex; do
    [ -e "$d" ] || continue
    [ -s "${d%.dex}.txt" ] || "$DEXDUMP" "$d" > "${d%.dex}.txt" 2>/dev/null
  done
done

# 3. class database, then the comparison
python -I "$ROOT/phase0b/scripts/t1_dexdb.py" "$WORK/dex" "$WORK/api$LEVEL.pickle"
python -I "$ROOT/phase0b/scripts/t1_analyze.py" "$WORK/api$LEVEL.pickle" "$ENGINE" \
  "$ROOT/phase0b/results/t1" "$ROOT/engine/api-baseline/api$LEVEL-signatures.txt" "$WORK/aosp"
