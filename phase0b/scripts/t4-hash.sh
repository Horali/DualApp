#!/system/bin/sh
# T4 step 6 helper: hash clone A's whole tree. Usage: sh -s <tag>
CLONE=/storage/emulated/0/Android/data/top.niunaijun.blackbox/files/blackbox/storage/emulated/0/Android/data/com.mobile.legends
TAG=${1:-x}
TMP=/data/local/tmp/t4
mkdir -p "$TMP"

find "$CLONE" -type f -printf '%P\t%s\n' | sort > "$TMP/a-sizes-$TAG.txt"
find "$CLONE" -type f -printf '%P\n' | sort > "$TMP/a-files-$TAG.txt"
cd "$CLONE" || exit 1
xargs -n 100 sha256sum < "$TMP/a-files-$TAG.txt" > "$TMP/a-$TAG.sha" 2>/dev/null
echo "tag=$TAG files=$(wc -l < "$TMP/a-$TAG.sha")"
