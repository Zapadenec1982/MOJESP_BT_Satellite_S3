/**
 * @file spectrum.h
 * @brief Real-time spectrum analyser and the bus that carries it to the UI.
 *
 * Why a bus and not a SharedState key: 28 band levels at 90 Hz is a small array on a fast
 * path, and SharedState is a key/value store written from the main loop. Pushing it through
 * there would mean 28 keys or a blob round-trip per frame, for data whose only consumer is
 * one widget.
 *
 * SpectrumBus is a seqlock: the audio task writes, the LVGL task reads, neither ever blocks
 * the other, and a reader that catches a half-written frame retries instead of drawing torn
 * data. That matters because the writer is the audio task — it must never wait on the UI.
 *
 * The analyser itself lives here rather than in the audio module so it is testable on the
 * host, where the FFT kernel is a plain reference implementation.
 */

#pragma once

#include <cstddef>
#include <cstdint>

namespace audio_core {

constexpr int SPECTRUM_BANDS = 28;      ///< as the design draws
/// 1024 points at 48 kHz = 46.9 Hz per bin, with 50 % overlap giving ~94 frames a second.
/// The window size is a straight trade: it buys low-frequency resolution with internal RAM
/// (1024 costs 12 KB of working buffers) and with latency. Even here the lowest bands are
/// narrower than one bin — a log-spaced band at f is about 0.24*f wide, so bands stop
/// sharing bins only above ~200 Hz. That is inherent to any FFT analyser with log bands;
/// the display shows it rather than interpolating a resolution that is not there.
constexpr int SPECTRUM_FFT_N = 1024;
constexpr int SPECTRUM_HOP   = SPECTRUM_FFT_N / 2;
constexpr float SPECTRUM_FLOOR_DB = -60.0f;

/// Ceiling for a band level, and the level a band display should put at the top of the row.
///
/// A band is not the whole signal. A full-scale sound spread evenly across the log-spaced
/// bands leaves 10*log10(SPECTRUM_BANDS) less in each one than in the total — about 14.5 dB
/// here. Drawing a row from 0 dBFS downwards therefore wastes 14.5 dB at the top that
/// nothing can reach, while ordinary music, whose strongest bands sit near −10 dBFS, pegs
/// against it anyway: the row reads as permanently clipped without ever being full.
///
/// Referencing the top to the band figure spreads real programme material across the row
/// and leaves the last stretch for a band that genuinely is at or above full scale, which
/// EQ boost can produce. The ceiling has to move with it, or the top of the row would be
/// unreachable by construction.
constexpr float SPECTRUM_BAND_REF_DB = 14.5f;      // 10*log10(28)
static_assert(SPECTRUM_BANDS == 28, "SPECTRUM_BAND_REF_DB is 10*log10(SPECTRUM_BANDS)");
constexpr float SPECTRUM_F_LO = 40.0f;
constexpr float SPECTRUM_F_HI = 16000.0f;

/// Log-spaced band analyser over a Hann-windowed FFT of the mono sum.
class SpectrumAnalyzer {
public:
    void reset();

    /// Feed interleaved stereo. Returns true when a new frame of bands became available —
    /// the caller publishes only then, so the bus is not rewritten for every audio block.
    ///
    /// Above 50 kHz the mono sum is averaged over 2 (≤ 100 kHz) or 4 frames first, so the FFT
    /// runs on a 44.1/48 kHz-equivalent stream at every output rate: the same ~94 frames a
    /// second and the same resolution, instead of four times the FFTs at 192 kHz (16.7 % of
    /// a core) for bins the display does not draw — it stops at 16 kHz.
    bool feed(const float* stereo, size_t frames);
    /// Frames averaged into one analysed sample at the current rate (1, 2 or 4).
    int decimation() const { return dec_; }

    /// Band levels in dBFS, SPECTRUM_FLOOR_DB..SPECTRUM_BAND_REF_DB. Valid after feed()
    /// returned true.
    const float* bands_db() const { return bands_; }

    /// Add `db` back to every band before it is reported.
    ///
    /// The analyser sits at the end of the chain, after the volume control, so without this
    /// it shows how loud you are LISTENING rather than what is playing. At an ordinary
    /// setting the volume stage cuts around 36 dB, which drops every band by 36 dB and
    /// flattens the whole row against the floor — the display appeared to have almost no
    /// sensitivity, and that is exactly what it was measuring.
    ///
    /// Tapping the signal before the volume stage instead would mean carrying a second copy
    /// of every block. Volume is a single scalar gain, so adding it back is the same
    /// arithmetic for none of the cost.
    ///
    /// The limiter's gain reduction is deliberately NOT compensated here: when the limiter
    /// works, that should be visible.
    void set_gain_offset_db(float db) { offset_db_ = db; }

private:
    void analyse();
    /// The bin range of every band at `fs`, built once per sample rate. It used to be
    /// worked out per frame — two powf() per band, 94 times a second — for a table that
    /// only changes when the output clock does.
    void build_bands(float fs);

    float acc_[SPECTRUM_FFT_N] = {};     ///< mono window; the second half becomes the first
    int   fill_ = 0;
    float cplx_[2 * SPECTRUM_FFT_N] = {};
    float bands_[SPECTRUM_BANDS] = {};
    float offset_db_ = 0.0f;
    int   lo_[SPECTRUM_BANDS] = {};      ///< first bin of each band, at bands_fs_
    int   hi_[SPECTRUM_BANDS] = {};      ///< one past the last
    float bands_fs_ = 0.0f;              ///< the rate the table was built for; 0 = not yet
    int   dec_ = 1, dec_k_ = 0;          ///< frames averaged per analysed sample, and so far
    float dec_acc_ = 0.0f;
};

/// Single-writer / single-reader seqlock carrying one frame of band levels.
class SpectrumBus {
public:
    void publish(const float* db);
    /// Copies the latest complete frame. False if nothing has ever been published or the
    /// writer kept winning — the caller simply keeps the previous frame on screen.
    bool read(float* db) const;

private:
    mutable volatile uint32_t seq_ = 0;
    float db_[SPECTRUM_BANDS] = {};
};

/// The one instance, shared by the audio core (writer) and the UI (reader).
SpectrumBus& spectrum_bus();

/// Centre frequency of band `i`, for axis labels.
float spectrum_band_hz(int i);

} // namespace audio_core
