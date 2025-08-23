#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "siphash.h"
#include "util.h"

public
uint64_t TOMO_HASH_KEY[2] = {23, 42}; // Randomized in tomo_init()

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

#include "siphash-internals.h"

PUREFUNC public uint64_t siphash24(const uint8_t *src, size_t src_sz) {
    siphash sh;
    if ((uint64_t)src % __alignof__(uint64_t) == 0) {
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-align"
#endif
        const uint64_t *in = (uint64_t *)src;
        /* Find largest src_sz evenly divisible by 8 bytes. */
        const ptrdiff_t src_sz_nearest_8bits = ((ptrdiff_t)src_sz >> 3) << 3;
        const uint64_t *goal = (uint64_t *)(src + src_sz_nearest_8bits);
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
        siphashinit(&sh, src_sz);
        src_sz -= (size_t)src_sz_nearest_8bits;
        while (in < goal) {
            siphashadd64bits(&sh, *in);
            in++;
        }
        return siphashfinish(&sh, (uint8_t *)in, src_sz);
    } else {
        const uint8_t *in = src;
        siphashinit(&sh, src_sz);
        while (src_sz >= 8) {
            uint64_t in_64;
            memcpy(&in_64, in, sizeof(uint64_t));
            siphashadd64bits(&sh, in_64);
            in += 8;
            src_sz -= 8;
        }
        return siphashfinish(&sh, (uint8_t *)in, src_sz);
    }
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
