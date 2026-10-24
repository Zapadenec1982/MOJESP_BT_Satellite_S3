/* memcpy/memset for the harness: leaves, like the target's. Built with
   -fno-tree-loop-distribute-patterns so GCC cannot turn these loops back into calls. */
#include <stddef.h>

void* memcpy(void* d, const void* s, size_t n) {
    unsigned char* dp = d;
    const unsigned char* sp = s;
    while (n--) *dp++ = *sp++;
    return d;
}

void* memset(void* d, int c, size_t n) {
    unsigned char* dp = d;
    while (n--) *dp++ = (unsigned char)c;
    return d;
}
