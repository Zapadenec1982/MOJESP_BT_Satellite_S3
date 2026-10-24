/**
 * @file uac_tone.h
 * @brief One clean tone, followed through the recurrence a sampled sinusoid obeys — the U0
 *        stream check (usb_uac.h). Pure C++: tests/host/test_uac_tone.cpp runs it on the PC.
 *
 * x[n] = 2c·x[n−1] − x[n−2], c = cos(2πf/fs). Each second fits c by least squares; the next second
 * checks every sample against that fit. A lost or repeated packet is a phase jump, and a phase
 * jump leaves a residual of the order of the tone itself — far above 16-bit quantisation, so a
 * threshold of 5 % of the tone's peak tells the two apart. Breaks are counted only across two
 * seconds that were both the same clean tone: music, or a tone being retuned, counts nothing.
 */

#pragma once

#include <cmath>
#include <cstdint>

namespace usb_uac {

struct Tone {
    double   s1 = 0, s2 = 0, s3 = 0, sig = 0;   ///< Σx1(x+x2), Σx1², Σ(x+x2)², Σx²
    float    x1 = 0, x2 = 0;                   ///< the two previous samples
    uint32_t seen = 0;          ///< samples since restart; the recurrence needs two
    float    c_prev = 0;        ///< last second's fit
    float    amp_prev = 0;      ///< last second's peak
    bool     prev_ok = false;   ///< last second was one clean tone
    bool     in_break = false;
    uint32_t breaks = 0;        ///< this second's

    void restart() { *this = Tone{}; }

    void push(float x) {
        if (seen >= 2) {
            bool bad = false;
            if (prev_ok) {
                const float r = x - (2.f * c_prev * x1 - x2);
                bad = std::fabs(r) > 0.05f * amp_prev;
                if (bad && !in_break) breaks++;
                in_break = bad;
            }
            // A break stays out of the fit: otherwise the samples that make one would make
            // their own second look like no clean tone, and the break would not be counted.
            if (!bad) {
                const float sum = x + x2;
                s1 += double(x1) * sum;
                s2 += double(x1) * x1;
                s3 += double(sum) * sum;
                sig += double(x) * x;
            }
        } else {
            seen++;
        }
        x2 = x1;
        x1 = x;
    }

    /// Close a second of `fs` samples whose peak was `peak` (full scale = 1). Returns the tone's
    /// frequency, or 0 if the second was not one clean tone; adds the second's breaks to `total`
    /// only when this second and the one before were the same clean tone.
    float close(uint32_t fs, float peak, uint32_t& total) {
        float hz = 0;
        float c = 0;
        bool ok = false;
        if (s2 > 0 && sig > 0 && peak > 0.01f) {
            c = float(s1 / (2.0 * s2));
            // Σr² at the best-fit c, against Σ(x − 2x1 + x2)²: what the fit had to explain beyond
            // c = 1. Against Σx² any smooth low-frequency signal would pass for one tone (two
            // tones at 1000 and 1531 Hz leave 1e-4 of Σx², but 0.14 of the second difference).
            // A 1 kHz test tone at 16 bits leaves ~1e-5; below ~300 Hz quantisation alone fails.
            const double resid = s3 - s1 * s1 / s2;
            const double d2 = s3 - 4.0 * s1 + 4.0 * s2;
            ok = c > -1.f && c < 1.f && d2 > 0 && resid / d2 < 0.01;
            if (ok) hz = std::acos(c) * float(fs) / (2.f * float(M_PI));
        }
        if (ok && prev_ok && std::fabs(c - c_prev) < 1e-4f) total += breaks;
        prev_ok  = ok;
        c_prev   = c;
        amp_prev = peak;
        s1 = s2 = s3 = sig = 0;
        breaks = 0;
        return hz;
    }
};

}  // namespace usb_uac
