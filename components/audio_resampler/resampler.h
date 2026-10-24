////////////////////////////////////////////////////////////////////////////
//                         **** RESAMPLER ****                            //
//                     Sinc-based Audio Resampling                        //
//                Copyright (c) 2006 - 2023 David Bryant.                 //
//                          All Rights Reserved.                          //
//      Distributed under the BSD Software License (see license.txt)      //
////////////////////////////////////////////////////////////////////////////

// resampler.h

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#define _USE_MATH_DEFINES
#include <math.h>

#ifdef ENABLE_THREADS
#include "workers.h"
#endif

#if defined(PATH_WIDTH) && (PATH_WIDTH==64)
typedef double artsample_t;
#else
typedef float artsample_t;
#endif

#define SUBSAMPLE_INTERPOLATE   0x1
#define BLACKMAN_HARRIS         0x2
#define INCLUDE_LOWPASS         0x4
#define RESAMPLE_MULTITHREADED  0x8
#define NO_FILTER_REDUCTION     0x10
#define RESAMPLE_FIXED_RATIO    0x20    // internal use only, do not set
#define EXTRAPOLATE_ENDPOINTS   0x40
#define EXTRAPOLATE_PREFILL     0x80    // internal use only, do not set
#define EXTEND_CONVOLUTION_MATH 0x100
#define RESAMPLER_FLUSHED       0x200   // internal use only, do not set
#define RESAMPLER_SNAP_OFFSET   0x400   // internal use only, do not set

typedef struct {
    unsigned int input_used, output_generated;
} ResampleResult;

typedef struct resample {
    int numChannels, numSamples, numFilters, numTaps, inputIndex, flags;
    double *tempFilter, outputOffset, fixedRatio, lowpassRatio;
    /* MODESP: was `double (*)(..., double offset)`.
     *
     * The phase is now handed in already split into an integer sample index and a float
     * fraction, and the result comes back as a sample rather than a double. On a target
     * without double-precision hardware — this one is built -mabi=ilp32f — the old
     * signature cost two soft-float library calls at every call site just to convert the
     * argument and the return, on top of the six inside. */
    artsample_t (*subsample)(struct resample *cxt, artsample_t *source, int base, float frac);
    artsample_t **buffers, **filters;
    /* MODESP: the filter bank as ONE block; filters[i] indexes into it. Upstream made a
     * separate allocation per phase, which on this board meant 161 x 192 bytes of internal
     * SRAM per converter. See the comment on bank_calloc() in resampler.c. */
    artsample_t *filterBank;

#ifdef ENABLE_THREADS
    Workers *workers;
    const artsample_t *input;
    artsample_t *cbuffer, *output;
    int numInputFrames, numOutputFrames, stride;
    ResampleResult res;
    double ratio;
#endif
} Resample;

#ifdef __cplusplus
extern "C" {
#endif

Resample *resampleInit (int numChannels, int numTaps, int numFilters, double lowpassRatio, int flags);
Resample *resampleFixedRatioInit (int numChannels, int numTaps, int maxFilters, double sourceRate, double destinRate, int lowpassFreq, int flags);
ResampleResult resampleProcess (Resample *cxt, const artsample_t *const *input, int numInputFrames, artsample_t *const *output, int numOutputFrames, double ratio);
ResampleResult resampleProcessInterleaved (Resample *cxt, const artsample_t *input, int numInputFrames, artsample_t *output, int numOutputFrames, double ratio);
ResampleResult resampleProcessAndFlush (Resample *cxt, const artsample_t *const *input, int numInputFrames, artsample_t *const *output, int numOutputFrames, double ratio);
ResampleResult resampleProcessAndFlushInterleaved (Resample *cxt, const artsample_t *input, int numInputFrames, artsample_t *output, int numOutputFrames, double ratio);
unsigned int resampleGetRequiredSamples (Resample *cxt, int numOutputFrames, double ratio);
unsigned int resampleGetExpectedOutput (Resample *cxt, int numInputFrames, double ratio);
void resampleAdvancePosition (Resample *cxt, double delta);
double resampleGetLowpassRatio (Resample *cxt);
double resampleGetPosition (Resample *cxt);
int resampleGetNumFilters (Resample *cxt);
int resampleInterpolationUsed (Resample *cxt);
void resampleReset (Resample *cxt);
void resampleFree (Resample *cxt);

#ifdef __cplusplus
}
#endif
