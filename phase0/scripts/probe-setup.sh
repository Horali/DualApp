# Set up (or with arg "clean", remove) a 1 MiB 0444 blob in a shared pool,
# symlinked into both Canary guests as files/shared.bin.
# adb shell run-as top.niunaijun.blackbox sh < probe-setup.sh
BB=/data/user/0/top.niunaijun.blackbox
POOL=$BB/dualapp_pool
G0=$BB/blackbox/data/user/0/dev.dualapp.canary/files
G1=$BB/blackbox/data/user/1/dev.dualapp.canary/files
rm -f $G0/shared.bin $G1/shared.bin
chmod -R u+w $POOL 2>/dev/null; rm -rf $POOL
[ "${MODE:-setup}" = clean ] && { echo cleaned; exit 0; }
mkdir -p $POOL/blobs
dd if=/dev/urandom of=$POOL/blobs/blob bs=1048576 count=1 2>/dev/null
chmod 0444 $POOL/blobs/blob; chmod 0555 $POOL/blobs
ln -s $POOL/blobs/blob $G0/shared.bin
ln -s $POOL/blobs/blob $G1/shared.bin
echo "blob $(sha256sum $POOL/blobs/blob | cut -c1-16) mode $(stat -c %a $POOL/blobs/blob)"
ls -la $G0/shared.bin $G1/shared.bin
