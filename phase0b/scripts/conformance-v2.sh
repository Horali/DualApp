#!/usr/bin/env bash
# T6 storage conformance suite v2 (C1-C22, Java + JNI, ext + int).
set -eu
export MSYS_NO_PATHCONV=1

ADB="C:/Users/child/AppData/Local/Android/Sdk/platform-tools/adb.exe"
if [ -n "${ADB_SERIAL:-}" ]; then
  SER="$ADB_SERIAL"
elif "$ADB" devices | grep -qE '^RRCY1032Y6F[[:space:]]+device'; then
  SER="RRCY1032Y6F"
else
  SER="192.168.1.17:5555"
fi
PKG="top.niunaijun.blackbox"
CANARY="dev.dualapp.canary"

ACTUAL_SER=$("$ADB" -s "$SER" shell getprop ro.serialno | tr -d '\r ')
if [ "$ACTUAL_SER" != "RRCY1032Y6F" ]; then
  echo "ERROR: target $SER has ro.serialno=$ACTUAL_SER (expected RRCY1032Y6F)" >&2
  exit 1
fi

INT_FILES="/data/user/0/$PKG/files"
BB_EXT="/storage/emulated/0/Android/data/$PKG/files/blackbox"
POOL="$BB_EXT/dualfs/pool/$CANARY"

U0_EXT_FILES="$BB_EXT/storage/emulated/0/Android/data/$CANARY/files"
U0_INT_FILES="/data/user/0/$PKG/blackbox/data/user/0/$CANARY/files"
U1_EXT_FILES="$BB_EXT/storage/emulated/1/Android/data/$CANARY/files"
U1_INT_FILES="/data/user/0/$PKG/blackbox/data/user/1/$CANARY/files"

U0_EXT_T6="$U0_EXT_FILES/t6"
U0_INT_T6="$U0_INT_FILES/t6"
U1_EXT_T6="$U1_EXT_FILES/t6"
U1_INT_T6="$U1_INT_FILES/t6"

OUT="${1:-phase0b/results/t6/t6-conformance.txt}"
mkdir -p "$(dirname "$OUT")" phase0b/work/t6

echo "=== T6 Conformance v2 ($(date -Iseconds)) ===" | tee "$OUT"
echo "Device: $SER | Label: upstream engine, spoofing on" | tee -a "$OUT"

# 1. Generate deterministic 1 MiB and 64 MiB blobs locally and compute hashes.
python - <<'PY' > phase0b/work/t6/hashes.env
import hashlib, os

def make_blob(path, size, seed):
    h_full = hashlib.sha256()
    h_1000 = hashlib.sha256()
    block = bytearray(65536)
    x = seed & 0xFFFFFFFFFFFFFFFF
    rem = size
    written = 0
    with open(path, 'wb') as f:
        while rem > 0:
            n = min(65536, rem)
            for i in range(0, n, 8):
                x ^= (x << 13) & 0xFFFFFFFFFFFFFFFF
                x ^= (x >> 7)
                x ^= (x << 17) & 0xFFFFFFFFFFFFFFFF
                chunk = ((x * 0x2545F4914F6CDD1D) & 0xFFFFFFFFFFFFFFFF).to_bytes(8, 'little')
                take = min(8, n - i)
                block[i:i+take] = chunk[:take]
            mv = memoryview(block)[:n]
            f.write(mv)
            h_full.update(mv)
            if written < 1000:
                h_1000.update(mv[:min(n, 1000 - written)])
            written += n
            rem -= n
    return h_full.hexdigest(), h_1000.hexdigest()

s1, s1000 = make_blob('phase0b/work/t6/blob1m.bin', 1048576, 0x1111222233334444)
s64, _ = make_blob('phase0b/work/t6/blob64m.bin', 64 * 1048576, 0x5555666677778888)
print(f"SHA1M={s1}")
print(f"SHA1M_1000={s1000}")
print(f"SHA64M={s64}")
PY

