#!/system/bin/sh
# T4 stage 1: host MLBB tree stats, size histogram, content types.
# Runs as shell (UID 2000), which can read the host MLBB external tree.
HOST=/storage/emulated/0/Android/data/com.mobile.legends/files/dragon2017

echo "== totals =="
find "$HOST" -type f -printf '%s\n' | awk '{n++; s+=$1} END {printf "files=%d bytes=%d MiB=%.1f\n", n, s, s/1048576}'
echo -n "folders="; find "$HOST" -type d | wc -l

echo "== size histogram =="
find "$HOST" -type f -printf '%s\n' | awk '
  $1 < 4096 {a++}
  $1 >= 4096 && $1 < 65536 {b++}
  $1 >= 65536 && $1 < 1048576 {c++}
  $1 >= 1048576 && $1 < 16777216 {d++}
  $1 >= 16777216 && $1 < 268435456 {e++}
  $1 >= 268435456 {f++}
  END {
    printf "<4KiB %d\n4-64KiB %d\n64KiB-1MiB %d\n1-16MiB %d\n16-256MiB %d\n>256MiB %d\n", a, b, c, d, e, f
  }'

echo "== per top-level folder (bytes files) =="
find "$HOST" -type f -printf '%s\t%p\n' | awk -v h="$HOST/" -F'\t' '
  { p=substr($2, length(h)+1); split(p, a, "/"); s[a[1]]+=$1; c[a[1]]++ }
  END { for (k in s) printf "%d\t%d\t%s\n", s[k], c[k], k }' | sort -k1 -n -r

echo "== content types (files >= 64 KiB) =="
find "$HOST" -type f -size +64k -printf '%p\n' | while IFS= read -r f; do
  m=$(od -An -tx1 -N8 "$f" 2>/dev/null | tr -d ' \n')
  case "$m" in
    7f454c46*) echo "ELF" ;;
    6465780a*) echo "dex" ;;
    504b0304*) echo "zip" ;;
    556e6974794653*) echo "UnityFS" ;;
    *) echo "other" ;;
  esac
done | sort | uniq -c | sort -k2

echo "== content types per top-level folder =="
find "$HOST" -type f -size +64k -printf '%p\n' | while IFS= read -r f; do
  m=$(od -An -tx1 -N8 "$f" 2>/dev/null | tr -d ' \n')
  case "$m" in
    7f454c46*) t=ELF ;;
    6465780a*) t=dex ;;
    504b0304*) t=zip ;;
    556e6974794653*) t=UnityFS ;;
    *) t=other ;;
  esac
  r=${f#$HOST/}
  echo "$t ${r%%/*}"
done | sort | uniq -c | sort -k2,2 -k1,1nr

echo "== done =="
