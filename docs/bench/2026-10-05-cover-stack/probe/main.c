/*
 * Stack probe for the firmware's own cover_soft.c.obj, run on QEMU "virt" (RV32).
 *
 * The object under test is linked unmodified from the esp32p4 build, so every frame below
 * cover_soft_info()/cover_soft_decode() is the one the panel runs. What this file supplies
 * is what that object imports: heap_caps_malloc/realloc, free, memcpy, memset.
 *
 * Method: paint the stack below the caller's SP with a pattern, make the call, scan up from
 * the bottom for the first word that is no longer the pattern. Twice, with two patterns, so
 * a frame that happens to store the pattern cannot hide a word.
 */
#include <stddef.h>
#include <stdint.h>
#include "images.h"

int            cover_soft_info(const unsigned char* jpg, int len, int* w, int* h);
unsigned char* cover_soft_decode(const unsigned char* jpg, int len, int* w, int* h);
void           cover_soft_free(unsigned char* px);
const char*    cover_soft_reason(void);

/* ── console and exit ─────────────────────────────────────────────────────────────────── */
#define UART ((volatile uint8_t*)0x10000000)
static void out_c(char c) {
    while (!(UART[5] & 0x20)) {}
    UART[0] = (uint8_t)c;
}
static void out_s(const char* s) { while (*s) out_c(*s++); }
static void out_u(uint32_t v, int width) {
    char b[12];
    int i = 0;
    do { b[i++] = (char)('0' + v % 10); v /= 10; } while (v);
    for (int pad = width - i; pad > 0; pad--) out_c(' ');
    while (i) out_c(b[--i]);
}
static void out_x(uint32_t v) {
    out_s("0x");
    for (int s = 28; s >= 0; s -= 4) out_c("0123456789abcdef"[(v >> s) & 15]);
}
static void out_pad(const char* s, int width) {
    int n = 0;
    while (s[n]) out_c(s[n++]);
    for (; n < width; n++) out_c(' ');
}

void finish(int code) {
    volatile uint32_t* test = (volatile uint32_t*)0x100000;   /* sifive_test on virt */
    *test = code ? (0x3333u | ((uint32_t)code << 16)) : 0x5555u;
    for (;;) {}
}

void on_trap(uint32_t cause, uint32_t epc, uint32_t tval) {
    out_s("\nTRAP mcause=");
    out_x(cause);
    out_s(" mepc=");
    out_x(epc);
    out_s(" mtval=");
    out_x(tval);
    out_s("\n");
    finish(3);
}

/* ── TLS: stbi__g_failure_reason and the flip flags are __thread ─────────────────────── */
extern char _tdata_start[], _tdata_end[], _tbss_start[], _tbss_end[];
static uint8_t tls_block[256] __attribute__((aligned(16)));
void tls_init(void) {
    const uint32_t data = (uint32_t)(_tdata_end - _tdata_start);
    const uint32_t total = (uint32_t)(_tbss_end - _tdata_start);
    if (total > sizeof(tls_block)) { out_s("TLS too big\n"); finish(4); }
    for (uint32_t i = 0; i < total; i++) tls_block[i] = i < data ? (uint8_t)_tdata_start[i] : 0;
    __asm__ volatile("mv tp, %0" ::"r"(tls_block));
}

/* ── heap: a bump allocator, reset after every image ─────────────────────────────────── */
extern uint8_t _heap_start[], _heap_end[];
static uint8_t* brk_ptr;
static uint8_t* last_block;
static uint32_t heap_peak;
static void heap_reset(void) { brk_ptr = _heap_start; last_block = 0; heap_peak = 0; }
static void* bump(size_t n) {
    n = (n + 15) & ~(size_t)15;
    if (brk_ptr + 16 + n > _heap_end) return 0;
    uint8_t* blk = brk_ptr;
    *(uint32_t*)blk = (uint32_t)n;
    brk_ptr += 16 + n;
    last_block = blk;
    const uint32_t used = (uint32_t)(brk_ptr - _heap_start);
    if (used > heap_peak) heap_peak = used;
    return blk + 16;
}
void* heap_caps_malloc(size_t n, uint32_t caps) { (void)caps; return bump(n); }
void* heap_caps_realloc(void* p, size_t n, uint32_t caps) {
    (void)caps;
    if (!p) return bump(n);
    uint8_t* blk = (uint8_t*)p - 16;
    const uint32_t old = *(uint32_t*)blk;
    if (blk == last_block) {                       /* grow in place, as a real heap often can */
        const size_t nn = (n + 15) & ~(size_t)15;
        if (blk + 16 + nn > _heap_end) return 0;
        *(uint32_t*)blk = (uint32_t)nn;
        brk_ptr = blk + 16 + nn;
        const uint32_t used = (uint32_t)(brk_ptr - _heap_start);
        if (used > heap_peak) heap_peak = used;
        return p;
    }
    uint8_t* q = bump(n);
    if (!q) return 0;
    const uint8_t* s = p;
    for (uint32_t i = 0; i < old && i < n; i++) q[i] = s[i];
    return q;
}
void free(void* p) { (void)p; }

