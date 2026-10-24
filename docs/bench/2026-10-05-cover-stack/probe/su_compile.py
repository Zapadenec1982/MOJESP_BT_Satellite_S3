"""Recompile sources with the build's exact command plus -fstack-usage -fcallgraph-info=su.

usage: su_compile.py <compile_commands.json> <outdir> <path-suffix>...
The object goes to <outdir>/<name>.obj, next to it <name>.su and <name>.ci.
"""
import json
import os
import shlex
import subprocess
import sys

cc_path, outdir, suffixes = sys.argv[1], sys.argv[2], sys.argv[3:]
os.makedirs(outdir, exist_ok=True)
cc = json.load(open(cc_path, encoding="utf-8"))
done = set()
for e in cc:
    f = e["file"].replace("\\", "/")
    hit = [s for s in suffixes if f.endswith(s)]
    if not hit or f in done:
        continue
    done.add(f)
    # Windows paths use backslashes; escaped quotes in -D values must survive.
    cmd = e["command"].replace('\\"', "\x00").replace("\\", "/").replace("\x00", '\\"')
    args = shlex.split(cmd, posix=True)
    # Drop the original -o target and pin ours.
    i = args.index("-o")
    name = os.path.basename(f)
    obj = os.path.join(outdir, name + ".obj").replace("\\", "/")
    args[i + 1] = obj
    args += ["-fstack-usage", "-fcallgraph-info=su"]
    r = subprocess.run(args, cwd=e["directory"], capture_output=True, text=True)
    print(("OK  " if r.returncode == 0 else "ERR ") + f)
    if r.returncode:
        print(r.stderr[-3000:])
missing = [s for s in suffixes if not any(d.endswith(s) for d in done)]
if missing:
    print("NOT FOUND:", missing)
