#!/system/bin/sh
# T4 stage 2b: hash every non-img path present in both the host and clone A.
HOST=/storage/emulated/0/Android/data/com.mobile.legends/files/dragon2017
CLONE_D=/storage/emulated/0/Android/data/top.niunaijun.blackbox/files/blackbox/storage/emulated/0/Android/data/com.mobile.legends/files/dragon2017
TMP=/data/local/tmp/t4
mkdir -p "$TMP"

find "$CLONE_D" -type f -not -path '*/assets/img/*' -printf '%P\n' > "$TMP/rel.txt"
: > "$TMP/overlap.txt"
while IFS= read -r r; do
  [ -f "$HOST/$r" ] && echo "$r" >> "$TMP/overlap.txt"
done < "$TMP/rel.txt"

echo "non_img_files=$(wc -l < "$TMP/rel.txt")"
echo "overlap_files=$(wc -l < "$TMP/overlap.txt")"

cd "$HOST" || exit 1
xargs -n 100 sha256sum < "$TMP/overlap.txt" > "$TMP/host.sha" 2>/dev/null
cd "$CLONE_D" || exit 1
xargs -n 100 sha256sum < "$TMP/overlap.txt" > "$TMP/clone.sha" 2>/dev/null

sort -k2 "$TMP/host.sha" > "$TMP/host.sorted"
sort -k2 "$TMP/clone.sha" > "$TMP/clone.sorted"

echo "host_hashes=$(wc -l < "$TMP/host.sha")"
echo "clone_hashes=$(wc -l < "$TMP/clone.sha")"
echo "== done (pull host.sha/clone.sha and compare with t4_compare.py) =="
