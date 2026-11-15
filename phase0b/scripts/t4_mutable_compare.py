import sys
from pathlib import Path
from collections import defaultdict

base = Path(sys.argv[1]) if len(sys.argv) > 1 else (
    Path(__file__).resolve().parents[2] / "phase0b" / "work" / "t4")

def load_sha(p):
    d = {}
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line:
                continue
            h, _, path = line.partition("  ")
            d[path] = h
    return d

def load_sizes(p):
    d = {}
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line:
                continue
            path, _, s = line.partition("\t")
            try:
                d[path] = int(s)
            except ValueError:
                pass
    return d

before = load_sha(base / "a-before.sha")
after = load_sha(base / "a-after.sha")
sb = load_sizes(base / "a-sizes-before.txt")
sa = load_sizes(base / "a-sizes-after.txt")

changed = [p for p in before if p in after and before[p] != after[p]]
new = [p for p in after if p not in before]
deleted = [p for p in before if p not in after]

def folder(p):
    parts = p.split("/")
    return "/".join(parts[:4]) if len(parts) >= 4 else p

def total(d, paths):
    return sum(d.get(p, 0) for p in paths)

print(f"before_files={len(before)} after_files={len(after)}")
print(f"changed={len(changed)} new={len(new)} deleted={len(deleted)}")
print(f"changed_bytes={total(sa, changed)} new_bytes={total(sa, new)} deleted_bytes={total(sb, deleted)}")

print("\n== changed files (mutable in place) ==")
for p in sorted(changed):
    print(f"{sa.get(p,0)}\t{sb.get(p,0)}\t{p}")

print("\n== new files by folder (downloaded) ==")
agg = defaultdict(lambda: [0, 0])
for p in new:
    agg[folder(p)][0] += 1
    agg[folder(p)][1] += sa.get(p, 0)
for k in sorted(agg, key=lambda x: -agg[x][1]):
    print(f"{agg[k][1]}\t{agg[k][0]}\t{k}")

print("\n== deleted files ==")
for p in sorted(deleted):
    print(p)
