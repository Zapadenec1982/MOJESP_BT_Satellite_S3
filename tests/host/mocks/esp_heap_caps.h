/**
 * @file esp_heap_caps.h
 * @brief HOST BUILD: heap_caps_* → the C heap. Capability flags are accepted and ignored.
 */
#pragma once

#include <cstddef>
#include <cstdlib>

#define MALLOC_CAP_8BIT      (1u << 0)
#define MALLOC_CAP_INTERNAL  (1u << 1)
#define MALLOC_CAP_SPIRAM    (1u << 2)
#define MALLOC_CAP_DMA       (1u << 3)

static inline void* heap_caps_malloc(size_t size, unsigned /*caps*/)            { return std::malloc(size); }
static inline void* heap_caps_calloc(size_t n, size_t size, unsigned /*caps*/)  { return std::calloc(n, size); }
static inline void  heap_caps_free(void* p)                                      { std::free(p); }

/// The capability-preference form. On the device it tries each capability set in turn; here
/// there is one heap, so the first argument set is as good as any.
static inline void* heap_caps_calloc_prefer(size_t n, size_t size, size_t /*num_caps*/, ...) {
    return std::calloc(n, size);
}

/// Free-size queries exist so a caller can LOG how much room is left. A host build has no
/// such notion; report a big, constant number rather than a zero that reads like exhaustion.
static inline size_t heap_caps_get_free_size(unsigned /*caps*/)           { return 1u << 20; }
static inline size_t heap_caps_get_largest_free_block(unsigned /*caps*/)  { return 1u << 20; }
