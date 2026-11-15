#!/system/bin/sh
# T4 stage 2a: clone A totals, download order (excluding the Phase 0 img copy).
HOST=/storage/emulated/0/Android/data/com.mobile.legends/files/dragon2017
CLONE=/storage/emulated/0/Android/data/top.niunaijun.blackbox/files/blackbox/storage/emulated/0/Android/data/com.mobile.legends

echo "== clone A totals =="
find "$CLONE" -type f -printf '%s\n' | awk '{n++; s+=$1} END {printf "files=%d bytes=%d MiB=%.1f\n", n, s, s/1048576}'
echo -n "folders="; find "$CLONE" -type d | wc -l

echo "== clone A per top-level folder (bytes files) =="
find "$CLONE" -type f -printf '%s\t%p\n' | awk -v c="$CLONE/" -F'\t' '
  { p=substr($2, length(c)+1); split(p, a, "/"); s[a[1]]+=$1; c2[a[1]]++ }
  END { for (k in s) printf "%d\t%d\t%s\n", s[k], c2[k], k }' | sort -k1 -n -r

echo "== img copy (excluded from list L) =="
find "$CLONE/files/dragon2017/assets/img" -type f -printf '%s\n' | awk '{n++; s+=$1} END {printf "files=%d bytes=%d MiB=%.1f\n", n, s, s/1048576}'

echo "== paths containing spaces? =="
find "$CLONE" -type f -name '* *' | wc -l

echo "== download order (mtime, size, relative path; img excluded) =="
find "$CLONE" -type f -not -path '*/assets/img/*' -printf '%T@\t%s\t%P\n' | sort -n

echo "== done =="
