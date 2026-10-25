/*
 * What the Service page says about a number: the panel's verdicts and formats
 * (modules/ui_lvgl/include/service_readout.h), so the web and the glass give one answer.
 * tools/tests/test_webui_service.py runs these against the C++ header itself.
 *
 * No imports: the test loads this file in Node as it is.
 */

export const OK = 0;
export const WARN = 1;
export const BAD = 2;

/** The way the panel turns a published float into its integer (BaseModule::read_number). */
export function num(v) {
  const f = Math.fround(Number(v));
  if (!(f >= -2147483520 && f <= 2147483520)) return 0;
  return Math.trunc(Math.fround(f >= 0 ? f + 0.5 : f - 0.5));
}

// ── verdicts: service_readout.h, threshold for threshold ──
export const cpu = (pct) => (pct >= 85 ? BAD : pct >= 60 ? WARN : OK);
export const loopMs = (ms) => (ms >= 30 ? BAD : ms >= 13 ? WARN : OK);
export const stackFree = (b) => (b < 1024 ? BAD : b < 2048 ? WARN : OK);
export const dmaFree = (b) => (b < 40 * 1024 ? BAD : b < 80 * 1024 ? WARN : OK);
export const internalFree = (b) => (b < 48 * 1024 ? BAD : b < 96 * 1024 ? WARN : OK);
export const rttMs = (ms) => (ms > 200 ? BAD : ms > 50 ? WARN : OK);
export const counter = (n) => (n > 0 ? BAD : OK);
export const softCounter = (n) => (n > 0 ? WARN : OK);
export const flag = (raised) => (raised ? BAD : OK);
const BAD_BOOTS = ['panic', 'brownout', 'int_wdt', 'task_wdt', 'wdt'];
export const bootGrade = (token) => (BAD_BOOTS.includes(token) ? BAD : OK);

/** `audio.lim_gr` in tenths of a dB, bounded as the panel bounds it before converting. */
export function limTenths(gr) {
  const g = Math.fround(Number(gr));
  if (!(g > 0)) return 0;
  return g < Math.fround(99.9) ? Math.trunc(Math.fround(Math.fround(g * 10) + 0.5)) : 999;
}
/** A working limiter is worth noticing, never a fault (screen_svc.cpp). */
export const limiter = (tenths) => (tenths > 0 ? WARN : OK);

// ── verdicts the panel does not draw: the norms of docs/AUDIO.md, "Diagnostics" and the
//    manifest's own words ──
/** `audio.bass_gr`: 0…6 dB on loud bass at 100 %; more than 10 dB means the EQ asks too much. */
export const bassGr = (db) => (db > 10 ? WARN : OK);
/** `audio.dsp_pct`: up to ~35 % at 192 kHz. */
export const dspPct = (pct) => (pct > 35 ? WARN : OK);
/** `audio.dsp_max_pct`: up to ~80 % ("понад 80 — тісно"). */
export const dspMaxPct = (pct) => (pct > 80 ? WARN : OK);

// ── formats ──
/**
 * A byte count as svc::bytes prints it: exact below 10 KB, then KB with one decimal and MB
 * with two, truncated rather than rounded. `u` names the units, `dec` is the decimal sign.
 */
export function bytes(b, u = { b: 'B', kb: 'KB', mb: 'MB' }, dec = '.') {
  if (!(b >= 0)) return '—';
  b = Math.trunc(b);
  if (b < 10 * 1024) return `${b} ${u.b}`;
  if (b < 1024 * 1024) {
    const kb = Math.trunc(b / 1024);
    const f = Math.trunc(((b % 1024) * 10) / 1024);
    return `${kb}${dec}${f} ${u.kb}`;
  }
  const mb = Math.trunc(b / (1024 * 1024));
  const f = Math.trunc(((b % (1024 * 1024)) * 100) / (1024 * 1024));
  return `${mb}${dec}${String(f).padStart(2, '0')} ${u.mb}`;
}

/** Whole kibibytes, for a hero figure. */
export const kib = (b) => (b < 0 ? 0 : Math.trunc(b / 1024));

/** Uptime as svc::uptime: a clock until there is a day, then "1д 04:12". `d` is the day mark. */
export function uptime(sec, d = 'd') {
  if (!(sec >= 0)) return '—';
  sec = Math.trunc(sec);
  const days = Math.min(9999, Math.trunc(sec / 86400));
  const p = (n) => String(n).padStart(2, '0');
  const h = Math.trunc((sec % 86400) / 3600);
  const m = Math.trunc((sec % 3600) / 60);
  return days > 0 ? `${days}${d} ${p(h)}:${p(m)}` : `${p(h)}:${p(m)}:${p(sec % 60)}`;
}

/** Limiter gain reduction as svc::gain_reduction prints it: "0.0" or "−1.5" (no unit). */
export function gainReduction(tenths, dec = '.') {
  if (tenths < 0) tenths = 0;
  if (tenths > 999) tenths = 999;
  const s = `${Math.trunc(tenths / 10)}${dec}${tenths % 10}`;
  return tenths === 0 ? s : `−${s}`;
}
