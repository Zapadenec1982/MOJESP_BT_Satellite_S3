"""Worst-case stack chains from GCC -fcallgraph-info=su output (.ci files, VCG format).

usage: ci_walk.py <dir with .ci files> <root> [--stop <fn>=<bytes>]... [--via <fn>]

<root> and <fn> match a symbol (mangled) or its pretty name by substring.
Walks every call edge from the root, summing static frame sizes, and prints the deepest chain.
Functions with no frame size in any .ci (IDF, newlib, FreeRTOS unless their .ci is present)
count as 0 and are listed, so the reader can see what the number leaves out.
  --stop cover_soft_decode=8752   replace the subtree under a function by a known figure
  --via cover_set_jpeg            only paths that pass through this function
"""
import glob
import os
import re
import sys

ci_dir, root_pat = sys.argv[1], sys.argv[2]
stops, via = {}, None
args = sys.argv[3:]
while args:
    if args[0] == "--stop":
        k, v = args[1].split("=")
        stops[k] = int(v)
    elif args[0] == "--via":
        via = args[1]
    else:
        raise SystemExit("bad arg " + args[0])
    args = args[2:]

node_re = re.compile(r'^node: \{ title: "([^"]*)" label: "([^"]*)"')
edge_re = re.compile(r'^edge: \{ sourcename: "([^"]*)" targetname: "([^"]*)"')
frames, labels, edges = {}, {}, {}


def key_of(title):
    # "D:/path/file.cpp:sym" for a definition with internal linkage, bare "sym" otherwise.
    m = re.match(r"^[A-Za-z]:/.*?:([^:/]+)$", title)
    return m.group(1) if m else title


for path in glob.glob(os.path.join(ci_dir, "*.ci")):
    for line in open(path, encoding="utf-8", errors="replace"):
        m = node_re.match(line)
        if m:
            k = key_of(m.group(1))
            parts = m.group(2).split("\\n")
            if re.search(r"\w", parts[0]) and k not in labels:   # GCC labels esp_log as ")"
                labels[k] = parts[0]
            for p in parts[1:]:
                b = re.match(r"(\d+) bytes \((static|dynamic|bounded)", p)
                if b:
                    if b.group(2) != "static":
                        print(f"note: {parts[0]} frame is {b.group(2)}")
                    frames[k] = max(frames.get(k, 0), int(b.group(1)))
            continue
        m = edge_re.match(line)
        if m:
            edges.setdefault(key_of(m.group(1)), set()).add(key_of(m.group(2)))


def name(k):
    return labels.get(k, k)


def matches(k, pat):
    return pat in k or pat in name(k)


roots = [k for k in frames if matches(k, root_pat)]
if not roots:
    raise SystemExit("no root matching " + root_pat)
externals = set()


def stop_cost(k):
    for s, v in stops.items():
        if matches(k, s):
            return v
    return None


memo = {}


def walk(k, seen):
    """Deepest path from k: (bytes, [lines])."""
    sc = stop_cost(k)
    if sc is not None:
        return sc, [f"{name(k)}  [subtree {sc}]"]
    if k in memo:
        return memo[k]
    if k in seen:
        return 0, [f"(recursion into {name(k)})"]
    own = frames.get(k)
    if own is None:
        externals.add(name(k))
        return 0, [f"{name(k)}  [not walked]"]
    best, chain = 0, []
    for t in edges.get(k, ()):
        d, c = walk(t, seen | {k})
        if d > best:
            best, chain = d, c
    memo[k] = (own + best, [f"{name(k)}  {own}"] + chain)
    return memo[k]


memo_via = {}


def walk_via(k, seen):
    """Deepest path from k that passes through `via`, or None."""
    if matches(k, via):
        return walk(k, seen)
    if k in memo_via:
        return memo_via[k]
    own = frames.get(k)
    if own is None or k in seen or stop_cost(k) is not None:
        return None
    best = None
    for t in edges.get(k, ()):
        r = walk_via(t, seen | {k})
        if r and (best is None or r[0] > best[0]):
            best = r
    memo_via[k] = None if best is None else (own + best[0], [f"{name(k)}  {own}"] + best[1])
    return memo_via[k]


for r in roots:
    res = walk_via(r, frozenset()) if via else walk(r, frozenset())
    if res is None:
        print(f"\n== {name(r)}: no path through {via}")
        continue
    total, chain = res
    print(f"\n== {name(r)}: {total} bytes" + (f" (through {via})" if via else ""))
    for c in chain:
        print("   " + c)
print("\nnot walked (frames not counted):", len(externals))
for e in sorted(externals):
    print("   ", e)
