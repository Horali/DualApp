#!/system/bin/sh
# T3 device-side probe. Runs under the engine UID via:
#   adb shell run-as top.niunaijun.blackbox sh -s < phase0b/scripts/t3-device.sh
# Creates and deletes its own probe folders only.
L_INT=/data/user/0/top.niunaijun.blackbox/blackbox/dualfs-probe
L_EXT=/storage/emulated/0/Android/data/top.niunaijun.blackbox/files/blackbox/dualfs-probe

rm -rf "$L_INT" "$L_EXT"
mkdir -p "$L_INT" "$L_EXT" || exit 1

echo "== 1. filesystem type, mount and st_dev =="
stat -f -c 'path=%n type=%T hex=%t bsize=%S blocks=%b free=%f' "$L_INT" "$L_EXT"
stat -c 'path=%n dev=%d devhex=%D inode=%i' "$L_INT" "$L_EXT"
echo "-- mountinfo --"
grep -E ' /data | /storage/emulated ' /proc/self/mountinfo | head -n 6

echo "== 2. sparse file (64 MiB) =="
df -k "$L_EXT" | tail -n 1
truncate -s 64M "$L_EXT/sparse.bin"
echo -n "du -k: "; du -k "$L_EXT/sparse.bin"
echo -n "first bytes: "; dd if="$L_EXT/sparse.bin" bs=1 count=4 2>/dev/null | od -An -tx1
df -k "$L_EXT" | tail -n 1
rm -f "$L_EXT/sparse.bin"

echo "== 3. small files (allocated KiB) =="
for n in 1 3 4096 65536; do
  dd if=/dev/zero of="$L_EXT/small-$n" bs=1 count=$n 2>/dev/null
  echo -n "size=$n "; du -k "$L_EXT/small-$n"
done
rm -f "$L_EXT"/small-*

echo "== 4. xattrs =="
touch "$L_EXT/xattr.bin"
setfattr -n user.dualapp.blob -v test "$L_EXT/xattr.bin" 2>&1
echo "setfattr exit=$?"
getfattr -n user.dualapp.blob "$L_EXT/xattr.bin" 2>&1
mv "$L_EXT/xattr.bin" "$L_EXT/xattr2.bin"
echo "-- after rename --"
getfattr -n user.dualapp.blob "$L_EXT/xattr2.bin" 2>&1
rm -f "$L_EXT/xattr2.bin"

echo "== 5. rename =="
mkdir -p "$L_EXT/a" "$L_EXT/b"
touch "$L_EXT/a/f"
mv "$L_EXT/a/f" "$L_EXT/b/f" 2>&1
echo "same-fs rename exit=$?"
mv "$L_EXT/b/f" "$L_INT/f" 2>&1
echo "cross L-ext to L-int exit=$?"
rm -rf "$L_EXT/a" "$L_EXT/b" "$L_INT/f"

echo "== 6. inode cost: 10,000 placeholders (64 KiB logical) =="
cd "$L_EXT" || exit 1
mkdir -p many
df -k . | tail -n 1
seq 0 9999 | sed 's#^#many/p#' | xargs truncate -s 64K
df -k . | tail -n 1
rm -rf many

echo "== 6b. inode cost: 10,000 symlinks =="
mkdir -p links
df -k . | tail -n 1
if ln -s /dev/null links/probe 2>/dev/null; then
  echo "symlinks: ALLOWED"
  seq 0 9999 | sed 's#^#links/s#' | xargs -n 200 sh -c 'for f in "$@"; do ln -s /dev/null "$f"; done' sh
  df -k . | tail -n 1
else
  echo "symlinks: DENIED on L-ext (permission denied)"
fi
rm -rf links

echo "== 8. inotify =="
echo -n "max_user_watches: "; cat /proc/sys/fs/inotify/max_user_watches
echo -n "max_user_instances: "; cat /proc/sys/fs/inotify/max_user_instances
mkdir -p "$L_EXT/watch"
inotifyd - "$L_EXT/watch" > "$L_EXT/watch.out" 2>&1 &
IPID=$!
sleep 1
touch "$L_EXT/watch/event"
sleep 1
kill $IPID 2>/dev/null
echo "-- events --"
cat "$L_EXT/watch.out"
rm -rf "$L_EXT/watch" "$L_EXT/watch.out"

echo "== 9. copy baseline: 256 MiB via dd =="
dd if=/dev/zero of="$L_EXT/src.bin" bs=1M count=256 2>&1 | tail -n 1
sync
S=$(date +%s)
dd if="$L_EXT/src.bin" of="$L_EXT/dst.bin" bs=1M 2>&1 | tail -n 1
E=$(date +%s)
echo "copy_seconds=$((E-S))"
sync
rm -f "$L_EXT/src.bin" "$L_EXT/dst.bin"

echo "== 11. RLIMIT_NOFILE =="
ulimit -n

echo "== cleanup =="
rm -rf "$L_INT" "$L_EXT"
echo "done"
