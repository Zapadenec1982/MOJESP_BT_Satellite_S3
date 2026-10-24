/**
 * @file dsps_fft_ref.h (HOST MOCK)
 * @brief Radix-2 decimation-in-time FFT standing in for esp-dsp on the host.
 *
 * Same contract as the pair dsps_fft2r_fc32() + dsps_bit_rev_fc32() used on target: in-place
 * over interleaved complex floats, output in natural bin order. Written out rather than
 * pulled in so the host build has no dependency on esp-dsp, and so the spectrum analyser can
 * be tested against known tones instead of only against itself.
 */
#pragma once

#include <cmath>

static inline void dsps_fft_ref(float* data, int n) {
    // bit reversal
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            float tr = data[2 * i],     ti = data[2 * i + 1];
            data[2 * i] = data[2 * j];  data[2 * i + 1] = data[2 * j + 1];
            data[2 * j] = tr;           data[2 * j + 1] = ti;
        }
    }
    // butterflies
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * M_PI / (double)len;
        for (int i = 0; i < n; i += len) {
            for (int k = 0; k < len / 2; k++) {
                const double a = ang * (double)k;
                const float wr = (float)cos(a), wi = (float)sin(a);
                const int p = 2 * (i + k), q = 2 * (i + k + len / 2);
                const float xr = data[q] * wr - data[q + 1] * wi;
                const float xi = data[q] * wi + data[q + 1] * wr;
                data[q]     = data[p] - xr;
                data[q + 1] = data[p + 1] - xi;
                data[p]     += xr;
                data[p + 1] += xi;
            }
        }
    }
}
