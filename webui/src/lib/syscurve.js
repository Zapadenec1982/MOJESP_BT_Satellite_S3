/*
 * «Показати всю систему» (docs/PLAN_SOUND_UX_2026-10-14.md, appendix A, the equalizer): what the
 * stages around the equalizer do to the sound, drawn with it — Бас and Високі, the loudness at the
 * level it is at now, the subsonic and the port's lift. Each is the audio core's own section
 * (components/audio_dsp/src/dsp_chain.cpp), built from dsp_chain.h's numbers and designed by
 * lib/eqmath.js as the core designs it: the tone, the subsonic and the lift by design_svf_params,
 * the loudness shelves bilinear (shelf_coef).
 *
 * Not drawn: what is not a fixed response — the bass limiter and the brickwall act on the level,
 * the width and the mono bass on the stereo image, the balance on one side.
 *
 * No imports but the maths: tools/tests/test_webui_eqfile.py runs it in Node.
 */
import { designBand, svfMagnitudeDb, curveDb } from './eqmath.js';

// dsp_chain.h
const TONE_LO_HZ = 100, TONE_HI_HZ = 10000, TONE_Q = 0.71, TONE_MAX_DB = 6;
const LOUD_LO_HZ = 148.6, LOUD_LO_Q = 0.359, LOUD_LO_K = 0.5605;
const LOUD_HI_HZ = 10000, LOUD_HI_Q = 0.71, LOUD_HI_K = 0.2253;
const SUB_Q = [0.5411961, 1.306563];      // a 4th-order Butterworth as two sections (design_subsonic)
const PORT_Q = 1.4, PORT_SUB_RATIO = 0.8, PORT_LIFT_MAX = 6;

const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);

/**
 * The stages as bands: `bass` and `treble` dB (audio.bass_db, treble_db), `loudDb` the loudness's
 * low shelf now (audio.loud_db), `hpHz` the subsonic asked for (audio.hp_hz), `portHz` and
 * `liftDb` the port that plays (0 for closed speakers, lib/speakers.js port()). `bilinear` marks
 * the loudness's shelves.
 */
export function systemBands({ bass = 0, treble = 0, loudDb = 0, hpHz = 0, portHz = 0, liftDb = 0 } = {}) {
  const out = [];
  const fc = portHz > 0 ? PORT_SUB_RATIO * portHz : hpHz;
  if (fc >= 20 && fc <= 80) for (const q of SUB_Q) out.push({ type: 'highpass', f: fc, gain: 0, q, on: true });
  if (portHz > 0 && liftDb > 0) out.push({ type: 'peak', f: portHz, gain: Math.min(PORT_LIFT_MAX, liftDb), q: PORT_Q, on: true });
  if (loudDb > 0) {
    out.push({ type: 'lowshelf', f: LOUD_LO_HZ, gain: loudDb, q: LOUD_LO_Q, on: true, bilinear: true });
    out.push({ type: 'highshelf', f: LOUD_HI_HZ, gain: (loudDb * LOUD_HI_K) / LOUD_LO_K, q: LOUD_HI_Q, on: true, bilinear: true });
  }
  if (bass) out.push({ type: 'lowshelf', f: TONE_LO_HZ, gain: clamp(bass, -TONE_MAX_DB, TONE_MAX_DB), q: TONE_Q, on: true });
  if (treble) out.push({ type: 'highshelf', f: TONE_HI_HZ, gain: clamp(treble, -TONE_MAX_DB, TONE_MAX_DB), q: TONE_Q, on: true });
  return out;
}

/** The whole response at each of `freqs`, in dB: the equalizer's `bands` (when `eqOn`) and the stages. */
export function systemCurveDb(bands, stages, fs, freqs, eqOn = true) {
  const base = eqOn ? curveDb(bands, fs, freqs) : freqs.map(() => 0);
  const ds = stages.map((b) => designBand(b, fs, !b.bilinear)).filter(Boolean);
  return freqs.map((f, i) => ds.reduce((db, d) => db + svfMagnitudeDb(d, fs, f), base[i]));
}
