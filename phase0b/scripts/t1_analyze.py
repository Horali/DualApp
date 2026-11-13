"""Phase 0b T1: compare NewBlackbox's framework expectations with the phone.

Usage: python -I t1_analyze.py <api.pickle> <engine_src_main> <out_dir> <baseline_out> [aosp_cache_dir]

Reads the class database from t1_dexdb.py and the engine sources
(Bcore/src/main), then writes:
  t1-framework-diff-api37.txt  sections A (implemented interfaces), B (local
                               AIDL copies), C (dynamic proxies), E (extended
                               classes) and the handleNewIntent check
  t1-mirror-check-api37.txt    section D (BlackReflection mirrors)
  t1-oem-additions-api37.txt   phone methods missing from AOSP main
  <baseline_out>               signatures of every watched type (API baseline)
The OEM scan needs the AOSP cache dir; files are fetched once into it.
"""
import base64
import concurrent.futures
import os
import pickle
import re
import sys
import urllib.error
import urllib.request

PRIMS = {"Z": "boolean", "B": "byte", "C": "char", "S": "short", "I": "int",
         "J": "long", "F": "float", "D": "double", "V": "void"}
PRIM_DESC = {v: k for k, v in PRIMS.items()}
JAVA_LANG = {"Object", "String", "Integer", "Long", "Boolean", "Byte", "Short",
             "Character", "Float", "Double", "Class", "CharSequence", "Runnable",
             "Throwable", "Exception", "Void", "Iterable", "Number", "ClassLoader",
             "Thread", "StringBuilder", "Process"}


# ---------------------------------------------------------------- descriptors
def split_params(desc):
    """'(Ljava/lang/String;I[J)V' -> ['Ljava/lang/String;', 'I', '[J']"""
    inner = desc[1:desc.index(")")]
    out, i = [], 0
    while i < len(inner):
        j = i
        while inner[j] == "[":
            j += 1
        if inner[j] == "L":
            j = inner.index(";", j)
        out.append(inner[i:j + 1])
        i = j + 1
    return out


def ret_type(desc):
    return desc[desc.index(")") + 1:]


def pretty(d):
    dims = 0
    while d.startswith("["):
        dims += 1
        d = d[1:]
    if d in PRIMS:
        base = PRIMS[d]
    else:
        base = d[1:-1].replace("/", ".")
    return base + "[]" * dims


def simple(d):
    """Descriptor -> simple name used for loose matching ('IBinder', 'int[]')."""
    p = pretty(d)
    dims = p.count("[]")
    base = p.replace("[]", "")
    base = base.split(".")[-1].split("$")[-1]
    return base + "[]" * dims


def sig(m):
    return "%s(%s)" % (m["name"], ", ".join(pretty(p) for p in split_params(m["type"])))


def cname(desc):
    return desc[1:-1].replace("/", ".")


def cdesc(name):
    return "L" + name.replace(".", "/") + ";"


# ---------------------------------------------------------------- class db
class DB:
    def __init__(self, path):
        with open(path, "rb") as fh:
            self.c = pickle.load(fh)

    def get(self, name):
        return self.c.get(cdesc(name))

    def is_iface(self, cls):
        return "INTERFACE" in cls["flags"]

    def chain(self, name):
        """Class and its superclasses (dicts), nearest first."""
        out = []
        cls = self.get(name)
        while cls:
            out.append(cls)
            cls = self.c.get(cls["super"]) if cls["super"] else None
        return out

    def all_ifaces(self, name, seen=None):
        seen = seen if seen is not None else set()
        cls = self.get(name)
        if not cls:
            return []
        out = []
        for i in cls["ifaces"]:
            n = cname(i)
            if n not in seen:
                seen.add(n)
                out.append(n)
                out.extend(self.all_ifaces(n, seen))
        return out


def real_methods(cls):
    return [m for m in cls["methods"]
            if "SYNTHETIC" not in m["access"] and "BRIDGE" not in m["access"]
            and "$" not in m["name"]]


# ---------------------------------------------------------------- java source helpers
def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", " ", src, flags=re.S)
    src = re.sub(r"//[^\n]*", " ", src)
    return src


def strip_strings(src):
    return re.sub(r'"(?:\\.|[^"\\])*"', '""', src)


def imports_of(src):
    imp = {}
    for full in re.findall(r"^\s*import\s+([\w.]+)\s*;", src, flags=re.M):
        imp[full.split(".")[-1]] = full
    pkg = re.search(r"^\s*package\s+([\w.]+)\s*;", src, flags=re.M)
    return imp, (pkg.group(1) if pkg else "")


def body_from(src, start):
    """Text between the '{' at/after start and its matching '}'."""
    i = src.index("{", start)
    depth = 0
    for j in range(i, len(src)):
        if src[j] == "{":
            depth += 1
        elif src[j] == "}":
            depth -= 1
            if depth == 0:
                return src[i + 1:j]
    return src[i + 1:]


def top_level(body):
    """Drop nested {...} blocks so only member declarations remain."""
    out, depth = [], 0
    for ch in body:
        if ch == "{":
            depth += 1
            if depth == 1:
                out.append("{}")
            continue
        if ch == "}":
            depth -= 1
            continue
        if depth == 0:
            out.append(ch)
    return "".join(out)


def strip_generics(t):
    prev = None
    while prev != t:
        prev = t
        t = re.sub(r"<[^<>]*>", "", t)
    return t


