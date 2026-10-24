/**
 * @file dsp_kernels.cpp
 * @brief Stereo SVF loops (dsp_kernels.h). Built with -ffp-contract=off (CMakeLists.txt).
 *
 * Both channels go through one loop. The P4's core is in order, and one channel's recursion
 * is a chain — v3, then v2, then the new s2 that the next sample's v3 needs — so a loop over
 * one channel waits on the FPU's latency at every step; the other channel's chain is
 * independent and fills those waits. It is also half the loop overhead and no de-interleave:
 * the block is filtered where it lies.
 */

#include "dsp_kernels.h"

namespace audio_core {

void svf_stereo(float* x, size_t frames, const SvfCoef& c, SvfState& st) {
    const SvfCoef k = c;                     // by value: the loop must not reload through a reference
    float l1 = st.s[0], l2 = st.s[1], r1 = st.s[2], r2 = st.s[3];
    for (size_t i = 0; i < frames; i++) {
        const float yl = svf_tick(x[2 * i], k, l1, l2);
        const float yr = svf_tick(x[2 * i + 1], k, r1, r2);
        x[2 * i]     = yl;
        x[2 * i + 1] = yr;
    }
    st.s[0] = l1; st.s[1] = l2; st.s[2] = r1; st.s[3] = r2;
}

void svf_stereo2(float* x, size_t frames, const SvfCoef& ca, SvfState& sa, const SvfCoef& cb, SvfState& sb) {
    // Twelve coefficients and eight states held in registers for the whole block: 20 of the
    // FPU's 32, which leaves the scheduler room for both channels' chains of both sections.
    const SvfCoef a = ca, b = cb;
    float al1 = sa.s[0], al2 = sa.s[1], ar1 = sa.s[2], ar2 = sa.s[3];
    float bl1 = sb.s[0], bl2 = sb.s[1], br1 = sb.s[2], br2 = sb.s[3];
    for (size_t i = 0; i < frames; i++) {
        const float yl = svf_tick(svf_tick(x[2 * i], a, al1, al2), b, bl1, bl2);
        const float yr = svf_tick(svf_tick(x[2 * i + 1], a, ar1, ar2), b, br1, br2);
        x[2 * i]     = yl;
        x[2 * i + 1] = yr;
    }
    sa.s[0] = al1; sa.s[1] = al2; sa.s[2] = ar1; sa.s[3] = ar2;
    sb.s[0] = bl1; sb.s[1] = bl2; sb.s[2] = br1; sb.s[3] = br2;
}

void svf_stereo_to(const float* __restrict in, float* __restrict out, size_t frames,
                   const SvfCoef& c, SvfState& st) {
    const SvfCoef k = c;
    float l1 = st.s[0], l2 = st.s[1], r1 = st.s[2], r2 = st.s[3];
    for (size_t i = 0; i < frames; i++) {
        out[2 * i]     = svf_tick(in[2 * i], k, l1, l2);
        out[2 * i + 1] = svf_tick(in[2 * i + 1], k, r1, r2);
    }
    st.s[0] = l1; st.s[1] = l2; st.s[2] = r1; st.s[3] = r2;
}

void svf_stereo_ramp(float* x, size_t frames, const SvfCoef& a, const SvfCoef& b, SvfState& st) {
    if (frames == 0) return;
    const float inv = 1.0f / (float)frames;
    SvfCoef d;
    d.c1 = (b.c1 - a.c1) * inv; d.c2 = (b.c2 - a.c2) * inv; d.c3 = (b.c3 - a.c3) * inv;
    d.m0 = (b.m0 - a.m0) * inv; d.m1 = (b.m1 - a.m1) * inv; d.m2 = (b.m2 - a.m2) * inv;
    float l1 = st.s[0], l2 = st.s[1], r1 = st.s[2], r2 = st.s[3];
    for (size_t i = 0; i + 1 < frames; i++) {
        const float t = (float)(i + 1);
        SvfCoef k;
        k.c1 = std::fmaf(t, d.c1, a.c1); k.c2 = std::fmaf(t, d.c2, a.c2); k.c3 = std::fmaf(t, d.c3, a.c3);
        k.m0 = std::fmaf(t, d.m0, a.m0); k.m1 = std::fmaf(t, d.m1, a.m1); k.m2 = std::fmaf(t, d.m2, a.m2);
        const float yl = svf_tick(x[2 * i], k, l1, l2);
        const float yr = svf_tick(x[2 * i + 1], k, r1, r2);
        x[2 * i]     = yl;
        x[2 * i + 1] = yr;
    }
    // The last frame on `b` itself, not on a sum that rounds next to it: the next block runs on b.
    const size_t i = frames - 1;
    const SvfCoef k = b;
    const float yl = svf_tick(x[2 * i], k, l1, l2);
    const float yr = svf_tick(x[2 * i + 1], k, r1, r2);
    x[2 * i]     = yl;
    x[2 * i + 1] = yr;
    st.s[0] = l1; st.s[1] = l2; st.s[2] = r1; st.s[3] = r2;
}

}  // namespace audio_core
