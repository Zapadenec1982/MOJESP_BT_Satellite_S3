/*
 * The EQ curve as the audio core designs it — a line-for-line port of
 * components/audio_dsp/src/dsp_chain.cpp: design_q, analog_mag2, design_matched,
 * design_svf_params, svf_magnitude_db and eq_curve_db.
 *
 * A second implementation of the maths for drawing would eventually disagree with what is heard,
 * and nothing would show it. So this one is held to the C++ by vectors the host test writes
 * (tests/host/fixtures/eq_curve/vectors.json, test_eq_curve_vectors.cpp) and
 * tools/tests/test_webui_eqmath.py runs it against them.
 *
 * A band is the API's: { type: 'peak'|'lowshelf'|'highshelf'|'lowpass'|'highpass'|'notch',
 * f, gain, q, on }. Everything in double, as the design is; the panel evaluates its curve in
 * float, which is a thousandth of a dB away and far under a pixel.
 */

/** A shelf takes Q up to this (dsp_chain.h): above it a shelf overshoots its own gain. */
export const SHELF_Q_MAX = 1.0;

export const TYPES = ['peak', 'lowshelf', 'highshelf', 'lowpass', 'highpass', 'notch'];

const isShelf = (t) => t === 'lowshelf' || t === 'highshelf';

/** Does `gain` mean anything for this type? Not for a pass or a notch (screen_eq.cpp). */
export const hasGain = (t) => t === 'peak' || isShelf(t);
export const isPass = (t) => t === 'lowpass' || t === 'highpass';

function designQ(b) {
  return isShelf(b.type) && b.q > SHELF_Q_MAX ? SHELF_Q_MAX : b.q;
}

/** |H|² of the band's analogue prototype at f — what MATCHED aims at. */
export function analogMag2(b, f) {
  const A = Math.pow(10, b.gain / 40), q = designQ(b);
  const W = f / b.f, W2 = W * W;
  let nr, ni, dr, di;
  switch (b.type) {
    case 'peak':      nr = 1 - W2;          ni = (W * A) / q;              dr = 1 - W2;     di = W / (A * q); break;
    case 'lowshelf':  nr = A * (A - W2);    ni = (A * W * Math.sqrt(A)) / q; dr = 1 - A * W2; di = (W * Math.sqrt(A)) / q; break;
    case 'highshelf': nr = A * (1 - A * W2); ni = (A * W * Math.sqrt(A)) / q; dr = A - W2;     di = (W * Math.sqrt(A)) / q; break;
    case 'notch':     nr = 1 - W2;          ni = 0;                        dr = 1 - W2;     di = W / q; break;
    case 'lowpass':   nr = 1;               ni = 0;                        dr = 1 - W2;     di = W / q; break;
    case 'highpass':  nr = -W2;             ni = 0;                        dr = 1 - W2;     di = W / q; break;
    default: return 1;
  }
  const den = dr * dr + di * di;
  return den > 1e-300 ? (nr * nr + ni * ni) / den : 1;
}

/** Vicanek's matched design for a peak, a shelf or a notch; null where it has no real solution. */
function designMatched(b, fs) {
  const A = Math.pow(10, b.gain / 40), q = designQ(b);
  const w0 = (2 * Math.PI * b.f) / fs;
  let wp, zeta;
  switch (b.type) {
    case 'peak':      wp = 1;               zeta = 1 / (2 * A * q); break;
    case 'lowshelf':  wp = 1 / Math.sqrt(A); zeta = 1 / (2 * q); break;
    case 'highshelf': wp = Math.sqrt(A);     zeta = 1 / (2 * q); break;
    case 'notch':     wp = 1;               zeta = 1 / (2 * q); break;
    default: return null;
  }
  const Wn = w0 * wp, e = Math.exp(-zeta * Wn);
  const a2 = e * e;
  const a1 = zeta <= 1 ? -2 * e * Math.cos(Wn * Math.sqrt(1 - zeta * zeta))
                       : -2 * e * Math.cosh(Wn * Math.sqrt(zeta * zeta - 1));
  let b0, b1, b2;
  if (b.type === 'notch') {
    const cw = Math.cos(w0), n = (1 + a1 + a2) / (2 - 2 * cw);
    b0 = n; b1 = -2 * n * cw; b2 = n;
  } else {
    const A0 = (1 + a1 + a2) * (1 + a1 + a2), A1 = (1 - a1 + a2) * (1 - a1 + a2), A2 = -4 * a2;
    const wm = w0 < 0.9 * Math.PI ? w0 : 0.9 * Math.PI;
    const p1 = Math.sin(wm / 2) * Math.sin(wm / 2), p0 = 1 - p1, p2 = 4 * p0 * p1;
    const B0 = A0 * analogMag2(b, 0);
    const B1 = A1 * analogMag2(b, fs / 2);
    const B2 = (analogMag2(b, (wm * fs) / (2 * Math.PI)) * (A0 * p0 + A1 * p1 + A2 * p2) - B0 * p0 - B1 * p1) / p2;
    const V = (Math.sqrt(B0) + Math.sqrt(B1)) / 2;
    if (V * V + B2 < 0) return null;
    b1 = (Math.sqrt(B0) - Math.sqrt(B1)) / 2;
    b0 = (V + Math.sqrt(V * V + B2)) / 2;
    b2 = V - b0;
  }
  const dd = 1 - a1 + a2;
  if (!(dd > 0) || !(1 + a1 + a2 > 0)) return null;
  const g = Math.sqrt((1 + a1 + a2) / dd);
  if (!(g > 0)) return null;
  const k = (2 * (1 - a2)) / (dd * g);
  const m0 = (b0 - b1 + b2) / dd;
  const m1 = ((2 * (b0 - b2)) / dd - m0 * g * k) / g;
  const m2 = (b0 + b1 + b2) / (dd * g * g) - m0;
  return { g, k, m0, m1, m2 };
}

