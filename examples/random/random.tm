# Random Number Generator (RNG) implementation based on ChaCha

use ./sysrandom.h
use ./chacha.h

struct chacha_ctx(j0,j1,j2,j3,j4,j5,j6,j7,j8,j9,j10,j11,j12,j13,j14,j15:Int32; extern, secret):
    func from_seed(seed:[Byte]=[] -> chacha_ctx):
        return inline C : chacha_ctx {
            chacha_ctx ctx;
            uint8_t seed_bytes[KEYSZ + IVSZ] = {};
            for (int64_t i = 0; i < (int64_t)sizeof(seed_bytes); i++)
                seed_bytes[i] = i < _$seed.length ? *(uint8_t*)(_$seed.data + i*_$seed.stride) : 0;
            chacha_keysetup(&ctx, seed_bytes);
            chacha_ivsetup(&ctx, seed_bytes + KEYSZ);
            ctx;
        }

random := RandomNumberGenerator.new()

func _os_random_bytes(count:Int64 -> [Byte]):
    return inline C : [Byte] {
        uint8_t *random_bytes = GC_MALLOC_ATOMIC(_$count);
        getrandom(random_bytes, _$count, 0);
        (Array_t){.length=_$count, .data=random_bytes, .stride=1, .atomic=1};
    }

