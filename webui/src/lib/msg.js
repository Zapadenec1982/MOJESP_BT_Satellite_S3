/*
 * Placeholders and the firmware's messages, kept free of Svelte and of the catalogs so the tests
 * can load them in Node (tools/tests/test_webui_i18n.py). stores/i18n.js re-exports both.
 */

/** '{0} s' / '{name}' placeholders. */
export function fmt(s, params = {}) {
  return String(s ?? '').replace(/\{(\w+)\}/g, (m, k) => (k in params ? params[k] : m));
}

const MSG = /^@([a-z][a-z0-9_]*(?:\.[a-z0-9_]+)+)(?:\u001f|$)/;

/**
 * A message the firmware publishes: "@<key>", then
 * name=value arguments, each behind U+001F — the panel's i18n::render() reads the same. A status
 * line, a progress: airplay.mode_status, audio.port_meas_status, the DLNA browser's lines. Only a
 * key of the catalog's shape that the catalog has makes a message; anything else — a DLNA folder
 * whose name starts with "@", an older firmware's Ukrainian text — is shown as it is.
 */
export function msg(dict, s) {
  const str = String(s ?? '');
  const m = MSG.exec(str);
  if (!m || !Object.prototype.hasOwnProperty.call(dict, m[1])) return str;
  const params = {};
  for (const part of str.slice(1 + m[1].length).split('\u001f').slice(1)) {
    const eq = part.indexOf('=');
    if (eq > 0) params[part.slice(0, eq)] = part.slice(eq + 1);
  }
  return fmt(dict[m[1]], params);
}
