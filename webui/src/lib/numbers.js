// Numbers as people type them into a field (dsp/NumField, dsp/StepRow): a decimal comma, "1.2k",
// and the minus the pages print (U+2212) or a dash. And as the pages print them. Pure, for
// tools/tests/test_webui_numbers.py.

/**
 * `x` to `n` decimals, with the separator `lang` writes: a comma in uk, de and pl, a point in en
 * (decision 7 of docs/PLAN_PANEL_LANG_2026-10-12.md) — what the glass's i18n::num prints, so the
 * equalizer reads «1,25 kHz» on both.
 */
export function decimal(x, n, lang) {
  const s = Number(x).toFixed(n);
  return lang === 'en' ? s : s.replace('.', ',');
}

/** The number in `s`, or NaN. */
export function parseNumber(s) {
  const t = String(s).trim().replace(/[−–]/g, '-').replace(',', '.');
  const m = t.match(/^([+-]?\d*\.?\d+)\s*(k)?/i);
  return m ? Number(m[1]) * (m[2] ? 1000 : 1) : NaN;
}

/**
 * For a field whose values are all at or below zero — a level in dBFS: a number typed without a
 * sign is the negative one. A phone's decimal keypad (iOS) has no minus, and "20" meant −20 dBFS,
 * not 0 dBFS — which the range would have made of it.
 */
export function parseNonPositive(s) {
  const v = parseNumber(s);
  if (!Number.isFinite(v)) return v;
  return /^\s*\+/.test(String(s)) ? v : -Math.abs(v);
}
