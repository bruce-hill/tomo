// Random Number Generator (RNG) implementation based on ChaCha

#include <ctype.h>
#include <err.h>
#include <gc.h>
#include <gmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "arrays.h"
#include "datatypes.h"
#include "rng.h"
#include "text.h"
#include "util.h"

#include "chacha.h"

struct RNGState_t {
    chacha_ctx chacha;
    size_t unused_bytes;
    uint8_t random_bytes[1024];
};

public _Thread_local RNG_t default_rng = (struct RNGState_t[1]){};

PUREFUNC static Text_t RNG$as_text(const void *rng, bool colorize, const TypeInfo_t*)
{
    if (!rng) return Text("RNG");
    return Text$format(colorize ? "\x1b[34;1mRNG(%p)\x1b[m" : "RNG(%p)", *(RNG_t**)rng);
}

#define KEYSZ 32
#define IVSZ 8

public void RNG$set_seed(RNG_t rng, Array_t seed)
{
    uint8_t seed_bytes[KEYSZ + IVSZ] = {};
    for (int64_t i = 0; i < (int64_t)sizeof(seed_bytes); i++)
        seed_bytes[i] = i < seed.length ? *(uint8_t*)(seed.data + i*seed.stride) : 0;

    rng->unused_bytes = 0;
    chacha_keysetup(&rng->chacha, seed_bytes, KEYSZ/8);
    chacha_ivsetup(&rng->chacha, seed_bytes + KEYSZ);
}

public RNG_t RNG$copy(RNG_t rng)
{
    RNG_t copy = GC_MALLOC_ATOMIC(sizeof(struct RNGState_t));
    *copy = *rng;
    return copy;
}

public RNG_t RNG$new(Array_t seed)
{
    RNG_t rng = GC_MALLOC_ATOMIC(sizeof(struct RNGState_t));
    RNG$set_seed(rng, seed);
    return rng;
}

static void rekey(RNG_t rng)
{
    // Fill the buffer with the keystream
    chacha_encrypt_bytes(&rng->chacha, rng->random_bytes, rng->random_bytes, sizeof(rng->random_bytes));
    // Immediately reinitialize for backtracking resistance
    chacha_keysetup(&rng->chacha, rng->random_bytes, KEYSZ/8);
    chacha_ivsetup(&rng->chacha, rng->random_bytes + KEYSZ);
    explicit_bzero(rng->random_bytes, KEYSZ + IVSZ);
    rng->unused_bytes = sizeof(rng->random_bytes) - KEYSZ - IVSZ;
    assert(rng->unused_bytes <= sizeof(rng->random_bytes));
}

static void random_bytes(RNG_t rng, uint8_t *dest, size_t needed)
{
    while (needed > 0) {
        assert(rng->unused_bytes <= sizeof(rng->random_bytes));
        if (rng->unused_bytes == 0)
            rekey(rng);

        size_t batch_size = MIN(needed, rng->unused_bytes);
        uint8_t *batch_src = rng->random_bytes + sizeof(rng->random_bytes) - rng->unused_bytes;
        memcpy(dest, batch_src, batch_size);
        memset(batch_src, 0, batch_size);
        rng->unused_bytes -= batch_size;
        dest += batch_size;
        needed -= batch_size;
        assert(rng->unused_bytes <= sizeof(rng->random_bytes));
    }
}

public Bool_t RNG$bool(RNG_t rng, Num_t p)
{
    if (p == 0.5) {
        uint8_t b;
        random_bytes(rng, &b, sizeof(b));
        return b & 1;
    } else {
        return RNG$num(rng, 0.0, 1.0) < p;
    }
}

public Int_t RNG$int(RNG_t rng, Int_t min, Int_t max)
{
    if (likely(((min.small & max.small) & 1) != 0)) {
        int32_t r = RNG$int32(rng, (int32_t)(min.small >> 2), (int32_t)(max.small >> 2));
        return I_small(r);
    }

    int32_t cmp = Int$compare_value(min, max);
    if (cmp > 0) {
        Text_t min_text = Int$as_text(&min, false, &Int$info), max_text = Int$as_text(&max, false, &Int$info);
        fail("Random minimum value (%k) is larger than the maximum value (%k)",
             &min_text, &max_text);
    }
    if (cmp == 0) return min;

    mpz_t range_size;
    mpz_init_set_int(range_size, max);
    if (min.small & 1) {
        mpz_t min_mpz;
        mpz_init_set_si(min_mpz, min.small >> 2);
        mpz_sub(range_size, range_size, min_mpz);
    } else {
        mpz_sub(range_size, range_size, *min.big);
    }

    gmp_randstate_t gmp_rng;
    gmp_randinit_default(gmp_rng);
    gmp_randseed_ui(gmp_rng, (unsigned long)RNG$int64(rng, INT64_MIN, INT64_MAX));

    mpz_t r;
    mpz_init(r);
    mpz_urandomm(r, gmp_rng, range_size);

    gmp_randclear(gmp_rng);
    return Int$plus(min, Int$from_mpz(r));
}