def java_param_simple(p):
    p = re.sub(r"@\w+(\([^)]*\))?", " ", p)
    p = re.sub(r"\b(final|in|out|inout)\b", " ", p)
    p = strip_generics(p).strip()
    if not p:
        return None
    toks = p.replace("...", "[] ").split()
    if len(toks) < 2:
        t = toks[0]
    else:
        t = " ".join(toks[:-1]).replace(" ", "")
    t = t.split(".")[-1]
    return t


DECL_RE = re.compile(r"([\w.<>\[\]?, ]+?)\s+(\w+)\s*\(([^()]*)\)\s*(?:throws\s+[\w., ]+)?\s*(\{\}|;)")


def java_methods(body):
    """[(name, [simple param types])] declared at the top level of a class body."""
    out = []
    for m in DECL_RE.finditer(top_level(body)):
        name = m.group(2)
        if name in ("if", "for", "while", "switch", "catch", "return", "new"):
            continue
        params = [x for x in (java_param_simple(p) for p in split_top(m.group(3))) if x]
        out.append((name, params))
    return out


def split_top(params):
    params = strip_generics(params)
    return [p for p in params.split(",") if p.strip()]


# ---------------------------------------------------------------- engine scan
class Engine:
    def __init__(self, root):
        self.root = root
        self.java = {}
        for base, _, files in os.walk(os.path.join(root, "java")):
            for f in files:
                if f.endswith(".java"):
                    p = os.path.join(base, f)
                    with open(p, encoding="utf-8", errors="replace") as fh:
                        self.java[p] = fh.read()

    def rel(self, p):
        return os.path.relpath(p, self.root).replace("\\", "/")

    def resolve(self, simple_or_full, src):
        imp, pkg = imports_of(src)
        head = simple_or_full.split(".")[0]
        if head in imp:
            return imp[head] + simple_or_full[len(head):]
        if "." in simple_or_full and simple_or_full[0].islower():
            return simple_or_full
        return (pkg + "." if pkg else "") + simple_or_full


# ---------------------------------------------------------------- section A
def section_a(db, eng, out, watched):
    out.append("=" * 78)
    out.append("A. Framework interfaces the engine IMPLEMENTS (Stub subclasses)")
    out.append("   Phone abstract method the engine does not implement = AbstractMethodError")
    out.append("   the first time the framework calls it.")
    out.append("=" * 78)
    impls = []
    for path, raw in eng.java.items():
        if "/black/" in path.replace("\\", "/"):
            continue
        src = strip_strings(strip_comments(raw))
        for m in re.finditer(r"class\s+(\w+)\s+extends\s+([\w.]+)\.Stub\b", src):
            impls.append((path, src, m.group(1), m.group(2), m.start()))
        for m in re.finditer(r"new\s+([\w.]+)\.Stub\s*\(\s*\)\s*\{", src):
            impls.append((path, src, "<anonymous>", m.group(1), m.start()))
    findings = []
    for path, src, cls_name, iface, pos in impls:
        full = eng.resolve(iface, src)
        icls = db.get(full)
        if not icls:
            continue  # engine's own AIDL (IB*), not a framework type
        watched.add(full)
        body = body_from(src, pos)
        have = java_methods(body)
        # methods inherited from an abstract parent declared in the same tree
        if cls_name != "<anonymous>":
            for p2, raw2 in eng.java.items():
                s2 = strip_strings(strip_comments(raw2))
                for m2 in re.finditer(r"class\s+(\w+)\s+extends\s+%s\b" % cls_name, s2):
                    have += java_methods(body_from(s2, m2.start()))
        have_set = {(n, tuple(p)) for n, p in have}
        have_names = {n for n, _ in have}
        line = src[:pos].count("\n") + 1
        out.append("")
        out.append("%s:%d  %s implements %s" % (eng.rel(path), line, cls_name, full))
        phone = []
        for iname in [full] + db.all_ifaces(full):
            if iname == "android.os.IInterface":
                continue
            c = db.get(iname)
            if c:
                phone += [m for m in real_methods(c) if "ABSTRACT" in m["access"]]
        for m in phone:
            key = (m["name"], tuple(simple(p) for p in split_params(m["type"])))
            ok = key in have_set
            mark = "ok      " if ok else "MISSING "
            out.append("   %s %s  [%s]" % (mark, sig(m), m["hidden"]))
            if not ok:
                findings.append(("A", full, sig(m), m["name"] in have_names))
        phone_keys = {(m["name"], tuple(simple(p) for p in split_params(m["type"]))) for m in phone}
        for n, p in sorted(have_set):
            if n in {m["name"] for m in phone} and (n, p) not in phone_keys:
                out.append("   extra    %s(%s)  (engine overload the phone no longer declares; never called)" % (n, ", ".join(p)))
    return findings


# ---------------------------------------------------------------- section B
def parse_aidl(text):
    text = strip_comments(text)
    pkg = re.search(r"package\s+([\w.]+)\s*;", text)
    pkg = pkg.group(1) if pkg else ""
    m = re.search(r"\b(?:oneway\s+)?interface\s+(\w+)\s*\{", text)
    if not m:
        p = re.search(r"\bparcelable\s+([\w.]+)", text)
        return ("parcelable", pkg + "." + p.group(1).replace(".", "$") if p else None, [])
    name = pkg + "." + m.group(1)
    body = body_from(text, m.start())
    methods = []
    for decl in body.split(";"):
        decl = " ".join(decl.split())
        mm = re.match(r"(?:oneway\s+)?([\w.<>\[\], ]+?)\s+(\w+)\s*\((.*)\)\s*$", decl)
        if not mm:
            continue
        params = [x for x in (java_param_simple(p) for p in split_top(mm.group(3))) if x]
        methods.append((mm.group(2), params))
    return ("interface", name, methods)


