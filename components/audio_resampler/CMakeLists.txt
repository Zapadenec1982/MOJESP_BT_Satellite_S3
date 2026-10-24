# dbry/audio-resampler (BSD-3-Clause, license.txt) — windowed-sinc resampler with a
# continuously adjustable ratio, used as the ASRC for asynchronous sources (BT module,
# network streams) in modules/audio. Vendored from
# https://github.com/dbry/audio-resampler (master, 2026-03), local changes marked MODESP:;
# only resampler + extrapolator, no threads (ENABLE_THREADS off), 32-bit float path.
#
# Local changes, each marked `MODESP:` at the site:
#   resampler.h/.c  subsample() takes the phase pre-split into index + float fraction, so a
#                   -mabi=ilp32f build does not pay two soft-float calls per sample;
#   resampler.c     the interpolation filter bank is ONE allocation out of PSRAM instead of
#                   one small internal-SRAM allocation per phase.
#   resampler.c     allocation failures release partial state; interpolated phases rounded
#                   to the endpoint use the final filter without reading a following row.
#   resampler.c     ordinary FIR returns float; short variable-ratio interleaved blocks use
#                   bounded Q32 phase; required-input queries avoid a per-frame dry run.
idf_component_register(
    SRCS "resampler.c" "extrapolator.c"
    INCLUDE_DIRS "."
)

# Speed here, size everywhere else.
#
# The project builds with -Os, which is the right default for control code and the wrong one
# for a per-sample loop: it will not unroll the FIR and keeps the code small at the cost of
# running it slowly. Measured on this board, MP3 at 44.1 kHz through the resampler took 76% of
# a core and managed only 1.15x real time, so refilling the two-second ring after a track
# switch took thirteen seconds — and the underruns in between were audible as a click.
#
# Raising the flag for the whole firmware would trade size and behaviour everywhere to fix one
# hot spot. -O2 on just the components that run per sample is the trade actually being asked
# for.
target_compile_options(${COMPONENT_LIB} PRIVATE -O2 -Wno-unused-function -Wno-unused-variable)
