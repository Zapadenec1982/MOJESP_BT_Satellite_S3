/*
 * Filters as text (docs/PLAN_SOUND_UX_2026-10-14.md, S7): the lines Equalizer APO reads, Room EQ
 * Wizard writes and reads (its "Generic" equaliser), and AutoEQ publishes for headphones —
 *
 *   Preamp: -6.4 dB
 *   Filter 1: ON LSC Fc 105 Hz Gain 5.5 dB Q 0.71
 *   Filter  2: ON  PK       Fc   47.0 Hz  Gain  -0.7 dB  Q  0.39
 *   Filter 3: OFF None
 *   Filter: ON PEQ Fc 100 Hz Gain 1.0 dB BW Oct 0.167
 *
 * The number after Filter means nothing (APO ignores it). What this panel has: PK, PEQ, Modal (a
 * peak), LSC and LS (a low shelf by its centre frequency), HSC and HS, LP and LPQ, HP and HPQ, NO.
 * A shelf's slope in dB per octave ("LSC 10.8 dB") becomes the RBJ Q for that slope at its gain;
 * without a slope or a Q it is 12 dB/oct, Q 0.707, as REW's shelves are. LS/HS 6dB and 12dB are
 * shelves by their corner, a definition this panel does not have, and BP, AP and IIR are not
 * filters it has: counted, not read. OFF lines are skipped. A Preamp is said and not kept: the
 * panel keeps headroom for a boost itself (DspChain). Up to MAX_BANDS filters; numbers out of the
 * panel's reach are held to it, and counted.
 *
 * Out, the same lines for the bands that are on: APO and REW read them, and this reads them back
 * to the same bands (tools/tests/test_webui_eqfile.py).
 */
import { clampBand, MAX_BANDS, Q_BW12 } from './eqband.js';
import { hasGain } from './eqmath.js';

const TYPE_OF = {
  PK: 'peak', PEQ: 'peak', MODAL: 'peak',
  LS: 'lowshelf', LSC: 'lowshelf', HS: 'highshelf', HSC: 'highshelf',
  LP: 'lowpass', LPQ: 'lowpass', HP: 'highpass', HPQ: 'highpass', NO: 'notch',
};
const CODE_OF = { peak: 'PK', lowshelf: 'LSC', highshelf: 'HSC', lowpass: 'LPQ', highpass: 'HPQ', notch: 'NO' };

const NUM = '([-+]?\\d+(?:[.,]\\d+)?)';
const num = (s) => Number(String(s).replace(',', '.'));
const RE_FILTER = /^\s*Filter\s*\d*\s*:\s*(ON|OFF)\b\s*(.*)$/i;
const RE_PREAMP = new RegExp(`^\\s*Preamp\\s*:\\s*${NUM}\\s*dB`, 'i');
const RE_FC = new RegExp(`\\bFc\\s+${NUM}\\s*(kHz|Hz)?`, 'i');
const RE_GAIN = new RegExp(`\\bGain\\s+${NUM}`, 'i');
const RE_Q = new RegExp(`\\bQ\\s+${NUM}`, 'i');
const RE_BW = new RegExp(`\\bBW\\s+(?:Oct\\s+)?${NUM}`, 'i');

/** A peak's bandwidth in octaves as its Q. */
export const bwToQ = (n) => Math.sqrt(Math.pow(2, n)) / (Math.pow(2, n) - 1);

/** A shelf's slope in dB/oct at its gain as its RBJ Q (S = slope / 12); NaN where there is none. */
export function slopeToQ(slopeDb, gainDb) {
  const S = slopeDb / 12, A = Math.pow(10, gainDb / 40);
  return 1 / Math.sqrt((A + 1 / A) * (1 / S - 1) + 2);
}

const same = (a, b) => Math.abs(a - b) < 1e-9;

/**
 * Read the text. Returns { bands, preamp, off, unsupported, unreadable, extra, clamped }: the
 * bands to import (on, held to the limits); the Preamp in dB or null; how many lines were OFF, of
 * a kind this panel lacks, not readable, past MAX_BANDS, and how many filters had a number held.
 */
export function parseFilters(text) {
  const out = { bands: [], preamp: null, off: 0, unsupported: 0, unreadable: 0, extra: 0, clamped: 0 };
  for (const line of String(text || '').split(/\r?\n/)) {
    const pre = line.match(RE_PREAMP);
    if (pre) { out.preamp = num(pre[1]); continue; }
    const m = line.match(RE_FILTER);
    if (!m) continue;                                  // headers, notes, comments
    if (m[1].toUpperCase() === 'OFF') { out.off++; continue; }
    const rest = m[2];
    const t = rest.match(/^([A-Za-z]+)(?:\s+(\d+(?:[.,]\d+)?)\s*dB\b)?/);
    if (!t) { out.unreadable++; continue; }
    const code = t[1].toUpperCase();
    if (code === 'NONE') { out.off++; continue; }      // REW's unused filter
    const slope = t[2] !== undefined ? num(t[2]) : null;
    const type = TYPE_OF[code];
    // LS/HS with 6 or 12 dB right after: shelves by their corner, not the centre this panel uses
    if (!type || ((code === 'LS' || code === 'HS') && slope !== null)) { out.unsupported++; continue; }
    const fc = rest.match(RE_FC);
    if (!fc) { out.unreadable++; continue; }
    const f = num(fc[1]) * (fc[2] && fc[2].toLowerCase() === 'khz' ? 1000 : 1);
    const g = rest.match(RE_GAIN);
    const gain = hasGain(type) ? (g ? num(g[1]) : NaN) : 0;
    if (hasGain(type) && !Number.isFinite(gain)) { out.unreadable++; continue; }
    const qm = rest.match(RE_Q), bw = rest.match(RE_BW);
    let q = qm ? num(qm[1]) : bw ? bwToQ(num(bw[1])) : NaN;
    if (!Number.isFinite(q)) {
      if (type === 'lowshelf' || type === 'highshelf') {
        q = slope !== null ? slopeToQ(slope, gain) : Q_BW12;
        if (!Number.isFinite(q)) q = Infinity;          // steeper than a shelf here goes: held to it below
      }
      else if (type === 'lowpass' || type === 'highpass') q = Q_BW12;
      else { out.unreadable++; continue; }             // a peak or a notch says its width
    }
    if (!Number.isFinite(f) || !(q > 0)) { out.unreadable++; continue; }
    if (out.bands.length >= MAX_BANDS) { out.extra++; continue; }
    const want = { type, f, gain, q, on: true };
    const b = clampBand(want);
    if (!same(b.f, want.f) || !same(b.gain, want.gain) || !same(b.q, want.q)) out.clamped++;
    out.bands.push(b);
  }
  return out;
}

/** A number as short as it can be without losing what the device keeps of it. */
const short = (v, places) => String(Number(v.toFixed(places)));

/**
 * The bands that are on, as the lines above: Fc to a hundredth of a Hz, gain to a hundredth of a
 * dB, Q to a thousandth — what the device itself calls the same band (eq_same_section).
 * `title` goes on the first line, after a #.
 */
export function formatFilters(bands, title = '') {
  const lines = [];
  if (title) lines.push(`# ${title}`);
  let n = 0;
  for (const b of bands) {
    const code = CODE_OF[b.type];
    if (!b.on || !code) continue;
    n++;
    const gain = hasGain(b.type) ? ` Gain ${short(b.gain, 2)} dB` : '';
    lines.push(`Filter ${n}: ON ${code} Fc ${short(b.f, 2)} Hz${gain} Q ${short(b.q, 3)}`);
  }
  return lines.join('\n') + '\n';
}