def section_b(db, eng, out, watched):
    out.append("")
    out.append("=" * 78)
    out.append("B. Local AIDL copies of android.* interfaces (Bcore/src/main/aidl/android)")
    out.append("   The phone's class wins at runtime. Differences matter where the engine")
    out.append("   implements the Stub (see A) or calls the method by the old signature.")
    out.append("=" * 78)
    findings = []
    aroot = os.path.join(eng.root, "aidl", "android")
    for base, _, files in os.walk(aroot):
        for f in sorted(files):
            if not f.endswith(".aidl"):
                continue
            p = os.path.join(base, f)
            with open(p, encoding="utf-8", errors="replace") as fh:
                kind, name, methods = parse_aidl(fh.read())
            rel = eng.rel(p)
            if kind == "parcelable":
                exists = bool(name and db.get(name))
                out.append("")
                out.append("%s  parcelable %s: %s" % (rel, name, "present" if exists else "NOT ON PHONE"))
                if not exists:
                    findings.append(("B", name, "parcelable missing", False))
                continue
            cls = db.get(name)
            out.append("")
            if not cls:
                out.append("%s  %s: NOT ON PHONE" % (rel, name))
                findings.append(("B", name, "interface missing", False))
                continue
            watched.add(name)
            phone = [m for m in real_methods(cls) if "ABSTRACT" in m["access"]]
            pkeys = {}
            for m in phone:
                pkeys.setdefault(m["name"], []).append(tuple(simple(x) for x in split_params(m["type"])))
            ckeys = {}
            for n, ps in methods:
                ckeys.setdefault(n, []).append(tuple(ps))
            diffs = []
            for n in sorted(set(pkeys) | set(ckeys)):
                pv, cv = set(pkeys.get(n, [])), set(ckeys.get(n, []))
                if pv == cv:
                    continue
                if not cv:
                    for s in sorted(pv):
                        diffs.append("   phone-only  %s(%s)" % (n, ", ".join(s)))
                elif not pv:
                    for s in sorted(cv):
                        diffs.append("   copy-only   %s(%s)  (removed from phone)" % (n, ", ".join(s)))
                else:
                    for s in sorted(pv - cv):
                        diffs.append("   CHANGED     %s(%s)  phone" % (n, ", ".join(s)))
                    for s in sorted(cv - pv):
                        diffs.append("               %s(%s)  copy" % (n, ", ".join(s)))
            out.append("%s  %s: %d phone methods, %d in copy, %s" % (
                rel, name, len(phone), len(methods), "identical" if not diffs else "%d differences" % len(diffs)))
            out.extend(diffs)
            if diffs:
                findings.append(("B", name, "%d differences" % len(diffs), False))
    return findings


# ---------------------------------------------------------------- section C
def mirror_index(eng):
    """BR-name -> framework class name, from @BClassName mirrors."""
    idx = {}
    for path, raw in eng.java.items():
        if "/black/" not in path.replace("\\", "/") or "/build/" in path.replace("\\", "/"):
            continue
        src = strip_comments(raw)
        stack = []
        depth = 0
        pending = None
        for tok in re.finditer(r'@BClassName\("([^"]+)"\)|\binterface\s+(\w+)|\{|\}', src):
            if tok.group(1):
                pending = tok.group(1)
            elif tok.group(2):
                stack.append((depth + 1, tok.group(2), pending))
                pending = None
            elif tok.group(0) == "{":
                depth += 1
            else:
                depth -= 1
                while stack and stack[-1][0] > depth:
                    stack.pop()
            if tok.group(2) and stack[-1][2]:
                idx["BR" + "".join(s[1] for s in stack)] = stack[-1][2]
    return idx