source phase0b/work/t6/hashes.env
echo "SHA1M=$SHA1M" | tee -a "$OUT"
echo "SHA1M_1000=$SHA1M_1000" | tee -a "$OUT"
echo "SHA64M=$SHA64M" | tee -a "$OUT"

# Build manifest for dualfs-tool seed
cat > phase0b/work/t6/manifest.txt <<EOF
$SHA64M 67108864 c01_64m.bin
$SHA1M 1048576 c01_java.bin
$SHA1M 1048576 c01_jni.bin
$SHA1M 1048576 c02_java.bin
$SHA1M 1048576 c02_jni.bin
$SHA1M 1048576 c03_java.bin
$SHA1M 1048576 c03_jni.bin
$SHA1M 1048576 c04_java.bin
$SHA1M 1048576 c04_jni.bin
$SHA1M 1048576 c05_java.bin
$SHA1M 1048576 c05_jni.bin
$SHA1M 1048576 c06_java.bin
$SHA1M 1048576 c06_jni.bin
$SHA1M 1048576 c07_java.bin
$SHA1M 1048576 c07_jni.bin
$SHA1M 1048576 c08_java.bin
$SHA1M 1048576 c08_jni.bin
$SHA1M 1048576 c09_java.bin
$SHA1M 1048576 c09_jni.bin
$SHA1M 1048576 c10_java.bin
$SHA1M 1048576 c10_jni.bin
$SHA1M 1048576 c11_java.bin
$SHA1M 1048576 c11_jni.bin
$SHA1M 1048576 c12_java.bin
$SHA1M 1048576 c12_jni.bin
$SHA1M 1048576 c13_path.bin
$SHA1M 1048576 c13_fd.bin
$SHA1M 1048576 c14_java.bin
$SHA1M 1048576 c14_jni.bin
$SHA1M 1048576 c15_java.bin
$SHA1M 1048576 c15_jni.bin
$SHA1M 1048576 c16_java.bin
$SHA1M 1048576 c16_jni.bin
$SHA1M 1048576 c17_java.bin
$SHA1M 1048576 c17_jni.bin
$SHA1M 1048576 c18.bin
$SHA1M 1048576 c19.bin
$SHA1M 1048576 c20.bin
$SHA1M 1048576 c21.bin
EOF

# 2. Push blobs, tools, and manifest to phone (skip 64 MiB blob if identical size already staged).
SZ1=$("$ADB" -s "$SER" shell "stat -c %s /data/local/tmp/t6_1m.bin 2>/dev/null || echo 0" | tr -d '\r ')
SZ64=$("$ADB" -s "$SER" shell "stat -c %s /data/local/tmp/t6_64m.bin 2>/dev/null || echo 0" | tr -d '\r ')
[ "$SZ1" = "1048576" ] || "$ADB" -s "$SER" push phase0b/work/t6/blob1m.bin /data/local/tmp/t6_1m.bin >/dev/null
[ "$SZ64" = "67108864" ] || "$ADB" -s "$SER" push phase0b/work/t6/blob64m.bin /data/local/tmp/t6_64m.bin >/dev/null
"$ADB" -s "$SER" push phase0b/work/t6/manifest.txt /data/local/tmp/t6_manifest.txt >/dev/null
"$ADB" -s "$SER" push phase0b/tools/dualfs-tool/dualfs-tool /data/local/tmp/dualfs-tool >/dev/null
"$ADB" -s "$SER" push phase0b/tools/dfaudit/dfaudit /data/local/tmp/dfaudit >/dev/null

# 3. Reset pool, configs, and seed U0/U1 (ext + int).
"$ADB" -s "$SER" shell "am force-stop $PKG"
"$ADB" -s "$SER" shell "run-as $PKG sh -s" <<EOF | tee -a "$OUT"
set -eu
export TMPDIR="$INT_FILES"
cp /data/local/tmp/dualfs-tool "$INT_FILES/dualfs-tool"
cp /data/local/tmp/dfaudit "$INT_FILES/dfaudit"
chmod 700 "$INT_FILES/dualfs-tool" "$INT_FILES/dfaudit"

