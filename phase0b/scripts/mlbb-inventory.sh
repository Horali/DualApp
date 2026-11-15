#!/usr/bin/env bash
# T4 MLBB inventory orchestrator.
#
# Usage:
#   bash phase0b/scripts/mlbb-inventory.sh            # read-only stages
#   bash phase0b/scripts/mlbb-inventory.sh --mutable  # also run clone A for 4 minutes
#
# Raw output goes to phase0b/work/t4/ (gitignored). The curated results are in
# phase0b/results/t4/. Requires Python 3 (set PYTHON if it is not `python`).
set -u
export MSYS_NO_PATHCONV=1
A="${ADB:-/c/Users/child/AppData/Local/Android/Sdk/platform-tools/adb.exe}"
PY="${PYTHON:-python}"
OUT=phase0b/work/t4
mkdir -p "$OUT"

run_device() { "$A" shell sh -s < "$1" 2>&1 | tr -d '\r'; }

echo "== host inventory =="
run_device phase0b/scripts/t4-host.sh | tee "$OUT/host.txt"

echo "== clone A =="
run_device phase0b/scripts/t4-clone.sh | tee "$OUT/clone.txt"

echo "== overlap hashing =="
run_device phase0b/scripts/t4-overlap.sh | tee "$OUT/overlap-raw.txt"
"$A" pull /data/local/tmp/t4/host.sha "$OUT/host.sha" >/dev/null 2>&1
"$A" pull /data/local/tmp/t4/clone.sha "$OUT/clone.sha" >/dev/null 2>&1
"$PY" phase0b/scripts/t4_compare.py "$OUT" | tee "$OUT/overlap.txt"

echo "== metadata paths =="
run_device phase0b/scripts/t4-metadata-paths.sh | tee "$OUT/metadata-paths.txt"

echo "== identity =="
run_device phase0b/scripts/t4-identity.sh | tee "$OUT/identity.txt"

echo "== unpack =="
run_device phase0b/scripts/t4-unpack.sh | tee "$OUT/unpack.txt"

if [ "${1:-}" = "--mutable" ]; then
  echo "== mutable run (launches clone A for 4 minutes) =="
  bash phase0b/scripts/t4-mutable.sh
fi

echo "Raw output in $OUT. Curated results are in phase0b/results/t4/."