def section_c(db, eng, out, watched, midx):
    out.append("")
    out.append("=" * 78)
    out.append("C. Dynamic proxies (@ProxyMethod hooks) vs the phone's interfaces")
    out.append("   NOT ON PHONE = the hook never fires, so that call is not virtualized.")
    out.append("   ARG>=N = the hook reads args[i] past the phone's parameter count.")
    out.append("   CAST = the hook casts args[i] to a type the phone does not pass there.")
    out.append("=" * 78)
    findings = []
    # name -> set(interface) over all phone interfaces, for fallback matching
    by_method = {}
    for d, c in db.c.items():
        if "INTERFACE" in c["flags"] and "$" not in d:
            for m in c["methods"]:
                by_method.setdefault(m["name"], set()).add(cname(d))
    hooks_by_file = {}
    for path, raw in eng.java.items():
        if "/fake/" not in path.replace("\\", "/"):
            continue
        src = strip_strings(strip_comments(raw)).replace('""', '"_"')
        src2 = strip_comments(raw)
        hooks = []
        for m in re.finditer(r'@ProxyMethod\("([^"]+)"\)[^{]*?class\s+(\w+)', src2):
            body = body_from(src2, m.end())
            hooks.append((m.group(1), m.group(2), body))
        for m in re.finditer(r'@ProxyMethods\(\{([^}]*)\}\)[^{]*?class\s+(\w+)', src2):
            body = body_from(src2, m.end())
            for n in re.findall(r'"([^"]+)"', m.group(1)):
                hooks.append((n, m.group(2), body))
        if hooks:
            hooks_by_file[path] = (src2, hooks)
    # attach @ScanClass(Common.class) hook sets to their proxies
    proxies = []
    for path, (src, hooks) in hooks_by_file.items():
        cm = re.search(r"class\s+(\w+)\s+extends\s+(BinderInvocationStub|ClassInvocationStub)", src)
        if not cm:
            continue
        all_hooks = list(hooks)
        for sc in re.findall(r"@ScanClass\((\w+)\.class\)", src):
            for p2, (s2, h2) in hooks_by_file.items():
                if re.search(r"class\s+%s\b" % sc, s2):
                    all_hooks += h2
        proxies.append((path, src, cm.group(1), all_hooks))
    inert = []
    resolved = []
    for path, src, pname, hooks in sorted(proxies, key=lambda x: x[2]):
        names = {h[0] for h in hooks}
        gw = re.search(r"Object\s+getWho\s*\(\s*\)\s*\{", src)
        gwb = re.sub(r"\s*Slog\.\w+\([^;]*\);", "", body_from(src, gw.start())) if gw else ""
        if gw and re.match(r"\s*return\s+null\s*;", gwb):
            inert.append((eng.rel(path), pname))
            continue
        if pname in PROXY_NOT_FRAMEWORK:
            inert.append((eng.rel(path), pname + "  (" + PROXY_NOT_FRAMEWORK[pname] + ")"))
            continue
        cands = list(PROXY_TARGETS.get(pname, []))
        for br in re.findall(r"\b(BR\w+)\b", src):
            fw = midx.get(br)
            if fw:
                fw = fw[:-5] if fw.endswith("$Stub") else fw
                cands.append(fw)
        base = re.sub(r"(Proxy|Stub)$", "", pname)
        for d in db.c:
            n = cname(d)
            if n.split(".")[-1] == base and "INTERFACE" in db.c[d]["flags"]:
                cands.append(n)

        def score(n):
            c = db.get(n)
            if not c:
                return -1
            have = {m["name"] for m in c["methods"]}
            return len(names & have)
        cands = [c for c in dict.fromkeys(cands) if db.get(c)]
        best = cands[0] if pname in PROXY_TARGETS else (max(cands, key=score) if cands else None)
        if pname not in PROXY_TARGETS and (not best or score(best) < max(1, len(names) // 2)):
            tally = {}
            for n in names:
                for i in by_method.get(n, ()):
                    tally[i] = tally.get(i, 0) + 1
            if tally:
                alt = max(tally, key=lambda k: (tally[k], k.startswith("android.")))
                if not best or tally[alt] > score(best):
                    best = alt
        resolved.append((path, pname, hooks, best))
    active_targets = {r[3] for r in resolved if r[3]}
    for path, pname, hooks, best in resolved:
        out.append("")
        out.append("%s  %s -> %s" % (eng.rel(path), pname, best or "(target not resolved)"))
        if not best:
            findings.append(("C", pname, "target interface not resolved", False))
            continue
        watched.add(best)
        cls = db.get(best)
        targets = [cls] + [db.get(i) for i in db.all_ifaces(best) if db.get(i)]
        for hname, hcls, body in sorted(hooks, key=lambda h: h[0]):
            overloads = [m for t in targets for m in t["methods"] if m["name"] == hname]
            idxs = sorted({int(i) for i in re.findall(r"args\[(\d+)\]", body)})
            casts = re.findall(r"\(\s*([\w.]+(?:\[\])?)\s*\)\s*args\[(\d+)\]", body)
            casts += [(t, i) for i, t in re.findall(r"args\[(\d+)\]\s+instanceof\s+([\w.]+)", body)]
            flags = []
            if not overloads:
                elsewhere = sorted(i for i in by_method.get(hname, ()) if i in active_targets)
                if elsewhere:
                    flags.append("NOT ON PHONE (moved; also hooked on %s)" % ", ".join(elsewhere))
                else:
                    other = sorted(by_method.get(hname, ()))
                    other = [o for o in other if o.startswith(("android.", "com.android."))][:3]
                    flags.append("NOT ON PHONE (legacy hook%s)" % ("; name now on " + ", ".join(other) if other else ""))
            else:
                minp = min(len(split_params(m["type"])) for m in overloads)
                maxp = max(len(split_params(m["type"])) for m in overloads)
                guarded = re.search(r"args\.length\s*[<>]=?|args\s*==\s*null|args\.length\s*==", body)
                if idxs and idxs[-1] >= maxp:
                    flags.append("ARG>=N(args[%d] vs %d params)" % (idxs[-1], maxp))
                elif idxs and idxs[-1] >= minp and not guarded:
                    flags.append("ARG>=N on some overloads(args[%d] vs min %d)" % (idxs[-1], minp))
                for t, i in casts:
                    i = int(i)
                    tsimple = t.split(".")[-1]
                    if tsimple in ("Object",):
                        continue
                    types = {simple(split_params(m["type"])[i]) for m in overloads
                             if i < len(split_params(m["type"]))}
                    if types and tsimple not in types and not (tsimple in ("int", "Integer") and types & {"int", "Integer"}) \
                            and not (tsimple in ("long", "Long") and types & {"long", "Long"}) \
                            and not (tsimple in ("boolean", "Boolean") and types & {"boolean", "Boolean"}) \
                            and not (tsimple in ("String",) and types & {"String", "CharSequence"}):
                        flags.append("CAST args[%d] %s vs phone %s" % (i, tsimple, "/".join(sorted(types))))
            out.append("   %-34s args%s %s" % (hname, idxs if idxs else "[]", " ".join(flags) if flags else "ok"))
            for m in overloads:
                out.append("        phone: %s" % sig(m))
            if flags:
                findings.append(("C", best, "%s: %s" % (hname, " ".join(flags)), False))
    out.append("")
    out.append("Inactive proxies (getWho() returns null, so nothing is installed) or")
    out.append("proxies of non-framework objects; out of scope for a framework diff:")
    for rel, pname in inert:
        out.append("   %s  %s" % (rel, pname))
    return findings


# Proxies whose target the name/mirror heuristic gets wrong: set by reading getWho().
PROXY_TARGETS = {
    "IActivityClientProxy": ["android.app.IActivityClientController"],
    "IActivityTaskManagerProxy": ["android.app.IActivityTaskManager"],
    "IAppOpsManagerProxy": ["com.android.internal.app.IAppOpsService"],
    "IDisplayManagerProxy": ["android.hardware.display.IDisplayManager"],
    "ITelephonyRegistryProxy": ["com.android.internal.telephony.ITelephonyRegistry"],
    "IActivityManagerProxy": ["android.app.IActivityManager"],
    "IJobServiceProxy": ["android.app.job.IJobScheduler"],
    "ContentServiceStub": ["android.content.IContentService"],
    "AudioPermissionProxy": ["android.media.IAudioService"],
}
PROXY_NOT_FRAMEWORK = {
    "WorkManagerProxy": "androidx.work, an app library",
    "GoogleAccountManagerProxy": "Google Play services object, not a framework binder",
    "ContentResolverProxy": "ContentResolver is a class, not an interface",
    "GmsProxy": "service 'gms' does not exist in ServiceManager",
    "IMiuiSecurityManagerProxy": "Xiaomi-only service",
}


# ---------------------------------------------------------------- section D
MEMBER_ANN = re.compile(r"@(BField|BStaticField|BMethod|BStaticMethod|BConstructor)\b")


def java_type_desc(t, src_imports, pkg):
    t = strip_generics(t).strip()
    dims = t.count("[]") + t.count("...")
    t = t.replace("[]", "").replace("...", "").strip()
    if t in PRIM_DESC:
        d = PRIM_DESC[t]
    else:
        head = t.split(".")[0]
        if head in src_imports:
            full = src_imports[head] + t[len(head):]
        elif t in JAVA_LANG:
            full = "java.lang." + t
        elif "." in t and t[0].islower():
            full = t
        else:
            full = t  # unresolved simple name
        # nested classes written as Outer.Inner after the import
        parts = full.split(".")
        for k in range(len(parts) - 1, 0, -1):
            if parts[k - 1][:1].isupper():
                parts[k - 1] = parts[k - 1] + "$" + parts[k]
                del parts[k]
        d = "L" + "/".join(parts) + ";"
    return "[" * dims + d


def parse_mirrors(eng):
    """[(file, line, framework class, kind, name, [param descs] or None)]"""
    out = []
    for path, raw in eng.java.items():
        p = path.replace("\\", "/")
        if "/black/" not in p:
            continue
        src = strip_comments(raw)
        imports, pkg = imports_of(src)
        stack = []
        depth = 0
        pending = None
        pos = 0
        toks = list(re.finditer(r'@BClassName\("([^"]+)"\)|\binterface\s+(\w+)|\{|\}|' + MEMBER_ANN.pattern, src))
        for tok in toks:
            if tok.group(1):
                pending = tok.group(1)
            elif tok.group(2):
                stack.append((depth + 1, pending, tok.group(2)))
                pending = None
            elif tok.group(0) == "{":
                depth += 1
            elif tok.group(0) == "}":
                depth -= 1
                while stack and stack[-1][0] > depth:
                    stack.pop()
            else:
                kind = tok.group(3)
                fw = stack[-1][1] if stack else None
                if not fw:
                    continue
                end = src.index(";", tok.end())
                decl = " ".join(src[tok.end():end].split())
                line = src[:tok.start()].count("\n") + 1
                m = re.match(r"(.*?)\b(\w+)\s*\((.*)\)\s*$", decl)
                if not m:
                    continue
                name = m.group(2)
                params = None
                if kind in ("BMethod", "BStaticMethod", "BConstructor"):
                    params = []
                    for prm in split_top(m.group(3)):
                        pn = re.search(r'@BParamClassName\("([^"]+)"\)', prm)
                        pc = re.search(r'@BParamClass\(([\w.]+)\.class\)', prm)
                        if pn:
                            params.append(cdesc(pn.group(1)))
                            continue
                        prm2 = re.sub(r"@\w+(\([^)]*\))?", " ", prm).replace("final ", " ").strip()
                        if pc:
                            prm2 = pc.group(1) + " x"
                        toks2 = prm2.replace("...", "[] ").split()
                        t = "".join(toks2[:-1]) if len(toks2) > 1 else toks2[0]
                        params.append(java_type_desc(t, imports, pkg))
                br = "BR" + "".join(x[2] for x in stack)
                out.append((eng.rel(path), line, fw, kind, name, params, br))
    return out


def section_d(db, eng, out, watched):
    out.append("=" * 78)
    out.append("D. BlackReflection mirrors (@BClassName / @BField / @BMethod ...) vs the phone")
    out.append("   Lookup rules copied from black-reflection Reflector: fields and methods")
    out.append("   are searched up the superclass chain, methods need exact parameter types.")
    out.append("   A missing member returns null / does nothing at runtime.")
    out.append("=" * 78)
    mirrors = parse_mirrors(eng)
    classes = sorted({m[2] for m in mirrors})
    findings = []
    missing_classes = []
    for c in classes:
        if db.get(c):
            watched.add(c)
        else:
            missing_classes.append(c)
    out.append("")
    out.append("Mirrored framework classes: %d (present %d, missing %d)" % (
        len(classes), len(classes) - len(missing_classes), len(missing_classes)))
    for c in missing_classes:
        out.append("   CLASS NOT ON PHONE  %s" % c)
    ok = bad = 0
    lines = []
    users = {}
    for path, raw in eng.java.items():
        if "/black/" in path.replace("\\", "/"):
            continue
        for i, ln in enumerate(raw.splitlines(), 1):
            if ln.strip().startswith(("//", "*", "import ")):
                continue
            for br in re.findall(r"\b(BR\w+)\b", ln):
                users.setdefault(br, []).append((eng.rel(path), i, ln))
    for rel, line, fw, kind, name, params, br in sorted(mirrors, key=lambda x: (x[2], x[4])):
        chain = db.chain(fw)
        if not chain:
            continue
        found = None
        if kind in ("BField", "BStaticField"):
            for c in chain:
                for f in c["fields"]:
                    if f["name"] == name:
                        found = "%s %s.%s" % (pretty(f["type"]), cname(c["desc"]), name)
                        break
                if found:
                    break
            if not found:  # getField() also sees interface constants
                for i in db.all_ifaces(fw):
                    c = db.get(i)
                    for f in (c["fields"] if c else []):
                        if f["name"] == name:
                            found = "%s %s.%s" % (pretty(f["type"]), i, name)
        else:
            want = name if kind != "BConstructor" else "<init>"
            search = chain[:1] if kind == "BConstructor" else chain + [db.get(i) for i in db.all_ifaces(fw) if db.get(i)]
            same_name = []
            for c in search:
                for m in c["methods"]:
                    if m["name"] == want:
                        same_name.append((c, m))
                        if split_params(m["type"]) == params:
                            found = "%s.%s" % (cname(c["desc"]), sig(m))
                            break
                if found:
                    break
        if found:
            ok += 1
            continue
        bad += 1
        desc = name if params is None else "%s(%s)" % (name, ", ".join(pretty(p) for p in params))
        lines.append("   MISSING %-13s %s.%s   (%s:%d)" % (kind, fw, desc, rel, line))
        if params is not None:
            for c, m in same_name[:6]:
                lines.append("              phone has: %s.%s" % (cname(c["desc"]), sig(m)))
        member = "_new" if kind == "BConstructor" else name
        calls = [(r, i) for r, i, ln in users.get(br, []) if re.search(r"\.%s\s*\(" % re.escape(member), ln)]
        if calls:
            for r, i in calls[:4]:
                lines.append("              used at %s:%d" % (r, i))
        else:
            lines.append("              not called from engine code")
        findings.append(("D", fw, "%s %s" % (kind, desc), rel, line, len(calls)))
    out.append("Members on present classes: %d found, %d missing" % (ok, bad))
    out.append("")
    out.extend(lines)
    return findings, mirrors


# ---------------------------------------------------------------- section E
def section_e(db, eng, out, watched):
    out.append("")
    out.append("=" * 78)
    out.append("E. Framework classes the engine EXTENDS / drives (Instrumentation, ActivityThread,")
    out.append("   ClientTransactionHandler). Methods the engine does not override run the")
    out.append("   base (framework) version on the delegate, not on the wrapped original.")
    out.append("=" * 78)
    findings = []
    have = []
    for path, raw in eng.java.items():
        if re.search(r"(BaseInstrumentationDelegate|AppInstrumentation)\.java$", path):
            src = strip_strings(strip_comments(raw))
            m = re.search(r"class\s+\w+\s+extends\s+\w+", src)
            have += java_methods(body_from(src, m.start()))
    have_set = {(n, tuple(p)) for n, p in have}
    name = "android.app.Instrumentation"
    watched.add(name)
    inst = db.get(name)
    out.append("")
    out.append("android.app.Instrumentation (BaseInstrumentationDelegate + AppInstrumentation)")
    nf = []
    for m in real_methods(inst):
        if m["name"] in ("<init>", "<clinit>") or "PRIVATE" in m["access"] or "STATIC" in m["access"] \
                or "FINAL" in m["access"] or "CONSTRUCTOR" in m["access"]:
            continue
        key = (m["name"], tuple(simple(p) for p in split_params(m["type"])))
        st = "overridden" if key in have_set else "NOT FORWARDED"
        nf.append((st, m))
    for st, m in sorted(nf, key=lambda x: (x[0], x[1]["name"])):
        out.append("   %-13s %s  [%s]" % (st, sig(m), m["hidden"]))
    for st, m in nf:
        if st != "overridden":
            findings.append(("E", name, sig(m), m["hidden"]))
    for cls in ("android.app.ActivityThread", "android.app.ClientTransactionHandler"):
        watched.add(cls)
    # handleNewIntent check
    out.append("")
    out.append("handleNewIntent: the engine's reflection forms (BActivityThread.handleNewIntent)")
    at = db.get("android.app.ActivityThread")
    for m in real_methods(at):
        if m["name"] in ("handleNewIntent", "performNewIntents"):
            out.append("   phone ActivityThread: %s  [%s]" % (sig(m), m["access"]))
    cth = db.get("android.app.ClientTransactionHandler")
    for m in real_methods(cth):
        if m["name"] in ("handleNewIntent", "performNewIntents"):
            out.append("   phone ClientTransactionHandler: %s  [%s]" % (sig(m), m["access"]))
    forms = [("performNewIntents", ["Landroid/os/IBinder;", "Ljava/util/List;"]),
             ("performNewIntents", ["Landroid/os/IBinder;", "Ljava/util/List;", "Z"]),
             ("handleNewIntent", ["Landroid/os/IBinder;", "Ljava/util/List;"])]
    matched = []
    for n, ps in forms:
        hit = any(m["name"] == n and split_params(m["type"]) == ps for c in db.chain("android.app.ActivityThread") for m in c["methods"])
        out.append("   engine form %s(%s): %s" % (n, ", ".join(pretty(p) for p in ps), "MATCHES" if hit else "no match"))
        if hit:
            matched.append(n)
    findings.append(("E-newintent", "android.app.ActivityThread", "matched: %s" % (matched or "none"), ""))
    return findings


# ---------------------------------------------------------------- OEM scan
AOSP = "https://android.googlesource.com/"
REPOS = [
    ("platform/frameworks/base", ["core/java", "graphics/java", "location/java", "media/java",
                                  "telephony/java", "telecomm/java", "wifi/java", "keystore/java",
                                  "apex/jobscheduler/framework/java", "nfc/java", "services/core/java",
                                  "apex/media/framework/java", "sensor/java", "packages/SettingsProvider/src"]),
    ("platform/packages/modules/Connectivity", ["framework/src", "framework-t/src", "framework/aidl-export", "nearby/framework/java"]),
    ("platform/packages/modules/Wifi", ["framework/java"]),
    ("platform/packages/modules/Permission", ["framework-s/java", "PermissionController/src"]),
    ("platform/packages/modules/Bluetooth", ["framework/java"]),
    ("platform/frameworks/libcore", ["luni/src/main/java", "ojluni/src/main/java", "dalvik/src/main/java"]),
    ("platform/packages/modules/StatsD", ["framework/java"]),
    ("platform/packages/modules/Uwb", ["framework/java"]),
    ("platform/packages/modules/Virtualization", ["java/framework/src"]),
    ("platform/packages/modules/Media", ["apex/framework/java"]),
    ("platform/packages/modules/Permission", ["framework/java"]),
]


JAR_REPOS = {
    "framework-wifi": [("platform/packages/modules/Wifi", ["framework/java"])],
    "framework-connectivity": [("platform/packages/modules/Connectivity", ["framework/src", "framework/aidl-export"])],
    "framework-connectivity-t": [("platform/packages/modules/Connectivity", ["framework-t/src"])],
    "framework-connectivity-b": [("platform/packages/modules/Connectivity", ["framework-b/src"])],
    "framework-bluetooth": [("platform/packages/modules/Bluetooth", ["framework/java"])],
    "framework-permission": [("platform/packages/modules/Permission", ["framework/java"])],
    "framework-permission-s": [("platform/packages/modules/Permission", ["framework-s/java"])],
    "framework-nfc": [("platform/packages/modules/Nfc", ["framework/java"]), ("platform/frameworks/base", ["nfc/java"])],
    "framework-telephony": [("platform/frameworks/base", ["telephony/java"])],
    "framework-scheduling": [("platform/packages/modules/Scheduling", ["framework/java"])],
    "updatable-media": [("platform/packages/modules/Media", ["apex/framework/java"]), ("platform/frameworks/base", ["media/java"])],
}
_last = [0.0]


def fetch(url):
    """GET a gitiles ?format=TEXT file. None on 404. Waits out 429s."""
    import time
    for attempt in range(8):
        wait = 1.2 - (time.time() - _last[0])
        if wait > 0:
            time.sleep(wait)
        _last[0] = time.time()
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "t1-framework-diff"})
            with urllib.request.urlopen(req, timeout=30) as r:
                return base64.b64decode(r.read()).decode("utf-8", "replace")
        except urllib.error.HTTPError as e:
            if e.code == 404:
                return None
            if e.code == 429:
                time.sleep(30 * (attempt + 1))
                continue
            raise
        except urllib.error.URLError:
            time.sleep(10)
    raise RuntimeError("gave up on " + url)


