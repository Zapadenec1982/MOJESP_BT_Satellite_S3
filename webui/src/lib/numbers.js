// Numbers as people type them into a field (dsp/NumField, dsp/StepRow): a decimal comma, "1.2k",
// and the minus the pages print (U+2212) or a dash. And as the pages print them. Pure, for
// tools/tests/test_webui_numbers.py.

/**
 * `x` to `n` decimals, with the separator `lang` writes: a comma in uk, de and pl, a point in en
 * and the minus sign (U+2212) — what the
 * glass's i18n::num prints, so the equalizer reads «1,25 кГц» and «−3,5 дБ» on both. A value that
 * rounds to zero has no sign.
 */
export function decimal(x, n, lang) {
  let s = Number(x).toFixed(n);
  if (/^-[0.]+$/.test(s)) s = s.slice(1);
  s = s.replace('-', '−');
  return lang === 'en' ? s : s.replace('.', ',');
}

const UNIT = { kHz: 'unit.khz', Hz: 'unit.hz', kbps: 'unit.kbps', bit: 'unit.bit', ch: 'unit.ch' };

/**
 * A technical line in the language's units and decimals — the codec badge ("44.1 kHz 2 ch
 * 128 kbps", "16/44.1 kHz", "LDAC 96.0 kHz"), USB's "48.0 kHz 24 bit", the tone's "1000 Hz" — as
 * the glass's i18n::badge writes it: «44,1 кГц 128 кбіт/с». Two channels go unsaid, and a whole
 * number loses its ",0". Words that are not a number and its unit stay as they are. `t` is the
 * catalog.
 */
export function badge(raw, t, lang) {
  if (!raw) return '';
  const w = String(raw).split(' ');
  const out = [];
  for (let i = 0; i < w.length; i++) {
    const unit = UNIT[w[i + 1]];
    const m = unit ? /^(\d+\/)?(\d+)(?:\.(\d+))?$/.exec(w[i]) : null;
    if (!m) { out.push(w[i]); continue; }
    i++;
    if (unit === 'unit.ch' && !m[1] && m[2] === '2' && !/[1-9]/.test(m[3] || '')) continue;
    const frac = /[1-9]/.test(m[3] || '') ? (lang === 'en' ? '.' : ',') + m[3] : '';
    out.push(`${m[1] || ''}${m[2]}${frac}`, t[unit] || w[i]);
  }
  return out.join(' ');
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
