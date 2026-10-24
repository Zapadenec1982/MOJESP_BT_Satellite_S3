#!/usr/bin/env python3
"""
fit_loudness.py — the loudness compensation's two shelves (DspChain::set_loudness,
components/audio_dsp), fitted to the ISO 226:2003 equal-loudness contours.

Turned down, music loses its bass and its top before its middle: the contours bunch up at both
ends. What gives the balance back is the difference between two contours — the one at the level
listened at and the one at the level the balance is judged at. Here that is 85 phon at the top
of the volume scale (100 % plays a full-scale source at the amplifier's ceiling), so at `a` dB
under the top the boost at f, re 1 kHz, is

    D(f, a) = [Lp(f, 85 − a) − Lp(1 kHz, 85 − a)] − [Lp(f, 85) − Lp(1 kHz, 85)].

The firmware has two RBJ shelves for it, a low one and a high one, whose gains are K·a. This
fits their corners, Qs and K to D over a = 10…50 dB:

  * re 1 kHz on both sides: the absolute level is free, the balance is what is fitted;
  * weighted from 40 Hz up (the SC-G101 this was made for rolls off below ~45 Hz, and the
    subsonic takes the rest), a little at 31.5 Hz, nothing at 20–25 Hz;
  * the high shelf held to Q ≤ 0.71 and a corner ≤ 10 kHz, and asked to stay level from
    12.5 kHz up — the contours stop at 12.5 kHz, and what lies above it is hiss and codec debris,
    which no loudness control should be lifting by 15 dB.

The shelves are the RBJ cookbook's analogue prototypes; the firmware's are those, bilinear, which
at 144 Hz is the same at every rate and at 10 kHz differs by 0.6 dB at 16 kHz between 44.1 and
192 kHz.

  python tools/eq/fit_loudness.py        # the fit, its error, and the C++ constants
Needs numpy (host only; the firmware build never runs this).
"""

import math

import numpy as np

# ISO 226:2003, the standard's table (f ≤ 12.5 kHz).
F = np.array([20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000,
              1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500], dtype=float)
AF = np.array([0.532, 0.506, 0.480, 0.455, 0.432, 0.409, 0.387, 0.367, 0.349, 0.330, 0.315, 0.301, 0.288,
               0.276, 0.267, 0.259, 0.253, 0.250, 0.246, 0.244, 0.243, 0.243, 0.243, 0.242, 0.242, 0.245,
               0.254, 0.271, 0.301])
LU = np.array([-31.6, -27.2, -23.0, -19.1, -15.9, -13.0, -10.3, -8.1, -6.2, -4.5, -3.1, -2.0, -1.1, -0.4,
               0.0, 0.3, 0.5, 0.0, -2.7, -4.1, -1.0, 1.7, 2.5, 1.2, -2.1, -7.1, -11.2, -10.7, -3.1])
TF = np.array([78.5, 68.7, 59.5, 51.1, 44.0, 37.5, 31.5, 26.5, 22.1, 17.9, 14.4, 11.4, 8.6, 6.2, 4.4, 3.0,
               2.2, 2.4, 3.5, 1.7, -1.3, -4.2, -6.0, -5.4, -1.5, 6.0, 12.6, 13.9, 12.3])
REF_PHON = 85.0
ATT = [10.0, 20.0, 30.0, 40.0, 50.0]


def spl(phon):
    af = 4.47e-3 * (10 ** (0.025 * phon) - 1.15) + (0.4 * 10 ** (((TF + LU) / 10) - 9)) ** AF
    return 10 / AF * np.log10(af) - LU + 94


K1 = int(np.where(F == 1000)[0][0])
FX = np.concatenate([F, [16000.0, 20000.0]])          # two points above the table, held level
K1X = int(np.where(FX == 1000)[0][0])
W = np.where(FX >= 40, 1.0, np.where(FX >= 31.5, 0.3, 0.0))
W[-2:] = 0.7


def target(a):
    lo, hi = spl(REF_PHON - a), spl(REF_PHON)
    t = (lo - lo[K1]) - (hi - hi[K1])
    return np.concatenate([t, [t[-1], t[-1]]])


def shelf_db(f, f0, g, q, high):
    """|H| of the RBJ cookbook's analogue shelf, dB (the firmware's analog_mag2)."""
    A = 10 ** (g / 40.0)
    s = 1j * f / f0
    if high:
        h = A * (A * s * s + math.sqrt(A) / q * s + 1) / (s * s + math.sqrt(A) / q * s + A)
    else:
        h = A * (s * s + math.sqrt(A) / q * s + A) / (A * s * s + math.sqrt(A) / q * s + 1)
    return 20 * np.log10(np.abs(h))


LO = np.array([30.0, 0.30, 0.0, 2000.0, 0.30, 0.0])
HI = np.array([600.0, 1.00, 1.0, 10000.0, 0.71, 1.0])


def model_rel(p, a):
    fl, ql, kl, fh, qh, kh = p
    m = shelf_db(FX, fl, kl * a, ql, False) + shelf_db(FX, fh, kh * a, qh, True)
    return m - m[K1X]


def resid(p):
    return np.concatenate([W * (model_rel(p, a) - target(a)) for a in ATT])


def fit(p0):
    """Levenberg–Marquardt with a numerical Jacobian, clamped to the bounds. Six parameters and
    two hundred residuals: no library needed."""
    p = np.clip(np.array(p0, dtype=float), LO, HI)
    lam = 1e-2
    r = resid(p)
    for _ in range(300):
        J = np.empty((r.size, p.size))
        for j in range(p.size):
            h = 1e-6 * max(1.0, abs(p[j]))
            q = p.copy()
            q[j] += h
            J[:, j] = (resid(q) - r) / h
        A = J.T @ J
        g = J.T @ r
        step = np.linalg.solve(A + lam * np.diag(np.diag(A) + 1e-12), -g)
        q = np.clip(p + step, LO, HI)
        rq = resid(q)
        if rq @ rq < r @ r:
            p, r, lam = q, rq, lam / 3
            if np.max(np.abs(step)) < 1e-9:
                break
        else:
            lam *= 4
            if lam > 1e8:
                break
    return p


def main():
    p = fit([140.0, 0.45, 0.48, 8000.0, 0.6, 0.15])
    fl, ql, kl, fh, qh, kh = p
    show = [40, 63, 100, 200, 500, 2000, 4000, 8000, 12500, 16000]
    print(f"low shelf {fl:.0f} Hz, Q {ql:.3f}, gain {kl:.4f}·a;  high shelf {fh:.0f} Hz, Q {qh:.3f}, gain {kh:.4f}·a")
    print("a dB  worst(≥40 Hz)  contour/fit at " + ", ".join(f"{f:g}" for f in show))
    for a in ATT:
        t, m = target(a), model_rel(p, a)
        worst = np.max(np.abs((m - t)[FX >= 40]))
        cells = " ".join(f"{t[list(FX).index(f)]:+.1f}/{m[list(FX).index(f)]:+.1f}" for f in show)
        print(f"{a:4.0f}  {worst:5.2f} dB      {cells}")
    print("\n// components/audio_dsp/include/dsp_chain.h")
    print(f"static constexpr float LOUD_LO_HZ = {fl:.1f}f, LOUD_LO_Q = {ql:.3f}f, LOUD_LO_K = {kl:.4f}f;")
    print(f"static constexpr float LOUD_HI_HZ = {fh:.1f}f, LOUD_HI_Q = {qh:.3f}f, LOUD_HI_K = {kh:.4f}f;")


if __name__ == "__main__":
    main()
