"""The web's library and DLNA browser speak the firmware's language: webui/src/lib/media.js
against components/dlna_client (the browser's commands and their packing) and the panel's
library page (how a path becomes a row).

A command the web packs differently from dlna_client::encode() is a tap that opens the wrong row,
or none; a row named differently from the panel's is a list that disagrees with the glass.
"""

import json
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
LIB = (ROOT / "webui/src/lib/media.js").as_uri()


def run(script):
    node = shutil.which("node")
    assert node, "Node.js is required for the WebUI media regression"
    result = subprocess.run([node, "--input-type=module", "-e", script], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return json.loads(result.stdout)


def firmware_commands():
    header = (ROOT / "components/dlna_client/include/dlna_browser.h").read_text(encoding="utf-8")
    body = re.search(r"enum class Command : uint8_t \{(.*?)\};", header, re.S).group(1)
    return {name.lower(): int(value) for name, value in re.findall(r"(\w+)\s*=\s*(\d+)", body)}


def test_commands_are_the_browsers():
    fw = firmware_commands()
    js = run(f"import {{ BROWSE }} from {json.dumps(LIB)}; console.log(JSON.stringify(BROWSE));")
    names = {"discover": "discover", "open": "open", "back": "back", "nextpage": "next",
             "prevpage": "prev", "queue": "queue", "cancel": "cancel"}
    assert {names[k]: v for k, v in fw.items()} == js


def test_packing_is_encode():
    # The C side, quoted so a change to it fails here.
    src = (ROOT / "components/dlna_client/src/dlna_browser.cpp").read_text(encoding="utf-8")
    assert ("return (int32_t)((uint32_t)revision << 12 | (uint32_t)(row & 255) << 4 | (uint8_t)command);"
            in src)
    cases = [(2, 0, 1), (2, 11, 4097), (3, 0, 65535), (6, 255, 65534), (7, 300, 12)]
    js = run(f"import {{ browseCode }} from {json.dumps(LIB)};"
             f"console.log(JSON.stringify({json.dumps(cases)}.map(([c, r, v]) => browseCode(c, r, v))));")
    for (command, row, revision), got in zip(cases, js):
        assert got == ((revision << 12) | ((row & 255) << 4) | command)
        assert got <= 268435455   # dlna.browse_cmd's max in modules/dlna/manifest.json


def test_rows_are_named_as_on_the_panel():
    # screen_library.cpp strip_ext(): a short, plain extension goes; anything else stays.
    cases = {
        "Artist/Album/01 - Title.flac": ["01 - Title", "Artist/Album"],
        "Mr. Brown": ["Mr. Brown", ""],
        "Mr. Brown.mp3": ["Mr. Brown", ""],
        "a/.hidden": [".hidden", "a"],
        "a/b.c": ["b.c", "a"],
        "a/song.opus": ["song", "a"],
        "a/song.mpeg4": ["song.mpeg4", "a"],
        "a/song.m-4": ["song.m-4", "a"],
    }
    js = run(f"import {{ trackName, trackFolder }} from {json.dumps(LIB)};"
             f"console.log(JSON.stringify(Object.fromEntries({json.dumps(list(cases))}"
             f".map((p) => [p, [trackName(p), trackFolder(p)]]))));")
    assert js == cases


def test_play_path_is_path_at():
    # Playlist::path_at(): "%s/%s", root and the entry.
    src = (ROOT / "components/playlist/src/playlist.cpp").read_text(encoding="utf-8")
    assert 'snprintf(out, n, "%.*s/%.*s", MAX_DIR - 1, root_, MAX_REL - 1, rel);' in src
    js = run(f"import {{ trackPath }} from {json.dumps(LIB)}; console.log(JSON.stringify(trackPath('/sdcard', 'A/b.flac')));")
    assert js == "/sdcard/A/b.flac"


def test_filter_takes_every_word_in_any_case():
    js = run(f"""import {{ matches }} from {json.dumps(LIB)};
const p = 'ДахаБраха/Шлях/03 - Вівця.flac';
console.log(JSON.stringify([matches(p, ''), matches(p, 'шлях'), matches(p, 'ДАХА вівця'), matches(p, 'даха 04'),
                            matches(p, '  '), matches(p, 'flac')]));""")
    assert js == [True, True, True, False, True, True]


def test_crumbs_follow_the_snapshots():
    js = run(f"""import {{ nextCrumbs }} from {json.dumps(LIB)};
const v = (title, servers = false) => ({{ title, servers }});
const out = [];
let c = [];
c = nextCrumbs(c, {{ open: 'NAS' }}, v('NAS')); out.push(c);
c = nextCrumbs(c, {{ open: 'CD1' }}, v('CD1')); out.push(c);
c = nextCrumbs(c, {{ open: 'CD1' }}, v('CD1')); out.push(c);          // a folder named like its parent
c = nextCrumbs(c, null, v('CD1')); out.push(c);                        // next page: the same folder
c = nextCrumbs(c, {{ back: true }}, v('CD1')); out.push(c);
c = nextCrumbs(c, {{ open: 'X' }}, v('CD1')); out.push(c);             // the open failed: nothing moved
c = nextCrumbs(c, null, v('Elsewhere')); out.push(c);                  // the panel went somewhere
c = nextCrumbs(c, {{ back: true }}, v('Up')); out.push(c);             // climbing a path not seen
c = nextCrumbs(c, {{ back: true }}, v('Media', true)); out.push(c);
console.log(JSON.stringify(out));""")
    assert js == [
        ["NAS"],
        ["NAS", "CD1"],
        ["NAS", "CD1", "CD1"],
        ["NAS", "CD1", "CD1"],
        ["NAS", "CD1"],
        ["NAS", "CD1"],
        [None, "Elsewhere"],
        [None, "Up"],
        [],
    ]
