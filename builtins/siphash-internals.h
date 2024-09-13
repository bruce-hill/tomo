#pragma once

// This file holds the internals for the SipHash implementation. For a few
// cases, we want to include this for incrementally computing hashes.
// Otherwise, it suffices to just use the siphash24() function from siphash.h

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "siphash.h"

/* <MIT License>
 Copyright (c) 2013  Marek Majkowski <marek@popcount.org>
 Copyright (c) 2018  Samantha McVey <samantham@posteo.net>
 Copyright (c) 2024  Bruce Hill <bruce@bruce-hill.com>

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 </MIT License>

 Original location:
        https://github.com/majek/csiphash/

 Original solution inspired by code from:
        Samuel Neves (supercop/crypto_auth/siphash24/little)
        djb (supercop/crypto_auth/siphash24/little2)
        Jean-Philippe Aumasson (https://131002.net/siphash/siphash24.c)

 Extensive modifications for MoarVM by Samantha McVey

 Further modifications for Tomo by Bruce Hill
*/
struct siphash {
    uint64_t v0;
    uint64_t v1;
    uint64_t v2;
    uint64_t v3;
    uint64_t  b;
};
typedef struct siphash siphash;
#define ROTATE(x, b) (uint64_t)( ((x) << (b)) | ( (x) >> (64 - (b))) )

#define HALF_ROUND(a,b,c,d,s,t) \
    a += b; c += d;             \
    b = ROTATE(b, s) ^ a;       \
    d = ROTATE(d, t) ^ c;       \
    a = ROTATE(a, 32);

#define DOUBLE_ROUND(v0,v1,v2,v3)  \
    HALF_ROUND(v0,v1,v2,v3,13,16); \
    HALF_ROUND(v2,v1,v0,v3,17,21); \
    HALF_ROUND(v0,v1,v2,v3,13,16); \
    HALF_ROUND(v2,v1,v0,v3,17,21);

static inline void siphashinit (siphash *sh, size_t src_sz) {
    const uint64_t k0 = TOMO_HASH_KEY[0];
    const uint64_t k1 = TOMO_HASH_KEY[1];
    sh->b = (uint64_t)src_sz << 56;
    sh->v0 = k0 ^ 0x736f6d6570736575ULL;
    sh->v1 = k1 ^ 0x646f72616e646f6dULL;
    sh->v2 = k0 ^ 0x6c7967656e657261ULL;
    sh->v3 = k1 ^ 0x7465646279746573ULL;
}
static inline void siphashadd64bits (siphash *sh, const uint64_t in) {
    const uint64_t mi = in;
    sh->v3 ^= mi;
    DOUBLE_ROUND(sh->v0,sh->v1,sh->v2,sh->v3);
    sh->v0 ^= mi;
}
#pragma GCC diagnostic ignored "-Winline"
static inline uint64_t siphashfinish_last_part (siphash *sh, uint64_t t) {
    sh->b |= t;
    sh->v3 ^= sh->b;
    DOUBLE_ROUND(sh->v0,sh->v1,sh->v2,sh->v3);
    sh->v0 ^= sh->b;
    sh->v2 ^= 0xff;
    DOUBLE_ROUND(sh->v0,sh->v1,sh->v2,sh->v3);
    DOUBLE_ROUND(sh->v0,sh->v1,sh->v2,sh->v3);
    return (sh->v0 ^ sh->v1) ^ (sh->v2 ^ sh->v3);
}
/* This union helps us avoid doing weird things with pointers that can cause old
 * compilers like GCC 4 to generate bad code. In addition it is nicely more C
 * standards compliant to keep type punning to a minimum. */
union SipHash64_union {
    uint64_t u64;
    uint32_t u32;
    uint8_t  u8[8];
};
static inline uint64_t siphashfinish (siphash *sh, const uint8_t *src, size_t src_sz) {
    union SipHash64_union t = { 0 };
    switch (src_sz) {
        /* Falls through */
        case 7: t.u8[6] = src[6];
        /* Falls through */
        case 6: t.u8[5] = src[5];
        /* Falls through */
        case 5: t.u8[4] = src[4];
        /* Falls through */
        case 4: t.u8[3] = src[3];
        /* Falls through */
        case 3: t.u8[2] = src[2];
        /* Falls through */
        case 2: t.u8[1] = src[1];
        /* Falls through */
        case 1: t.u8[0] = src[0];
        default: break;
    }
    return siphashfinish_last_part(sh, t.u64);
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