struct RandomNumberGenerator(_chacha:chacha_ctx, _random_bytes:[Byte]=[]; secret):
    func new(seed=none:[Byte], -> @RandomNumberGenerator):
        ctx := chacha_ctx.from_seed(seed or _os_random_bytes(40))
        return @RandomNumberGenerator(ctx, [])

    func _rekey(rng:&RandomNumberGenerator):
        rng._random_bytes = inline C : [Byte] {
            Byte_t new_keystream[KEYSZ + IVSZ] = {};
            // Fill the buffer with the keystream
            chacha_encrypt_bytes(&_$rng->_chacha, new_keystream, new_keystream, sizeof(new_keystream));
            // Immediately reinitialize for backtracking resistance
            chacha_keysetup(&_$rng->_chacha, new_keystream);
            chacha_ivsetup(&_$rng->_chacha, new_keystream + KEYSZ);
            Array_t new_bytes = (Array_t){.data=GC_MALLOC_ATOMIC(1024), .length=1024, .stride=1, .atomic=1};
            memset(new_bytes.data, 0, new_bytes.length);
            chacha_encrypt_bytes(&_$rng->_chacha, new_bytes.data, new_bytes.data, new_bytes.length);
            new_bytes;
        }

    func _fill_bytes(rng:&RandomNumberGenerator, dest:&Memory, needed:Int64):
        inline C {
            while (_$needed > 0) {
                if (_$rng->_random_bytes.length == 0)
                    _$random$RandomNumberGenerator$_rekey(_$rng);

                assert(_$rng->_random_bytes.stride == 1);

                int64_t batch_size = MIN(_$needed, _$rng->_random_bytes.length);
                uint8_t *batch_src = _$rng->_random_bytes.data;
                memcpy(_$dest, batch_src, batch_size);
                memset(batch_src, 0, batch_size);
                _$rng->_random_bytes.data += batch_size;
                _$rng->_random_bytes.length -= batch_size;
                _$dest += batch_size;
                _$needed -= batch_size;
            }
        }

    func bytes(rng:&RandomNumberGenerator, count:Int -> [Byte]):
        return inline C : [Byte] {
            int64_t count64 = Int64$from_int(_$count, false);
            Array_t ret = {.data=GC_MALLOC_ATOMIC(count64), .stride=1, .atomic=1, .length=count64};
            _$random$RandomNumberGenerator$_fill_bytes(_$rng, ret.data, count64);
            ret;
        }

    func byte(rng:&RandomNumberGenerator -> Byte):
        return inline C : Byte {
            Byte_t b;
            _$random$RandomNumberGenerator$_fill_bytes(_$rng, &b, sizeof(b));
            b;
        }

    func bool(rng:&RandomNumberGenerator, probability=0.5 -> Bool):
        if probability == 0.5:
            return rng:byte() < 0x80
        else:
            return rng:num(0., 1.) < 0.5

    func int64(rng:&RandomNumberGenerator, min=Int64.min, max=Int64.max -> Int64):
        fail("Random minimum value $min is larger than the maximum value $max") if min > max
        return min if min == max
        if min == Int64.min and max == Int64.max:
            return inline C : Int64 {
                int64_t i;
                _$random$RandomNumberGenerator$_fill_bytes(_$rng, &i, sizeof(i));
                i;
            }

        return inline C : Int64 {
            uint64_t range = (uint64_t)_$max - (uint64_t)_$min + 1;
            uint64_t min_r = -range % range;
            uint64_t r;
            for (;;) {
                _$random$RandomNumberGenerator$_fill_bytes(_$rng, (uint8_t*)&r, sizeof(r));
                if (r >= min_r) break;
            }
            (int64_t)((uint64_t)_$min + (r % range));
        }

    func int32(rng:&RandomNumberGenerator, min=Int32.min, max=Int32.max -> Int32):
        fail("Random minimum value $min is larger than the maximum value $max") if min > max
        return min if min == max
        if min == Int32.min and max == Int32.max:
            return inline C : Int32 {
                int32_t i;
                _$random$RandomNumberGenerator$_fill_bytes(_$rng, &i, sizeof(i));
                i;
            }

        return inline C : Int32 {
            uint32_t range = (uint32_t)_$max - (uint32_t)_$min + 1;
            uint32_t min_r = -range % range;
            uint32_t r;
            for (;;) {
                _$random$RandomNumberGenerator$_fill_bytes(_$rng, (uint8_t*)&r, sizeof(r));
                if (r >= min_r) break;
            }
            (int32_t)((uint32_t)_$min + (r % range));
        }

    func int16(rng:&RandomNumberGenerator, min=Int16.min, max=Int16.max -> Int16):
        fail("Random minimum value $min is larger than the maximum value $max") if min > max
        return min if min == max
        if min == Int16.min and max == Int16.max:
            return inline C : Int16 {
                int16_t i;
                _$random$RandomNumberGenerator$_fill_bytes(_$rng, &i, sizeof(i));
                i;
            }

        return inline C : Int16 {
            uint16_t range = (uint16_t)_$max - (uint16_t)_$min + 1;
            uint16_t min_r = -range % range;
            uint16_t r;
            for (;;) {
                _$random$RandomNumberGenerator$_fill_bytes(_$rng, (uint8_t*)&r, sizeof(r));
                if (r >= min_r) break;
            }
            (int16_t)((uint16_t)_$min + (r % range));
        }

    func int8(rng:&RandomNumberGenerator, min=Int8.min, max=Int8.max -> Int8):
        fail("Random minimum value $min is larger than the maximum value $max") if min > max
        return min if min == max
        if min == Int8.min and max == Int8.max:
            return inline C : Int8 {
                int8_t i;
                _$random$RandomNumberGenerator$_fill_bytes(_$rng, &i, sizeof(i));
                i;
            }

        return inline C : Int8 {
            uint8_t range = (uint8_t)_$max - (uint8_t)_$min + 1;
            uint8_t min_r = -range % range;
            uint8_t r;
            for (;;) {
                _$random$RandomNumberGenerator$_fill_bytes(_$rng, (uint8_t*)&r, sizeof(r));
                if (r >= min_r) break;
            }
            (int8_t)((uint8_t)_$min + (r % range));
        }

    func num(rng:&RandomNumberGenerator, min=0., max=1. -> Num):
        return inline C : Num {
            if (_$min > _$max) fail("Random minimum value (", _$min, ") is larger than the maximum value (", _$max, ")");
            if (_$min == _$max) return _$min;

            union {
                Num_t num;
                uint64_t bits;
            } r = {.bits=0}, one = {.num=1.0};
            _$random$RandomNumberGenerator$_fill_bytes(_$rng, (uint8_t*)&r, sizeof(r));

            // Set r.num to 1.<random-bits>
            r.bits &= ~(0xFFFULL << 52);
            r.bits |= (one.bits & (0xFFFULL << 52));

            r.num -= 1.0;

            (_$min == 0.0 && _$max == 1.0) ? r.num : ((1.0-r.num)*_$min + r.num*_$max);
        }

    func num32(rng:&RandomNumberGenerator, min=Num32(0.), max=Num32(1.) -> Num32):
        return Num32(rng:num(Num(min), Num(max)))

    func int(rng:&RandomNumberGenerator, min:Int, max:Int -> Int):
        return inline C : Int {
            if (likely(((_$min.small & _$max.small) & 1) != 0)) {
                int32_t r = _$random$RandomNumberGenerator$int32(_$rng, (int32_t)(_$min.small >> 2), (int32_t)(_$max.small >> 2));
                return I_small(r);
            }

            int32_t cmp = Int$compare_value(_$min, _$max);
            if (cmp > 0)
                fail("Random minimum value (", _$min, ") is larger than the maximum value (", _$max, ")");
            if (cmp == 0) return _$min;

            mpz_t range_size;
            mpz_init_set_int(range_size, _$max);
            if (_$min.small & 1) {
                mpz_t min_mpz;
                mpz_init_set_si(min_mpz, _$min.small >> 2);
                mpz_sub(range_size, range_size, min_mpz);
            } else {
                mpz_sub(range_size, range_size, *_$min.big);
            }

            gmp_randstate_t gmp_rng;
            gmp_randinit_default(gmp_rng);
            int64_t seed = _$random$RandomNumberGenerator$int64(_$rng, INT64_MIN, INT64_MAX);
            gmp_randseed_ui(gmp_rng, (unsigned long)seed);

            mpz_t r;
            mpz_init(r);
            mpz_urandomm(r, gmp_rng, range_size);

            gmp_randclear(gmp_rng);
            Int$plus(_$min, Int$from_mpz(r));
        }


func main():
    >> rng := RandomNumberGenerator.new()
    >> rng:num()
    >> rng:num()
    >> rng:num()
    >> rng:num(0, 100)
    >> rng:byte()
    >> rng:bytes(20)
    # >> rng:int(1, 100)
    # >> rng:int(1, 100)
    # >> rng:int(1, 100)
    # >> rng:int(1, 100)
