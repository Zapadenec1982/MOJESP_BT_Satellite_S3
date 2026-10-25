/*
 * What a toast says when something failed: the panel's own words for its
 * answers, never the answer itself — `{"error":"busy"}`, «Failed to fetch» and an English
 * status word were what people read. lib/api.js keeps the raw text in the Error (pages that
 * look for a code of their own parse it); the toast says it in the language of the page.
 *
 * No imports: tools/tests/test_webui_errors.py runs it in Node.
 */

/** What browsers say when the panel is not there (Chrome, Firefox, Safari) and api.js's upload. */
const OFFLINE = ['Failed to fetch', 'NetworkError when attempting to fetch resource.', 'Load failed',
  'The network connection was lost.', 'Connection error'];
/** api.js's status words. */
const STATUS = { 'Bad request': 'err.bad_request', 'Invalid value': 'err.bad_request', 'Not found': 'err.not_found',
  'Too large': 'err.too_large', 'Server error': 'err.server' };
/** The panel's codes ({"error": code}) that any page may meet; a page reads its own first. */
const CODES = { busy: 'err.busy', 'LVGL busy': 'err.busy', no_memory: 'common.no_memory',
  too_large: 'err.too_large', size: 'err.too_large', bad_request: 'err.bad_request', write: 'err.server' };

/**
 * `raw` — an Error's message, or any text a page hands the toast — in words: `t` the catalog,
 * `fmt` the catalog's {name} filler. Text that is none of the above is already words: as it is.
 */
export function humanError(raw, t, fmt) {
  const s = raw instanceof Error ? raw.message : raw == null ? '' : String(raw);
  if (OFFLINE.includes(s)) return t['app.error'] || s;
  if (STATUS[s]) return t[STATUS[s]] || s;
  if (s.startsWith('{')) {
    let code;
    try { code = JSON.parse(s).error; } catch (e) { return s; }
    if (typeof code !== 'string') return s;
    return t[CODES[code]] || fmt(t['err.code'] || '{code}', { code });
  }
  const st = /^(?:GET )?\/\S+: (\d{3})$/.exec(s);          // "GET /api/state: 503", a status api.js has no word for
  if (st) return st[1] === '503' ? t['err.busy'] || s : fmt(t['err.code'] || '{code}', { code: st[1] });
  return s;
}