def aosp_source(cls, jar, cache):
    outer = cls.split("$")[0]
    path = outer.replace(".", "/")
    key = os.path.join(cache, outer + ".src")
    if os.path.exists(key):
        with open(key, encoding="utf-8") as fh:
            t = fh.read()
        return None if t == "<<none>>" else t
    short = jar.split("__")[-1]
    repos = JAR_REPOS.get(short, []) + [r for r in REPOS if r[0] == "platform/frameworks/base"]
    tried = set()
    for repo, roots in repos:
        for root in roots:
            for ext in (".java", ".aidl"):
                url = "%s%s/+/refs/heads/main/%s/%s%s?format=TEXT" % (AOSP, repo, root, path, ext)
                if url in tried:
                    continue
                tried.add(url)
                t = fetch(url)
                if t:
                    with open(key, "w", encoding="utf-8") as fh:
                        fh.write("// " + url + chr(10) + t)
                    return t
    with open(key, "w", encoding="utf-8") as fh:
        fh.write("<<none>>")
    return None


def oem_scan(db, watched, implemented, proxied, extended, out, cache):
    out.append("=" * 78)
    out.append("OEM scan: phone methods whose NAME does not appear in the AOSP main source")
    out.append("of that type (android.googlesource.com, refs/heads/main, fetched 2026-11-14).")
    out.append("These are Samsung / One UI additions, or Android 17 changes not yet public.")
    out.append("Name-level check: a new overload of an existing AOSP name is not caught here")
    out.append("(the A/B/C sections compare full signatures for the types that matter).")
    out.append("=" * 78)
    os.makedirs(cache, exist_ok=True)
    types = sorted(t for t in watched if db.get(t) and t.startswith(("android.", "com.android.")))
    srcs = {}
    for k, t in enumerate(types):
        srcs[t] = aosp_source(t, db.get(t)["jar"], cache)
        if k % 20 == 0:
            print("aosp %d/%d" % (k, len(types)), file=sys.stderr, flush=True)
    findings = []
    nosrc = []
    for t in types:
        src = srcs[t]
        if src is None:
            nosrc.append(t)
            continue
        words = set(re.findall(r"\w+", src))
        cls = db.get(t)
        extra = [m for m in real_methods(cls) if m["name"] not in words and m["name"] not in ("<init>", "<clinit>")]
        extra_f = [f for f in cls["fields"] if f["name"] not in words and "SYNTHETIC" not in f["access"] and "$" not in f["name"]]
        if not extra and not extra_f:
            continue
        role = "implements" if t in implemented else "proxied" if t in proxied else "extends" if t in extended else "mirror"
        out.append("")
        out.append("%s  (%s, %s)" % (t, role, cls["jar"]))
        for m in extra:
            abstract = "ABSTRACT" in m["access"]
            if role == "implements" and abstract:
                cl = "CRASH (abstract, engine does not implement)"
            elif role == "extends" and "FINAL" not in m["access"] and "STATIC" not in m["access"] and "PRIVATE" not in m["access"]:
                cl = "SILENT (not forwarded by delegate)"
            elif role == "proxied":
                cl = "PASS-THROUGH (not virtualized; review)"
            else:
                cl = "harmless (engine never calls it)"
            sam = " samsung-prefix" if re.match(r"(?i)sem|samsung|sec|knox|dex", m["name"]) else ""
            out.append("   %-44s %s%s  [%s]" % (sig(m), cl, sam, m["hidden"]))
            findings.append((t, role, sig(m), cl))
        for f in extra_f:
            out.append("   field %s %s  harmless (not read by the engine)" % (pretty(f["type"]), f["name"]))
    out.append("")
    out.append("Types whose AOSP source could not be located (not scanned): %d" % len(nosrc))
    for t in nosrc:
        out.append("   %s" % t)
    return findings