chmod -R u+w "$POOL" 2>/dev/null || true
rm -rf "$POOL" "$BB_EXT/dualfs/inst/0/$CANARY" "$BB_EXT/dualfs/inst/1/$CANARY" \
       "$U0_EXT_T6" "$U0_INT_T6" "$U1_EXT_T6" "$U1_INT_T6" \
       "$INT_FILES/dualfs-tmp-u0" "$INT_FILES/dualfs-tmp-u1" "$INT_FILES/t6-work"

D1="${SHA1M:0:2}"
D64="${SHA64M:0:2}"
mkdir -p "$POOL/blobs/\$D1" "$POOL/blobs/\$D64" "$POOL/staging" "$POOL/journal" "$POOL/quarantine" \
         "$BB_EXT/dualfs/inst/0/$CANARY/tmp" "$BB_EXT/dualfs/inst/0/$CANARY/locks" \
         "$BB_EXT/dualfs/inst/1/$CANARY/tmp" "$BB_EXT/dualfs/inst/1/$CANARY/locks" \
         "$INT_FILES/dualfs-tmp-u0" "$INT_FILES/dualfs-tmp-u1" "$INT_FILES/t6-work" \
         "$U0_EXT_T6" "$U0_INT_T6" "$U1_EXT_T6" "$U1_INT_T6"

B1="$POOL/blobs/\$D1/$SHA1M"
B64="$POOL/blobs/\$D64/$SHA64M"
cp /data/local/tmp/t6_1m.bin "\$B1"
cp /data/local/tmp/t6_64m.bin "\$B64"
chmod 0444 "\$B1" "\$B64"
chmod 0555 "$POOL/blobs/\$D1" "$POOL/blobs/\$D64" "$POOL/blobs"

cat > "$INT_FILES/t6-work/seed-u0-ext.cfg" <<EOC
pool=$POOL
root=$U0_EXT_T6
tmp=$BB_EXT/dualfs/inst/0/$CANARY/tmp
locks=$BB_EXT/dualfs/inst/0/$CANARY/locks
EOC
cat > "$INT_FILES/t6-work/seed-u0-int.cfg" <<EOC
pool=$POOL
root=$U0_INT_T6
tmp=$INT_FILES/dualfs-tmp-u0
locks=$BB_EXT/dualfs/inst/0/$CANARY/locks
EOC
cat > "$INT_FILES/t6-work/seed-u1-ext.cfg" <<EOC
pool=$POOL
root=$U1_EXT_T6
tmp=$BB_EXT/dualfs/inst/1/$CANARY/tmp
locks=$BB_EXT/dualfs/inst/1/$CANARY/locks
EOC
cat > "$INT_FILES/t6-work/seed-u1-int.cfg" <<EOC
pool=$POOL
root=$U1_INT_T6
tmp=$INT_FILES/dualfs-tmp-u1
locks=$BB_EXT/dualfs/inst/1/$CANARY/locks
EOC

echo "=== SEED ==="
"$INT_FILES/dualfs-tool" seed "$INT_FILES/t6-work/seed-u0-ext.cfg" /data/local/tmp/t6_manifest.txt
"$INT_FILES/dualfs-tool" seed "$INT_FILES/t6-work/seed-u0-int.cfg" /data/local/tmp/t6_manifest.txt
"$INT_FILES/dualfs-tool" seed "$INT_FILES/t6-work/seed-u1-ext.cfg" /data/local/tmp/t6_manifest.txt
"$INT_FILES/dualfs-tool" seed "$INT_FILES/t6-work/seed-u1-int.cfg" /data/local/tmp/t6_manifest.txt

