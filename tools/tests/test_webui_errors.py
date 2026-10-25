"""A failure in words (webui/src/lib/errors.js) under Node: the toast said `{"error":"busy"}`,
«Failed to fetch» and English status words."""

import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
LIB = json.dumps((ROOT / "webui" / "src" / "lib" / "errors.js").as_uri())
UK = json.loads((ROOT / "webui" / "src" / "i18n" / "uk.json").read_text(encoding="utf-8"))


def test_human_error_says_the_panels_answers_in_words():
    node = shutil.which("node")
    assert node, "Node.js is required for the WebUI error tests"
    script = f"""
const {{ humanError }} = await import({LIB});
const t = {json.dumps(UK, ensure_ascii=False)};
const fmt = (s, p) => s.replace(/\\{{(\\w+)\\}}/g, (m, k) => (k in p ? p[k] : m));
const eq = (a, b, what) => {{ if (a !== b) {{ console.error('FAILED: ' + what + ': ' + JSON.stringify(a) + ' is not ' + JSON.stringify(b)); process.exit(1); }} }};
eq(humanError('{{"error":"busy"}}', t, fmt), t['err.busy'], 'busy');
eq(humanError('{{"error":"LVGL busy"}}', t, fmt), t['err.busy'], 'the glass busy');
eq(humanError('{{"error":"no_memory"}}', t, fmt), t['common.no_memory'], 'no memory');
eq(humanError('{{"error":"bad_edit"}}', t, fmt), fmt(t['err.code'], {{ code: 'bad_edit' }}), 'a code of its own page: said with the code');
eq(humanError('Failed to fetch', t, fmt), t['app.error'], 'Chrome offline');
eq(humanError('Load failed', t, fmt), t['app.error'], 'Safari offline');
eq(humanError(new Error('Connection error'), t, fmt), t['app.error'], "the upload's offline, as an Error");
eq(humanError('Server error', t, fmt), t['err.server'], 'a status word');
eq(humanError('GET /api/state: 503', t, fmt), t['err.busy'], '503 is busy');
eq(humanError('/api/ota: 418', t, fmt), fmt(t['err.code'], {{ code: '418' }}), 'another status: its number');
eq(humanError('{{not json', t, fmt), '{{not json', 'not JSON: as it is');
eq(humanError(t['wifi.connecting'], t, fmt), t['wifi.connecting'], 'words stay words');
eq(humanError(undefined, t, fmt), '', 'nothing');
"""
    r = subprocess.run([node, "--input-type=module", "-e", script], capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
