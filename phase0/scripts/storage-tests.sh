# Phase 0 / S4: shared-storage tests inside NewBlackbox's sandbox.
#
# Run on the phone as the engine's UID (same UID every guest app runs under):
#   adb shell run-as top.niunaijun.blackbox sh < phase0/scripts/storage-tests.sh
#
# Touches only the engine's own data dir and the two Canary guest dirs.
# Everything it creates is under dualapp_pool/ or named shared*.bin / *.tmp,
# and the cleanup at the end removes it all.

BB=/data/user/0/top.niunaijun.blackbox
POOL=$BB/dualapp_pool
G0=$BB/blackbox/data/user/0/dev.dualapp.canary/files
G1=$BB/blackbox/data/user/1/dev.dualapp.canary/files
MB=64

rep() { printf '%-44s %s\n' "$1" "$2"; }
sha() { sha256sum "$1" 2>/dev/null | cut -c1-16; }
try() { if eval "$1" >/dev/null 2>&1; then echo ALLOWED; else echo DENIED; fi; }

cd $BB || exit 1
echo "id: $(id)"
echo

# ---------- setup ----------
rm -rf $POOL; chmod -R u+w $POOL 2>/dev/null; rm -rf $POOL
mkdir -p $POOL/blobs
dd if=/dev/urandom of=$POOL/blobs/blob.tmp bs=1048576 count=$MB 2>/dev/null
H=$(sha256sum $POOL/blobs/blob.tmp | cut -d' ' -f1)
mv $POOL/blobs/blob.tmp $POOL/blobs/$H
BLOB=$POOL/blobs/$H
chmod 0444 $BLOB
chmod 0555 $POOL/blobs
echo "$H  ${MB}MiB" > $POOL/index.txt
ln -s $BLOB $G0/shared.bin
ln -s $BLOB $G1/shared.bin
REF=$(sha $BLOB)
echo "== setup: ${MB} MiB blob $REF, mode $(stat -c %a $BLOB), pool dir $(stat -c %a $POOL/blobs)"
echo

echo "== T1 read through symlink"
rep "U0 read hash matches"  "$([ "$(sha $G0/shared.bin)" = "$REF" ] && echo PASS || echo FAIL)"
rep "U1 read hash matches"  "$([ "$(sha $G1/shared.bin)" = "$REF" ] && echo PASS || echo FAIL)"
echo

echo "== T2 write to 0444 blob through symlink"
rep "append via U0 link"            "$(try "echo x >> $G0/shared.bin")"
rep "truncate via U0 link"          "$(try ": > $G0/shared.bin")"
rep "pool still intact"             "$([ "$(sha $BLOB)" = "$REF" ] && echo YES || echo NO)"
echo

echo "== T3 the 0444 assumption under a shared UID"
rep "chmod u+w on blob via U0 link" "$(try "chmod u+w $G0/shared.bin")"
rep "blob mode now"                 "$(stat -c %a $BLOB)"
rep "append via U0 link after chmod" "$(try "echo CORRUPT >> $G0/shared.bin")"
rep "U1 sees changed content"       "$([ "$(sha $G1/shared.bin)" != "$REF" ] && echo 'YES (cross-instance corruption)' || echo NO)"
# restore
truncate -s $((MB*1048576)) $BLOB 2>/dev/null
chmod 0444 $BLOB
rep "restored hash ok"              "$([ "$(sha $BLOB)" = "$REF" ] && echo YES || echo NO)"
echo

echo "== T4 delete"
rep "rm U0 symlink (own dir)"       "$(try "rm $G0/shared.bin")"
rep "pool blob still there"         "$([ -f $BLOB ] && echo YES || echo NO)"
ln -s $BLOB $G0/shared.bin
rep "rm blob in 0555 pool dir"      "$(try "rm -f $BLOB")"
rep "chmod pool dir u+w"            "$(try "chmod u+w $POOL/blobs")"
rep "rm blob after chmod (dry: mv)" "$(try "mv $BLOB $BLOB.gone && mv $BLOB.gone $BLOB")"
chmod 0555 $POOL/blobs
echo

echo "== T5 rename"
rep "rename U0 symlink"             "$(try "mv $G0/shared.bin $G0/shared2.bin && mv $G0/shared2.bin $G0/shared.bin")"
rep "link still resolves"           "$([ "$(sha $G0/shared.bin)" = "$REF" ] && echo YES || echo NO)"
echo