/* ── the probe ────────────────────────────────────────────────────────────────────────── */
extern uint32_t _stack_bottom[];
#define PAT_A 0xA5A5A5A5u
#define PAT_B 0x5A5A5A5Au

__attribute__((noinline)) static void paint(uint32_t pat, uintptr_t below) {
    for (volatile uint32_t* p = _stack_bottom; (uintptr_t)p < below; p++) *p = pat;
}
__attribute__((noinline)) static uintptr_t lowest(uint32_t pat) {
    const volatile uint32_t* p = _stack_bottom;
    while (*p == pat) p++;
    return (uintptr_t)p;
}

struct probe {
    int      ok, w, h;
    uint32_t depth;
    uint32_t sum;
};

/* Each returns the bytes of stack used from the SP at the call, i.e. by the callee and below. */
__attribute__((noinline)) static uint32_t probe_info(const struct image* im, uint32_t pat, struct probe* r) {
    uintptr_t sp;
    __asm__ volatile("mv %0, sp" : "=r"(sp));
    paint(pat, sp - 64);
    r->ok = cover_soft_info(im->data, im->len, &r->w, &r->h);
    return (uint32_t)(sp - lowest(pat));
}
__attribute__((noinline)) static uint32_t probe_decode(const struct image* im, uint32_t pat, struct probe* r) {
    uintptr_t sp;
    __asm__ volatile("mv %0, sp" : "=r"(sp));
    paint(pat, sp - 64);
    unsigned char* px = cover_soft_decode(im->data, im->len, &r->w, &r->h);
    const uint32_t depth = (uint32_t)(sp - lowest(pat));
    r->ok = px != 0;
    r->sum = 0;
    if (px) {
        uint32_t h = 2166136261u;                  /* FNV-1a over the RGB output */
        for (uint32_t i = 0; i < (uint32_t)r->w * (uint32_t)r->h * 3u; i++) h = (h ^ px[i]) * 16777619u;
        r->sum = h;
        cover_soft_free(px);
    }
    return depth;
}

static int ends_with(const char* s, const char* tail) {
    int a = 0, b = 0;
    while (s[a]) a++;
    while (tail[b]) b++;
    if (b > a) return 0;
    for (int i = 0; i < b; i++) if (s[a - b + i] != tail[i]) return 0;
    return 1;
}

int main_c(void) {
    uint32_t worst_png = 0, worst_jpg = 0, worst_info = 0;
    const char* worst_png_name = "";
    const char* worst_jpg_name = "";
    out_s("\nstb_image stack probe: firmware cover_soft.c.obj on RV32 (QEMU virt)\n");
    out_s("image                             bytes     w x h      info  decode   heap KB  ok\n");
    int failures = 0;
    for (int i = 0; i < IMAGE_COUNT; i++) {
        const struct image* im = &IMAGES[i];
        struct probe a = {0}, b = {0}, c = {0};
        heap_reset();
        uint32_t info = probe_info(im, PAT_A, &a);
        heap_reset();
        const uint32_t info_b = probe_info(im, PAT_B, &a);
        if (info_b > info) info = info_b;
        heap_reset();
        uint32_t dec = probe_decode(im, PAT_A, &b);
        heap_reset();
        const uint32_t dec_b = probe_decode(im, PAT_B, &c);
        if (dec_b > dec) dec = dec_b;
        const int ok = a.ok && b.ok && c.ok && b.sum == c.sum;
        if (!ok) failures++;

        out_pad(im->name, 32);
        out_u((uint32_t)im->len, 7);
        out_u((uint32_t)b.w, 7);
        out_s(" x");
        out_u((uint32_t)b.h, 5);
        out_u(info, 8);
        out_u(dec, 8);
        out_u(heap_peak / 1024, 10);
        out_s(ok ? "  yes" : "  NO ");
        if (!ok) {
            const char* why = cover_soft_reason();
            out_s(" (");
            out_s(why ? why : "no reason");
            out_s(")");
        }
        out_s("\n");
        if (info > worst_info) worst_info = info;
        if (ends_with(im->name, ".png")) {
            if (dec > worst_png) { worst_png = dec; worst_png_name = im->name; }
        } else if (dec > worst_jpg) {
            worst_jpg = dec;
            worst_jpg_name = im->name;
        }
    }
    out_s("\nworst cover_soft_info():   ");
    out_u(worst_info, 0);
    out_s(" B\nworst cover_soft_decode() PNG:  ");
    out_u(worst_png, 0);
    out_s(" B  (");
    out_s(worst_png_name);
    out_s(")\nworst cover_soft_decode() JPEG: ");
    out_u(worst_jpg, 0);
    out_s(" B  (");
    out_s(worst_jpg_name);
    out_s(")\n");
    finish(failures ? 2 : 0);
    return 0;
}