/**
 * The band as one SVF section at fs: { g, k, m0, m1, m2 }, or null when it cannot be designed
 * (then it is a wire). `matched` false is the bilinear design, RBJ's own digital filter.
 */
export function designBand(b, fs, matched = true) {
  if (!(fs >= 1000)) return null;
  const f = b.f, q = designQ(b);
  if (!(f >= 10) || f > fs * 0.49 || !(q > 0)) return null;
  if (matched) {
    const d = designMatched(b, fs);
    if (d) return d;
  }
  const A = Math.pow(10, b.gain / 40);
  let g = Math.tan((Math.PI * f) / fs);
  let k = 1 / q;
  let m0, m1, m2;
  switch (b.type) {
    case 'peak':      k = 1 / (q * A); m0 = 1; m1 = k * (A * A - 1); m2 = 0; break;
    case 'lowshelf':  g /= Math.sqrt(A); m0 = 1; m1 = k * (A - 1); m2 = A * A - 1; break;
    case 'highshelf': g *= Math.sqrt(A); m0 = A * A; m1 = k * (1 - A) * A; m2 = 1 - A * A; break;
    case 'lowpass':   m0 = 0; m1 = 0; m2 = 1; break;
    case 'highpass':  m0 = 1; m1 = -k; m2 = -1; break;
    case 'notch':     m0 = 1; m1 = -k; m2 = 0; break;
    default: return null;
  }
  return { g, k, m0, m1, m2 };
}

function mag2(d, S, C) {
  const gc = d.g * C;
  const dr = gc * gc - S * S;
  const di = d.k * gc * S;
  const nr = d.m0 * dr + d.m2 * gc * gc;
  const ni = d.m0 * di + d.m1 * gc * S;
  const den = dr * dr + di * di;
  return den > 1e-300 ? (nr * nr + ni * ni) / den : 1;
}

/** |H| of a designed section at f, in dB. */
export function svfMagnitudeDb(d, fs, f) {
  const h = (Math.PI * f) / fs;
  const m = mag2(d, Math.sin(h), Math.cos(h));
  return m > 1e-30 ? 10 * Math.log10(m) : -300;
}

/** One band at f, in dB; a band that is off or cannot be designed is 0 dB. */
export function bandMagnitudeDb(b, fs, f) {
  if (!b.on) return 0;
  const d = designBand(b, fs);
  return d ? svfMagnitudeDb(d, fs, f) : 0;
}

/** The summed curve at each of `freqs`, in dB — each band designed once. */
export function curveDb(bands, fs, freqs) {
  const ds = [];
  for (const b of bands) {
    if (!b.on) continue;
    const d = designBand(b, fs);
    if (d) ds.push(d);
  }
  return freqs.map((f) => {
    if (!ds.length) return 0;
    const h = (Math.PI * f) / fs, S = Math.sin(h), C = Math.cos(h);
    let prod = 1;
    for (const d of ds) prod *= mag2(d, S, C);
    return prod > 1e-30 ? 10 * Math.log10(prod) : -300;
  });
}
