"""Parse `dexdump` text output into a class database (Phase 0b T1).

Usage: python -I t1_dexdb.py <dex_dump_root> <out.pickle>

<dex_dump_root> holds one folder per pulled jar, each with classesN.txt files
written by build-tools `dexdump`. The first definition of a class wins, in
BOOTCLASSPATH order (the order file is <dex_dump_root>/../bootclasspath.txt).
"""
import os
import pickle
import re
import sys

CLASS_RE = re.compile(r"^  Class descriptor  : '(.+)'$")
FLAGS_RE = re.compile(r"^  Access flags      : 0x[0-9a-f]+ \((.*)\)$")
SUPER_RE = re.compile(r"^  Superclass        : '(.+)'$")
IFACE_RE = re.compile(r"^    #\d+\s+: '(.+)'$")
SECTION_RE = re.compile(r"^  (Interfaces|Static fields|Instance fields|Direct methods|Virtual methods)\s+-$")
MEMBER_RE = re.compile(r"^    #\d+\s+: \(in .+\)$")
NAME_RE = re.compile(r"^      name          : '(.+)'$")
TYPE_RE = re.compile(r"^      type          : '(.+)'$")
ACCESS_RE = re.compile(r"^      access        : 0x[0-9a-f]+ \((.*)\)$")
HIDDEN_RE = re.compile(r"^      hiddenapi     : 0x[0-9a-f]+ \((.*)\)$")


def parse_file(path, jar, db):
    cur = None
    section = None
    member = None
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            line = line.rstrip("\n")
            m = CLASS_RE.match(line)
            if m:
                desc = m.group(1)
                if desc in db:  # earlier jar on the classpath wins
                    cur = None
                else:
                    cur = {"desc": desc, "jar": jar, "flags": "", "super": None,
                           "ifaces": [], "fields": [], "methods": []}
                    db[desc] = cur
                section = None
                member = None
                continue
            if cur is None:
                continue
            m = SECTION_RE.match(line)
            if m:
                section = m.group(1)
                member = None
                continue
            if section is None:
                m = FLAGS_RE.match(line)
                if m:
                    cur["flags"] = m.group(1)
                    continue
                m = SUPER_RE.match(line)
                if m:
                    cur["super"] = m.group(1)
                continue
            if section == "Interfaces":
                m = IFACE_RE.match(line)
                if m:
                    cur["ifaces"].append(m.group(1))
                continue
            if MEMBER_RE.match(line):
                member = {"name": None, "type": None, "access": "", "hidden": "",
                          "kind": section}
                if section in ("Static fields", "Instance fields"):
                    cur["fields"].append(member)
                else:
                    cur["methods"].append(member)
                continue
            if member is None:
                continue
            for rx, key in ((NAME_RE, "name"), (TYPE_RE, "type"),
                            (ACCESS_RE, "access"), (HIDDEN_RE, "hidden")):
                m = rx.match(line)
                if m:
                    member[key] = m.group(1)
                    break


def main():
    root, out = sys.argv[1], sys.argv[2]
    order_file = os.path.join(os.path.dirname(os.path.abspath(root)), "bootclasspath.txt")
    jars = []
    with open(order_file) as fh:
        for p in fh:
            p = p.strip()
            if p:
                jars.append(p.lstrip("/").replace("/", "__")[:-4])
    db = {}
    for jar in jars:
        d = os.path.join(root, jar)
        if not os.path.isdir(d):
            print("missing dump:", jar, file=sys.stderr)
            continue
        files = sorted(f for f in os.listdir(d) if f.endswith(".txt"))
        # classes.txt, classes2.txt, ... in numeric order
        files.sort(key=lambda f: int(re.sub(r"\D", "", f) or 1))
        for f in files:
            parse_file(os.path.join(d, f), jar, db)
    with open(out, "wb") as fh:
        pickle.dump(db, fh)
    print("classes:", len(db))


if __name__ == "__main__":
    main()
