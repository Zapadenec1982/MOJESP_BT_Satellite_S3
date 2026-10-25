/**
 * @file dsp_kernels.h
 * @brief The per-sample loops of the EQ: one trapezoidal state-variable filter section,
 *        both channels of an interleaved block, in place.
 *
 * Why this structure and not a biquad. The Direct Form II biquad this replaced keeps its
 * delay line at the signal divided by (1 + a1 + a2), which for a 40 Hz band is tens of
 * thousands at 96 kHz; its output is a difference of numbers that large, so float32's
 * rounding came out as broadband noise riding on the bass — measured at −36 dB under the
 * signal at 96 kHz and −23 dB at 192 kHz with the customer's EQ.
 * That was the "grit" that went away with the EQ switched off. Every direct form has
 * the same problem in single precision at low f0 and high fs; the trapezoidal SVF (A. Simper,
 * cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf) keeps its integrators at signal level
 * and measured −125/−114 dB in the same case, for the same frequency response (it is the RBJ
 * response, bilinear and prewarped alike — see design_svf in dsp_chain.h).
 *
 * Arithmetic is spelled out with fmaf and this file is compiled with -ffp-contract=off, so
 * the compiler fuses nothing on its own: the host build (tests/host) and the P4 then round
 * every operation the same way, and a checksum taken on one is valid on the other.
 */

#pragma once

#include <cmath>
#include <cstddef>

namespace audio_core {

/// One section. Loop: c1 = 1/(1 + g(g + k)), c2 = g·c1, c3 = g·c2. Output: y = m0·x + m1·v1
/// + m2·v2, where v1 is the band-pass and v2 the low-pass node — every second-order response
/// is a choice of {m0, m1, m2}. The default is a wire.
struct SvfCoef {
    float c1 = 1.0f, c2 = 0.0f, c3 = 0.0f;
    float m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;
};

/// Integrator states of one section for both channels: {ic1 L, ic2 L, ic1 R, ic2 R}.
struct SvfState {
    float s[4] = {0.0f, 0.0f, 0.0f, 0.0f};
};

/// Run one section over `frames` interleaved stereo frames, in place.
void svf_stereo(float* x, size_t frames, const SvfCoef& c, SvfState& st);

/// Two sections in one pass: `a`, then `b` on what `a` gave, frame by frame. Bit for bit what
/// svf_stereo(a) and then svf_stereo(b) give — every sample meets the same operations in the
/// same order, only the loop around them is shared.
///
/// Why: a section is 20 floating-point operations a frame, and its own loop adds two loads, two
/// stores and the loop's count and branch — 26 instructions, 28 cycles on the P4's in-order core.
/// Two sections in one loop pay that once: 46 instructions for the pair. Three no longer fit:
/// 30 coefficients and states for 32 FPU registers, and the compiled loop spills to the stack.
void svf_stereo2(float* x, size_t frames, const SvfCoef& a, SvfState& sa, const SvfCoef& b, SvfState& sb);

/// The same section from `in` to `out` (which must not overlap).
void svf_stereo_to(const float* in, float* out, size_t frames, const SvfCoef& c, SvfState& st);

/// The same section while it is being retuned: every coefficient moves in a straight line from
/// `a` (before the first frame) to `b` (from the last frame on). A coefficient that jumps at a
/// block boundary is a step in the output, heard as a tick on a bass note; the same change
/// spread across the block is not.
void svf_stereo_ramp(float* x, size_t frames, const SvfCoef& a, const SvfCoef& b, SvfState& st);

/// Zero any state that has decayed below 1e-15. After a silence an IIR's integrators sink
/// into subnormal numbers and can stay there under round-to-nearest; zero is exactly where
/// they were heading, and the next signal starts them again from rest.
inline void svf_flush(SvfState& st) {
    for (float& v : st.s) if (v < 1e-15f && v > -1e-15f) v = 0.0f;
}

/// The two inner nodes of one section for one sample: v1 (band-pass) and v2 (low-pass), for
/// the stages that need more than one output from the same filter (the bass limiter's split).
inline void svf_nodes(float x, const SvfCoef& c, float& s1, float& s2, float& v1, float& v2) {
    const float v3 = x - s2;
    v1 = std::fmaf(c.c2, v3, c.c1 * s1);
    v2 = std::fmaf(c.c3, v3, std::fmaf(c.c2, s1, s2));
    s1 = std::fmaf(2.0f, v1, -s1);
    s2 = std::fmaf(2.0f, v2, -s2);
}

/// One sample of one section — the definition the stereo loops are held to, bit for bit.
inline float svf_tick(float x, const SvfCoef& c, float& s1, float& s2) {
    const float v3 = x - s2;
    const float v1 = std::fmaf(c.c2, v3, c.c1 * s1);
    // Not (1 − c3)·s2 + c3·x: at a low corner c3 is ~1e-5 and 1 − c3 in float throws the
    // state's precision away, which is the very thing this structure exists to keep.
    const float v2 = std::fmaf(c.c3, v3, std::fmaf(c.c2, s1, s2));
    s1 = std::fmaf(2.0f, v1, -s1);
    s2 = std::fmaf(2.0f, v2, -s2);
    return std::fmaf(c.m2, v2, std::fmaf(c.m1, v1, c.m0 * x));
}

}  // namespace audio_core