echo "== T6 kernel-level protections"
rep "hard link blob into U0 dir"    "$(try "ln $BLOB $G0/hard.bin")"; rm -f $G0/hard.bin
rep "chattr +i (immutable)"         "$(try "chattr +i $BLOB")"
rep "fsverity enable"               "$(try "fsverity enable $BLOB")"
rep "fs-verity measure"             "$(fsverity measure $BLOB 2>&1 | head -1 | cut -c1-40)"
echo

echo "== T7 copy-up (what an engine CoW hook would do)"
rep "kernel does CoW on open(O_WRONLY)?" "NO (T2/T3: write is denied or hits the shared blob)"
t0=$(date +%s%N)
cp $BLOB $G0/shared.bin.tmp && chmod 0600 $G0/shared.bin.tmp && mv -f $G0/shared.bin.tmp $G0/shared.bin
t1=$(date +%s%N)
rep "copy-up of ${MB} MiB (ms)"     "$(( (t1-t0)/1000000 ))"
rep "U0 now private file"           "$([ -L $G0/shared.bin ] && echo NO || echo YES)"
echo MODIFIED >> $G0/shared.bin
rep "U0 modified, pool intact"      "$([ "$(sha $BLOB)" = "$REF" ] && echo YES || echo NO)"
rep "U1 still sees original"        "$([ "$(sha $G1/shared.bin)" = "$REF" ] && echo YES || echo NO)"
rm -f $G0/shared.bin; ln -s $BLOB $G0/shared.bin
echo

echo "== T8 concurrency: 4 readers while U0 copies up"
: > $POOL/../readers.log 2>/dev/null || true
LOG=$BB/files/dualapp_readers.log; : > $LOG
for i in 1 2 3 4; do ( for j in 1 2 3; do sha $G1/shared.bin >> $LOG; done ) & done
cp $BLOB $G0/shared.bin.tmp && mv -f $G0/shared.bin.tmp $G0/shared.bin
wait
rep "reader hashes all equal ref"   "$([ "$(sort -u $LOG)" = "$REF" ] && echo YES || echo "NO: $(sort -u $LOG | tr '\n' ' ')")"
rep "reads completed"               "$(wc -l < $LOG)"
rm -f $LOG $G0/shared.bin; ln -s $BLOB $G0/shared.bin
echo

echo "== T9 crash during copy-up (kill -9 mid-copy)"
( cp $BLOB $G0/shared.bin.tmp && mv -f $G0/shared.bin.tmp $G0/shared.bin ) &
P=$!; sleep 0.05; kill -9 $P 2>/dev/null; wait $P 2>/dev/null
rep "target still the symlink"      "$([ -L $G0/shared.bin ] && echo YES || echo NO)"
rep "leftover partial tmp"          "$([ -e $G0/shared.bin.tmp ] && echo "YES ($(stat -c %s $G0/shared.bin.tmp) bytes, needs journal cleanup)" || echo NO)"
rep "U0 still reads original"       "$([ "$(sha $G0/shared.bin)" = "$REF" ] && echo YES || echo NO)"
rm -f $G0/shared.bin.tmp
echo

echo "== T10 integrity scrub (index vs pool)"
for f in $POOL/blobs/*; do n=$(basename $f); a=$(sha256sum $f | cut -d' ' -f1); rep "blob $(echo $n | cut -c1-16)" "$([ "$n" = "$a" ] && echo OK || echo MISMATCH)"; done
echo

echo "== L1 storage (bytes on disk)"
rep "pool blob"                     "$(du -k $BLOB | cut -f1) KiB"
rep "U0 link"                       "$(du -k $G0/shared.bin | cut -f1) KiB"
rep "U1 link"                       "$(du -k $G1/shared.bin | cut -f1) KiB"
rep "2 instances shared"            "$(( $(du -k $BLOB | cut -f1) )) KiB"
rep "2 instances private copies"    "$(( 2 * $(du -k $BLOB | cut -f1) )) KiB"
echo

echo "== cleanup"
rm -f $G0/shared.bin $G1/shared.bin
chmod -R u+w $POOL; rm -rf $POOL
rep "removed"                       "$([ -e $POOL ] && echo NO || echo YES)"
