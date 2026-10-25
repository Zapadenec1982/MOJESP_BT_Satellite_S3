"""«Кросовер»: webui/src/lib/xover.js in Node.

A scheme is offered only once the firmware says it plays it (audio.out_caps, out_caps.h); a
firmware that says nothing has stereo; the bits are the firmware's.
"""

import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def run(script):
    node = shutil.which("node")
    assert node, "Node.js is required for the WebUI crossover regression"
    lib = (ROOT / "webui/src/lib/xover.js").as_uri()
    head = (
        f"import * as X from {json.dumps(lib)};\n"
        "const eq = (a, b, what) => { const x = JSON.stringify(a), y = JSON.stringify(b);"
        " if (x !== y) { console.error(what + ': ' + x + ' != ' + y); process.exit(1); } };\n"
    )
    result = subprocess.run([node, "--input-type=module", "-e", head + script], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr


def test_only_what_the_firmware_says_plays():
    run("""
const on = (caps) => X.schemesOf(caps).filter((s) => s.supported).map((s) => s.id);
eq(X.schemesOf(1).map((s) => s.id), ['stereo', 'sub', 'biamp', 'four'], 'all four, in the page\\'s order');
eq(on(1), ['stereo'], 'this board: stereo');
eq(on(undefined), ['stereo'], 'a firmware that does not say: stereo');
eq(on(0), ['stereo'], 'nor one that says nothing');
eq(on(3), ['stereo', 'sub'], 'with a subwoofer');
eq(on(15), ['stereo', 'sub', 'biamp', 'four'], 'all of them');
eq(X.playing(1), 'stereo', 'what plays');
""")


def test_the_bits_are_the_firmwares():
    header = (ROOT / "components/audio_dsp/include/out_caps.h").read_text(encoding="utf-8")
    bits = dict(re.findall(r"(OUT_[A-Z]+)\s*=\s*(\d+)", header))
    lib = (ROOT / "webui/src/lib/xover.js").read_text(encoding="utf-8")
    web = dict(re.findall(r"(OUT_[A-Z]+) = (\d+)", lib))
    assert bits == web, (bits, web)
    manifest = json.loads((ROOT / "modules/audio/manifest.json").read_text(encoding="utf-8"))
    assert manifest["state"]["audio.out_caps"]["default"] == int(bits["OUT_STEREO"])