public Int64_t RNG$int64(RNG_t rng, Int64_t min, Int64_t max)
{
    if (min > max) fail("Random minimum value (%ld) is larger than the maximum value (%ld)", min, max);
    if (min == max) return min;
    if (min == INT64_MIN && max == INT64_MAX) {
        int64_t r;
        random_bytes(rng, (uint8_t*)&r, sizeof(r));
        return r;
    }
    uint64_t range = (uint64_t)max - (uint64_t)min + 1;
    uint64_t min_r = -range % range;
    uint64_t r;
    for (;;) {
        random_bytes(rng, (uint8_t*)&r, sizeof(r));
        if (r >= min_r) break;
    }
    return (int64_t)((uint64_t)min + (r % range));
}

public Int32_t RNG$int32(RNG_t rng, Int32_t min, Int32_t max)
{
    if (min > max) fail("Random minimum value (%d) is larger than the maximum value (%d)", min, max);
    if (min == max) return min;
    if (min == INT32_MIN && max == INT32_MAX) {
        int32_t r;
        random_bytes(rng, (uint8_t*)&r, sizeof(r));
        return r;
    }
    uint32_t range = (uint32_t)max - (uint32_t)min + 1;
    uint32_t min_r = -range % range;
    uint32_t r;
    for (;;) {
        random_bytes(rng, (uint8_t*)&r, sizeof(r));
        if (r >= min_r) break;
    }
    return (int32_t)((uint32_t)min + (r % range));
}

public Int16_t RNG$int16(RNG_t rng, Int16_t min, Int16_t max)
{
    if (min > max) fail("Random minimum value (%d) is larger than the maximum value (%d)", min, max);
    if (min == max) return min;
    if (min == INT16_MIN && max == INT16_MAX) {
        int16_t r;
        random_bytes(rng, (uint8_t*)&r, sizeof(r));
        return r;
    }
    uint16_t range = (uint16_t)max - (uint16_t)min + 1;
    uint16_t min_r = -range % range;
    uint16_t r;
    for (;;) {
        random_bytes(rng, (uint8_t*)&r, sizeof(r));
        if (r >= min_r) break;
    }
    return (int16_t)((uint16_t)min + (r % range));
}

public Int8_t RNG$int8(RNG_t rng, Int8_t min, Int8_t max)
{
    if (min > max) fail("Random minimum value (%d) is larger than the maximum value (%d)", min, max);
    if (min == max) return min;
    if (min == INT8_MIN && max == INT8_MAX) {
        int8_t r;
        random_bytes(rng, (uint8_t*)&r, sizeof(r));
        return r;
    }
    uint8_t range = (uint8_t)max - (uint8_t)min + 1;
    uint8_t min_r = -range % range;
    uint8_t r;
    for (;;) {
        random_bytes(rng, (uint8_t*)&r, sizeof(r));
        if (r >= min_r) break;
    }
    return (int8_t)((uint8_t)min + (r % range));
}

public Num_t RNG$num(RNG_t rng, Num_t min, Num_t max)
{
    if (min > max) fail("Random minimum value (%g) is larger than the maximum value (%g)", min, max);
    if (min == max) return min;

    union {
        Num_t num;
        uint64_t bits;
    } r = {.bits=0}, one = {.num=1.0};
    random_bytes(rng, (void*)&r, sizeof(r));

    // Set r.num to 1.<random-bits>
    r.bits &= ~(0xFFFULL << 52);
    r.bits |= (one.bits & (0xFFFULL << 52));

    r.num -= 1.0;

    if (min == 0.0 && max == 1.0)
        return r.num;

    return (1.0-r.num)*min + r.num*max;
}

public Num32_t RNG$num32(RNG_t rng, Num32_t min, Num32_t max)
{
    return (Num32_t)RNG$num(rng, (Num_t)min, (Num_t)max);
}

public Byte_t RNG$byte(RNG_t rng)
{
    Byte_t b;
    random_bytes(rng, &b, sizeof(b));
    return b;
}

public Array_t RNG$bytes(RNG_t rng, Int_t count)
{
    int64_t n = Int_to_Int64(count, false);
    Byte_t *r = GC_MALLOC_ATOMIC(sizeof(Byte_t[n]));
    random_bytes(rng, r, sizeof(Byte_t[n]));
    return (Array_t){.data=r, .length=n, .stride=1, .atomic=1};
}

public const TypeInfo_t RNG$info = {
    .size=sizeof(void*),
    .align=__alignof__(void*),
    .metamethods={
        .as_text=RNG$as_text,
    },
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