# ---------------------------------------------------------------- baseline
def baseline(db, watched, path):
    rows = ["# API 37 baseline: framework signatures of every type NewBlackbox implements,",
            "# proxies, mirrors or extends. Source: phone BOOTCLASSPATH jars, dexdump.",
            "# Device: SM-S936B S936BXXUDDZJB (Android 17, API 37, One UI 9).",
            "# Format: class|kind|signature|return/type|access|hiddenapi",
            ]
    for t in sorted(watched):
        c = db.get(t)
        if not c:
            continue
        rows.append("%s|class|%s|super=%s|%s|%s" % (t, c["jar"], cname(c["super"]) if c["super"] else "-", c["flags"],
                                                     ",".join(cname(i) for i in c["ifaces"])))
        for f in sorted(c["fields"], key=lambda f: f["name"]):
            if "SYNTHETIC" in f["access"]:
                continue
            rows.append("%s|field|%s|%s|%s|%s" % (t, f["name"], pretty(f["type"]), f["access"], f["hidden"]))
        for m in sorted(real_methods(c), key=lambda m: (m["name"], m["type"])):
            rows.append("%s|method|%s|%s|%s|%s" % (t, sig(m), pretty(ret_type(m["type"])), m["access"], m["hidden"]))
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(rows) + "\n")
    return len(rows)


