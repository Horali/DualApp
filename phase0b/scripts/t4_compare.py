import sys
from pathlib import Path

base = Path(sys.argv[1]) if len(sys.argv) > 1 else (
    Path(__file__).resolve().parents[2] / "phase0b" / "work" / "t4")

def load(p):
    d = {}
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\n")
            if not line:
                continue
            h, _, path = line.partition("  ")
            d[path] = h
    return d

host = load(base / "host.sha")
clone = load(base / "clone.sha")

same = [p for p in host if p in clone and host[p] == clone[p]]
diff = [p for p in host if p in clone and host[p] != clone[p]]
only_host = [p for p in host if p not in clone]
only_clone = [p for p in clone if p not in host]

print(f"host_hashes={len(host)} clone_hashes={len(clone)}")
print(f"identical={len(same)} different={len(diff)} only_host={len(only_host)} only_clone={len(only_clone)}")
print("\n== different paths ==")
for p in sorted(diff):
    print(p)
print("\n== identical sample (first 20) ==")
for p in sorted(same)[:20]:
    print(p)