echo "=== VERIFY INITIAL TREES ==="
"$INT_FILES/dualfs-tool" verify-tree "$INT_FILES/t6-work/seed-u0-ext.cfg"
"$INT_FILES/dualfs-tool" verify-tree "$INT_FILES/t6-work/seed-u0-int.cfg"
"$INT_FILES/dualfs-tool" verify-tree "$INT_FILES/t6-work/seed-u1-ext.cfg"
"$INT_FILES/dualfs-tool" verify-tree "$INT_FILES/t6-work/seed-u1-int.cfg"

C21_EXT_MT=\$(stat -c %Y "$U0_EXT_T6/c21.bin")
C21_INT_MT=\$(stat -c %Y "$U0_INT_T6/c21.bin")
B1_MT=\$(stat -c %Y "\$B1")
B64_MT=\$(stat -c %Y "\$B64")
echo "B1_MT=\$B1_MT B64_MT=\$B64_MT"

for D in "$U0_EXT_FILES" "$U1_EXT_FILES"; do
  mkdir -p "\$D"
  cat > "\$D/t6_meta.txt" <<EOM
sha1m=$SHA1M
sha64m=$SHA64M
sha1m_1000=$SHA1M_1000
c21_ext_mtime=\$C21_EXT_MT
c21_int_mtime=\$C21_INT_MT
EOM
done

cat > "$BB_EXT/dualfs/inst/0/$CANARY/config" <<EOC
pool=$POOL
root=$U0_EXT_T6
root=$U0_INT_T6
tmp=$BB_EXT/dualfs/inst/0/$CANARY/tmp
tmp_int=$INT_FILES/dualfs-tmp-u0
locks=$BB_EXT/dualfs/inst/0/$CANARY/locks
pkg=$CANARY
user=0
EOC

cat > "$BB_EXT/dualfs/inst/1/$CANARY/config" <<EOC
pool=$POOL
root=$U1_EXT_T6
root=$U1_INT_T6
tmp=$BB_EXT/dualfs/inst/1/$CANARY/tmp
tmp_int=$INT_FILES/dualfs-tmp-u1
locks=$BB_EXT/dualfs/inst/1/$CANARY/locks
pkg=$CANARY
user=1
EOC
EOF

rm -f /dev/null 2>/dev/null || true

# Helper: run dfaudit in background on device
"$ADB" -s "$SER" shell "run-as $PKG sh -c 'rm -f $INT_FILES/t6-work/df_*.log; nohup $INT_FILES/dfaudit $U0_EXT_T6 600 </dev/null > $INT_FILES/t6-work/df_ext.log 2>&1 & echo \$! > $INT_FILES/t6-work/df_ext.pid; nohup $INT_FILES/dfaudit $U0_INT_T6 600 </dev/null > $INT_FILES/t6-work/df_int.log 2>&1 & echo \$! > $INT_FILES/t6-work/df_int.pid'"
sleep 1

# Launch BlackBox -> tap Canary User 0 (180,518)
"$ADB" -s "$SER" logcat -c
"$ADB" -s "$SER" shell "monkey -p $PKG -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1"
sleep 2
"$ADB" -s "$SER" shell "input tap 180 518"
sleep 5

echo "=== DUALFS HOOK INIT ===" | tee -a "$OUT"
DUALFS_INIT=$("$ADB" -s "$SER" logcat -d -s DUALFS)
echo "$DUALFS_INIT" | tee -a "$OUT"
if ! echo "$DUALFS_INIT" | grep -q "enabled: pkg=$CANARY"; then
  echo "WARN: DualFs did not log enabled on first tap (checking freezer / retrying once)..." | tee -a "$OUT"
  "$ADB" -s "$SER" shell "monkey -p $PKG -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1"
  sleep 2
  "$ADB" -s "$SER" shell "input tap 180 518"
  sleep 5
  DUALFS_INIT=$("$ADB" -s "$SER" logcat -d -s DUALFS)
  echo "$DUALFS_INIT" | tee -a "$OUT"
fi
if echo "$DUALFS_INIT" | grep -q "FAIL"; then
  echo "ERROR: DualFs hook install failed!" | tee -a "$OUT"
  exit 1