# ---------------------------------------------------------------- main
def main():
    pickle_path, src_main, out_dir, base_out = sys.argv[1:5]
    cache = sys.argv[5] if len(sys.argv) > 5 else None
    db = DB(pickle_path)
    eng = Engine(src_main)
    os.makedirs(out_dir, exist_ok=True)
    head = ["# Phase 0b T1, generated by phase0b/scripts/t1_analyze.py",
            "# Phone: SM-S936B S936BXXUDDZJB, Android 17 / API 37, One UI 9",
            "# Engine: NewBlackbox 89b5983 + patch 0001", ""]
    watched, implemented, proxied, extended = set(), set(), set(), set()

    out = list(head)
    w = set()
    fa = section_a(db, eng, out, w)
    implemented |= w
    watched |= w
    w = set()
    fb = section_b(db, eng, out, w)
    watched |= w
    w = set()
    fc = section_c(db, eng, out, w, mirror_index(eng))
    proxied |= w
    watched |= w
    w = set()
    fe = section_e(db, eng, out, w)
    extended |= {"android.app.Instrumentation"}
    watched |= w
    with open(os.path.join(out_dir, "t1-framework-diff-api37.txt"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(out) + "\n")

    out = list(head)
    w = set()
    fd, _ = section_d(db, eng, out, w)
    watched |= w
    with open(os.path.join(out_dir, "t1-mirror-check-api37.txt"), "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(out) + "\n")

    # supertypes of implemented / extended types are watched too
    for t in list(implemented | extended):
        for c in db.chain(t):
            watched.add(cname(c["desc"]))
        watched.update(db.all_ifaces(t))
    watched.discard("java.lang.Object")
    n = baseline(db, watched, base_out)

    fo = []
    if cache:
        out = list(head)
        fo = oem_scan(db, watched, implemented, proxied, extended, out, cache)
        with open(os.path.join(out_dir, "t1-oem-additions-api37.txt"), "w", encoding="utf-8", newline="\n") as fh:
            fh.write("\n".join(out) + "\n")

    with open(os.path.join(os.path.dirname(os.path.abspath(pickle_path)), "t1-findings.pickle"), "wb") as fh:
        pickle.dump({"A": fa, "B": fb, "C": fc, "D": fd, "E": fe, "OEM": fo}, fh)
    print("A missing:", len(fa), " B diffs:", len(fb), " C flags:", len(fc), " D missing:", len(fd),
          " E not forwarded:", len(fe) - 1, " OEM:", len(fo), " baseline rows:", n, " watched:", len(watched))


if __name__ == "__main__":
    main()
