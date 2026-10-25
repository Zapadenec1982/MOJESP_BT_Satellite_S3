/*
 * One EQ band as the device holds it, and the edits the equalizer page makes on a set of them.
 * A band is the API's { type, f, gain, q, on }; a set has
 * up to MAX_BANDS, and «Рівно» is the same ten with all of them off.
 *
 * The page lists every band of the set by its place, on or off, as the glass does: «Смуга 4» is the point 4 on both, and «Увімкнена» in a band's sheet turns it
 * on and off with its numbers kept. A set shorter than MAX_BANDS (an import) takes a new one.
 *
 * Limits are the panel's page's (eq_web.h): 20 Hz – 20 kHz, ±12 dB, Q 0.3 – 8, a shelf's Q ≤ 1.
 *
 * No imports but the maths: tools/tests/test_webui_eqfile.py runs it in Node; lib/eq.js passes
 * these on to the pages.
 */
import { SHELF_Q_MAX, isPass } from './eqmath.js';

export { SHELF_Q_MAX };
export const F_MIN = 20;
export const F_MAX = 20000;
export const GAIN_MAX = 12;
export const Q_MIN = 0.3;
export const Q_MAX = 8;
export const Q_LR12 = 0.5;   // Linkwitz-Riley 12 dB/oct
export const Q_BW12 = 0.707; // Butterworth 12 dB/oct
/** The bands a set holds at most (dsp_chain.h MAX_BANDS). */
export const MAX_BANDS = 10;

/** The ladder a pass band's slope walks (screen_eq.cpp PASS_Q): both named alignments on it. */
export const PASS_Q = [0.3, 0.4, Q_LR12, 0.6, Q_BW12, 0.8, 1, 1.3, 1.6, 2, 2.5, 3, 4, 5, 6.5, 8];

const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
const isShelf = (t) => t === 'lowshelf' || t === 'highshelf';

/** A band held to the limits the device holds it to. */
export function clampBand(b) {
  const q = Number.isFinite(b.q) ? b.q : 1;
  return {
    type: b.type,
    f: clamp(Number.isFinite(b.f) ? b.f : 1000, F_MIN, F_MAX),
    gain: clamp(Number.isFinite(b.gain) ? b.gain : 0, -GAIN_MAX, GAIN_MAX),
    q: clamp(q, Q_MIN, isShelf(b.type) ? SHELF_Q_MAX : Q_MAX),
    on: !!b.on,
  };
}

/**
 * A new type for a band, as the panel changes it (screen_eq.cpp set_type): a pass starts at
 * Butterworth rather than turning leftover Q into a resonator, a shelf's Q stops at its cap.
 */
export function withType(b, type) {
  let q = b.q;
  if (isPass(type) && !isPass(b.type)) q = Q_BW12;
  if (isShelf(type) && q > SHELF_Q_MAX) q = SHELF_Q_MAX;
  return { ...b, type, q };
}

export function passQIndex(q) {
  let best = 0;
  for (let i = 1; i < PASS_Q.length; i++) if (Math.abs(PASS_Q[i] - q) < Math.abs(PASS_Q[best] - q)) best = i;
  return best;
}

/** The bands the page lists: those that are on, each with its place in the set. */
export const shownBands = (bands) => bands.map((b, i) => ({ i, b })).filter((x) => x.b.on);

/**
 * Where a new band goes: in the middle — on the log axis — of the widest stretch no band is in,
 * so its point does not land on another; 1 kHz in an empty set. Two significant figures.
 */
export function freeFrequency(bands) {
  const fs = shownBands(bands).map((x) => x.b.f).sort((a, b) => a - b);
  if (!fs.length) return 1000;
  const edges = [F_MIN, ...fs, F_MAX];
  let best = 0, at = 1000;
  for (let k = 1; k < edges.length; k++) {
    const gap = edges[k] / edges[k - 1];
    if (gap > best) { best = gap; at = Math.sqrt(edges[k] * edges[k - 1]); }
  }
  return Number(at.toPrecision(2));
}

/**
 * «+ Додати смугу», for a set shorter than MAX_BANDS: a 0 dB peak at a free frequency after the
 * last. Returns the new set and the new band's place, or null when the set has all of them.
 */
export function addBand(bands) {
  if (bands.length >= MAX_BANDS) return null;
  const fresh = { type: 'peak', f: freeFrequency(bands), gain: 0, q: 1, on: true };
  return { bands: [...bands, fresh], i: bands.length };
}

/** «Скинути «Власний»»: every band off — what «Рівно» is. */
export const resetBands = (bands) => bands.map((b) => ({ ...b, on: false }));