fi

D1="${SHA1M:0:2}"
D64="${SHA64M:0:2}"
B1="$POOL/blobs/$D1/$SHA1M"
B64="$POOL/blobs/$D64/$SHA64M"

EXT_LINES=0
INT_LINES=0
FAILS=0

for i in $(seq 1 21); do
  CASE="C$i"
  "$ADB" -s "$SER" logcat -c
  "$ADB" -s "$SER" shell "run-as $PKG sh -c 'printf \"%s\" \"$CASE\" > $U0_EXT_FILES/t6_cmd.txt'"
  "$ADB" -s "$SER" shell "input keyevent KEYCODE_C"
  for _ in $(seq 1 30); do
    if "$ADB" -s "$SER" logcat -d -s CANARY_T6 | grep -q "T6 DONE cmd=$CASE"; then
      break
    fi
    sleep 0.5
  done

  echo "--- $CASE ---" | tee -a "$OUT"
  RES=$("$ADB" -s "$SER" logcat -d -s CANARY_T6 | grep -E "CANARY_T6: $CASE " || true)
  echo "$RES" | tee -a "$OUT"
  if [ -z "$RES" ] || echo "$RES" | grep -q " FAIL"; then FAILS=$((FAILS+1)); fi

  # Check pool hashes and mtimes + User 1 view
  POOL_CHK=$("$ADB" -s "$SER" shell "run-as $PKG sh -c 'sha256sum $B1 $B64; stat -c \"%a %Y %n\" $B1 $B64; $INT_FILES/dualfs-tool verify-tree $INT_FILES/t6-work/seed-u1-ext.cfg; $INT_FILES/dualfs-tool verify-tree $INT_FILES/t6-work/seed-u1-int.cfg'")
  echo "$POOL_CHK" | tee -a "$OUT"
  if ! echo "$POOL_CHK" | grep -q "^$SHA1M " || ! echo "$POOL_CHK" | grep -q "^$SHA64M "; then
    echo "POOL_HASH_MISMATCH in $CASE" | tee -a "$OUT"
    FAILS=$((FAILS+1))
  fi
  if echo "$POOL_CHK" | grep -qE "bad=[1-9]"; then
    echo "U1_VERIFY_TREE_FAIL in $CASE" | tee -a "$OUT"
    FAILS=$((FAILS+1))
  fi

  # Check dfaudit delta
  sleep 0.3
  NEW_EXT=$("$ADB" -s "$SER" shell "run-as $PKG sh -c 'tail -n +$((EXT_LINES+1)) $INT_FILES/t6-work/df_ext.log'")
  NEW_INT=$("$ADB" -s "$SER" shell "run-as $PKG sh -c 'tail -n +$((INT_LINES+1)) $INT_FILES/t6-work/df_int.log'")
  EXT_LINES=$("$ADB" -s "$SER" shell "run-as $PKG sh -c 'wc -l < $INT_FILES/t6-work/df_ext.log'" | tr -d '\r ')
  INT_LINES=$("$ADB" -s "$SER" shell "run-as $PKG sh -c 'wc -l < $INT_FILES/t6-work/df_int.log'" | tr -d '\r ')
  if [ "$CASE" = "C18" ]; then
    if ! echo "$NEW_EXT" | grep -qE "ACCESS.*c18\.bin placeholder" || ! echo "$NEW_INT" | grep -qE "ACCESS.*c18\.bin placeholder"; then
      echo "DFAUDIT_MISS_C18" | tee -a "$OUT"
      FAILS=$((FAILS+1))
    fi
  elif [ "$CASE" = "C19" ]; then
    if ! echo "$NEW_EXT" | grep -qE "MODIFY.*c19\.bin placeholder" || ! echo "$NEW_INT" | grep -qE "MODIFY.*c19\.bin placeholder"; then
      echo "DFAUDIT_MISS_C19" | tee -a "$OUT"
      FAILS=$((FAILS+1))
    fi
  else
    if echo "$NEW_EXT$NEW_INT" | grep -qE "(ACCESS|MODIFY).* placeholder"; then
      echo "DFAUDIT_UNEXPECTED_BYPASS in $CASE" | tee -a "$OUT"
      FAILS=$((FAILS+1))
    fi
  fi
  echo "dfaudit_ext_events:" | tee -a "$OUT"
  echo "$NEW_EXT" | head -n 12 | tee -a "$OUT"
  echo "dfaudit_int_events:" | tee -a "$OUT"
  echo "$NEW_INT" | head -n 12 | tee -a "$OUT"
