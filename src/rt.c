/* Minimal freestanding runtime: memory helpers and fast division.
 * The GBA's ARM7 has no divide instruction, so the division helpers run as ARM code
 * from IWRAM (no wait states) and only loop over the bits the quotient needs. */
#include "types.h"
typedef unsigned long size_t_;
typedef unsigned long long u64;
void *memcpy(void *d, const void *s, size_t_ n) { u8 *a = d; const u8 *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int c, size_t_ n) { u8 *a = d; while (n--) *a++ = (u8)c; return d; }
void *memmove(void *d, const void *s, size_t_ n) { u8 *a = d; const u8 *b = s; if (a < b) while (n--) *a++ = *b++; else { a += n; b += n; while (n--) *--a = *--b; } return d; }
void __aeabi_memcpy(void *d, const void *s, size_t_ n) { memcpy(d, s, n); }
void __aeabi_memcpy4(void *d, const void *s, size_t_ n) { memcpy(d, s, n); }
void __aeabi_memcpy8(void *d, const void *s, size_t_ n) { memcpy(d, s, n); }
void __aeabi_memset(void *d, size_t_ n, int c) { memset(d, c, n); }
void __aeabi_memset4(void *d, size_t_ n, int c) { memset(d, c, n); }
void __aeabi_memclr(void *d, size_t_ n) { memset(d, 0, n); }
void __aeabi_memclr4(void *d, size_t_ n) { u32 *a = d; n >>= 2; while (n--) *a++ = 0; }
void __aeabi_memclr8(void *d, size_t_ n) { __aeabi_memclr4(d, n); }

#ifdef HOST
#define FAST
#else
#define FAST __attribute__((target("arm"), section(".iwram"), noinline))
#endif

/* returns quotient in the low word, remainder in the high word (AEABI layout) */
FAST u64 rt_udivmod(u32 n, u32 d) {
    if (d == 0) return 0;
    if (n < d) return (u64)n << 32;
    u32 q = 0, bit = 1;
    while (!(d & 0x80000000u) && (d << 1) <= n) { d <<= 1; bit <<= 1; }
    while (bit) {
        if (n >= d) { n -= d; q |= bit; }
        d >>= 1; bit >>= 1;
    }
    return ((u64)n << 32) | q;
}
#ifndef HOST
FAST u64 __aeabi_uidivmod(u32 n, u32 d) { return rt_udivmod(n, d); }
FAST u32 __aeabi_uidiv(u32 n, u32 d) { return (u32)rt_udivmod(n, d); }
FAST long long __aeabi_idivmod(s32 n, s32 d) {
    u32 un = n < 0 ? -(u32)n : (u32)n, ud = d < 0 ? -(u32)d : (u32)d;
    u64 r = rt_udivmod(un, ud);
    u32 q = (u32)r, m = (u32)(r >> 32);
    if ((n < 0) != (d < 0)) q = -q;
    if (n < 0) m = -m;
    return (long long)(((u64)m << 32) | q);
}
FAST s32 __aeabi_idiv(s32 n, s32 d) { return (s32)__aeabi_idivmod(n, d); }
#endif
