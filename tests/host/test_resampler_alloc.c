/* Compile the production C kernel with a private symbol prefix and allocator.
 * This exercises every init allocation without changing firmware allocators. */
#include <stdlib.h>
#include <assert.h>

static int fail_at, calls, live;
void resampler_alloc_fail_at(int index) { assert(live == 0); fail_at = index; calls = 0; }
int resampler_alloc_live(void) { return live; }
int resampler_alloc_calls(void) { return calls; }
static void *test_malloc(size_t size) {
    void *p;
    if (++calls == fail_at) return NULL;
    p = malloc(size);
    if (p) ++live;
    return p;
}
static void *test_calloc(size_t n, size_t size) {
    void *p;
    if (++calls == fail_at) return NULL;
    p = calloc(n, size);
    if (p) ++live;
    return p;
}
static void test_free(void *p) { if (p) { --live; free(p); } }

#define malloc test_malloc
#define calloc test_calloc
#define free test_free
#define resampleInit alloc_resampleInit
#define resampleFixedRatioInit alloc_resampleFixedRatioInit
#define resampleProcess alloc_resampleProcess
#define resampleProcessInterleaved alloc_resampleProcessInterleaved
#define resampleProcessAndFlush alloc_resampleProcessAndFlush
#define resampleProcessAndFlushInterleaved alloc_resampleProcessAndFlushInterleaved
#define resampleGetRequiredSamples alloc_resampleGetRequiredSamples
#define resampleGetExpectedOutput alloc_resampleGetExpectedOutput
#define resampleAdvancePosition alloc_resampleAdvancePosition
#define resampleGetLowpassRatio alloc_resampleGetLowpassRatio
#define resampleGetPosition alloc_resampleGetPosition
#define resampleGetNumFilters alloc_resampleGetNumFilters
#define resampleInterpolationUsed alloc_resampleInterpolationUsed
#define resampleReset alloc_resampleReset
#define resampleFree alloc_resampleFree
#include "../../components/audio_resampler/resampler.c"