done

# Stop dfaudit
"$ADB" -s "$SER" shell "run-as $PKG sh -c 'kill \$(cat $INT_FILES/t6-work/df_ext.pid $INT_FILES/t6-work/df_int.pid) 2>/dev/null || true'"

# Verify User 1 in-guest reads
echo "=== VERIFY_U1 IN-GUEST ===" | tee -a "$OUT"
"$ADB" -s "$SER" shell "am force-stop $PKG"
sleep 1
"$ADB" -s "$SER" shell "run-as $PKG sh -c 'printf \"VERIFY_U1\" > $U1_EXT_FILES/t6_cmd.txt'"
"$ADB" -s "$SER" shell "monkey -p $PKG -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1"
sleep 2
"$ADB" -s "$SER" shell "input swipe 900 1000 150 1000 250"
sleep 1
"$ADB" -s "$SER" logcat -c
"$ADB" -s "$SER" shell "input tap 180 518"
sleep 5
"$ADB" -s "$SER" shell "input keyevent KEYCODE_C"
for _ in $(seq 1 30); do
  if "$ADB" -s "$SER" logcat -d -s CANARY_T6 | grep -q "T6 DONE cmd=VERIFY_U1"; then
    break
  fi
  sleep 0.5
done
U1_RES=$("$ADB" -s "$SER" logcat -d -s CANARY_T6)
echo "$U1_RES" | tee -a "$OUT"
if ! echo "$U1_RES" | grep -q "VERIFY_U1 EXT JNI PASS" || ! echo "$U1_RES" | grep -q "VERIFY_U1 INT JNI PASS"; then
  FAILS=$((FAILS+1))
fi

# Force-stop part of C22
echo "=== C22 AFTER FORCE-STOP ===" | tee -a "$OUT"
"$ADB" -s "$SER" shell "am force-stop $PKG"
sleep 1
"$ADB" -s "$SER" shell "run-as $PKG sh -c 'printf \"C22_CHECK\" > $U0_EXT_FILES/t6_cmd.txt'"
"$ADB" -s "$SER" shell "monkey -p $PKG -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1"
sleep 2
"$ADB" -s "$SER" logcat -c
"$ADB" -s "$SER" shell "input tap 180 518"
sleep 5
"$ADB" -s "$SER" shell "input keyevent KEYCODE_C"
for _ in $(seq 1 30); do
  if "$ADB" -s "$SER" logcat -d -s CANARY_T6 | grep -q "T6 DONE cmd=C22_CHECK"; then
    break
  fi
  sleep 0.5
done
C22_RES=$("$ADB" -s "$SER" logcat -d -s CANARY_T6)
echo "$C22_RES" | tee -a "$OUT"
if echo "$C22_RES" | grep -q " FAIL" || ! echo "$C22_RES" | grep -q "C22_CHECK EXT JAVA PASS"; then
  FAILS=$((FAILS+1))
fi

echo "=== DUALFS STATS & CRASH GREP ===" | tee -a "$OUT"
"$ADB" -s "$SER" logcat -d -s DUALFS | tail -n 30 | tee -a "$OUT"
"$ADB" -s "$SER" logcat -d | grep -E "FATAL EXCEPTION|SIGSEGV|SIGILL|ANR in" | head -n 20 | tee -a "$OUT" || true
echo "FAILS=$FAILS" | tee -a "$OUT"
