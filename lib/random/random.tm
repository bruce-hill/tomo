# Random Number Generator (RNG) implementation based on ChaCha

use ./sysrandom.h
use ./chacha.h

struct chacha_ctx(j0,j1,j2,j3,j4,j5,j6,j7,j8,j9,j10,j11,j12,j13,j14,j15:Int32; extern, secret)
    func from_seed(seed:[Byte]=[] -> chacha_ctx)
        return C_code:chacha_ctx(
            chacha_ctx ctx;
            uint8_t seed_bytes[KEYSZ + IVSZ] = {};
            for (int64_t i = 0; i < (int64_t)sizeof(seed_bytes); i++)
                seed_bytes[i] = i < @seed.length ? *(uint8_t*)(@seed.data + i*@seed.stride) : 0;
            chacha_keysetup(&ctx, seed_bytes);
            chacha_ivsetup(&ctx, seed_bytes + KEYSZ);
            ctx
        )

random := RandomNumberGenerator.new()

func _os_random_bytes(count:Int64 -> [Byte])
    return C_code:[Byte](
        uint8_t *random_bytes = GC_MALLOC_ATOMIC(@count);
        getrandom(random_bytes, @count, 0);
        (List_t){.length=@count, .data=random_bytes, .stride=1, .atomic=1}
    )
struct RandomNumberGenerator(_chacha:chacha_ctx, _random_bytes:[Byte]=[]; secret)
    func new(seed:[Byte]?=none, -> @RandomNumberGenerator)
        ctx := chacha_ctx.from_seed(seed or _os_random_bytes(40))
        return @RandomNumberGenerator(ctx, [])

    func _rekey(rng:&RandomNumberGenerator)
        rng._random_bytes = C_code:[Byte](
            Byte_t new_keystream[KEYSZ + IVSZ] = {};
            // Fill the buffer with the keystream
            chacha_encrypt_bytes(&@rng->_chacha, new_keystream, new_keystream, sizeof(new_keystream));
            // Immediately reinitialize for backtracking resistance
            chacha_keysetup(&@rng->_chacha, new_keystream);
            chacha_ivsetup(&@rng->_chacha, new_keystream + KEYSZ);
            List_t new_bytes = (List_t){.data=GC_MALLOC_ATOMIC(1024), .length=1024, .stride=1, .atomic=1};
            memset(new_bytes.data, 0, new_bytes.length);
            chacha_encrypt_bytes(&@rng->_chacha, new_bytes.data, new_bytes.data, new_bytes.length);
            new_bytes
        )

    func _fill_bytes(rng:&RandomNumberGenerator, dest:&Memory, needed:Int64)
        C_code {
            while (@needed > 0) {
                if (@rng->_random_bytes.length == 0)
                    @(rng._rekey());

                assert(@rng->_random_bytes.stride == 1);

                int64_t batch_size = MIN(@needed, @rng->_random_bytes.length);
                uint8_t *batch_src = @rng->_random_bytes.data;
                memcpy(@dest, batch_src, batch_size);
                memset(batch_src, 0, batch_size);
                @rng->_random_bytes.data += batch_size;
                @rng->_random_bytes.length -= batch_size;
                @dest += batch_size;
                @needed -= batch_size;
            }
        }

    func bytes(rng:&RandomNumberGenerator, count:Int -> [Byte])
        count64 := Int64(count)
        buf := C_code:@Memory(GC_MALLOC_ATOMIC(@count64))
        rng._fill_bytes(buf, count64)
        return C_code:[Byte]((List_t){.data=@buf, .stride=1, .atomic=1, .length=@count64})

    func byte(rng:&RandomNumberGenerator -> Byte)
        byte : &Byte
        rng._fill_bytes(byte, 1)
        return byte[]

    func bool(rng:&RandomNumberGenerator, probability=0.5 -> Bool)
        if probability == 0.5
            return rng.byte() < 0x80
        else
            return rng.num(0., 1.) < 0.5

    func int64(rng:&RandomNumberGenerator, min=Int64.min, max=Int64.max -> Int64)
        fail("Random minimum value $min is larger than the maximum value $max") if min > max
        return min if min == max
        random_int64 : &Int64
        rng._fill_bytes(random_int64, 8)
        if min == Int64.min and max == Int64.max
            return random_int64

        return C_code:Int64(
            uint64_t range = (uint64_t)@max - (uint64_t)@min + 1;
            uint64_t min_r = -range % range;
            uint64_t r;
            @random_int64 = &r;
            for (;;) {
                @(rng._fill_bytes(random_int64, 8));
                if (r >= min_r) break;
            }
            (int64_t)((uint64_t)@min + (r % range))
        )

    func int32(rng:&RandomNumberGenerator, min=Int32.min, max=Int32.max -> Int32)
        fail("Random minimum value $min is larger than the maximum value $max") if min > max
        return min if min == max
        random_int32 : &Int32
        rng._fill_bytes(random_int32, 8)
        if min == Int32.min and max == Int32.max
            return random_int32

        return C_code:Int32(
            uint32_t range = (uint32_t)@max - (uint32_t)@min + 1;
            uint32_t min_r = -range % range;
            uint32_t r;
            @random_int32 = &r;
            for (;;) {
                @(rng._fill_bytes(random_int32, 8));
                if (r >= min_r) break;
            }
            (int32_t)((uint32_t)@min + (r % range))
        )

    func int16(rng:&RandomNumberGenerator, min=Int16.min, max=Int16.max -> Int16)
        fail("Random minimum value $min is larger than the maximum value $max") if min > max
        return min if min == max
        random_int16 : &Int16
        rng._fill_bytes(random_int16, 8)
        if min == Int16.min and max == Int16.max
            return random_int16

        return C_code:Int16(
            uint16_t range = (uint16_t)@max - (uint16_t)@min + 1;
            uint16_t min_r = -range % range;
            uint16_t r;
            @random_int16 = &r;
            for (;;) {
                @(rng._fill_bytes(random_int16, 8));
                if (r >= min_r) break;
            }
            (int16_t)((uint16_t)@min + (r % range))
        )

    func int8(rng:&RandomNumberGenerator, min=Int8.min, max=Int8.max -> Int8)
        fail("Random minimum value $min is larger than the maximum value $max") if min > max
        return min if min == max
        random_int8 : &Int8
        rng._fill_bytes(random_int8, 8)
        if min == Int8.min and max == Int8.max
            return random_int8

        return C_code:Int8(
            uint8_t range = (uint8_t)@max - (uint8_t)@min + 1;
            uint8_t min_r = -range % range;
            uint8_t r;
            @random_int8 = &r;
            for (;;) {
                @(rng._fill_bytes(random_int8, 8));
                if (r >= min_r) break;
            }
            (int8_t)((uint8_t)@min + (r % range))
        )

    func num(rng:&RandomNumberGenerator, min=0., max=1. -> Num)
        num_buf : &Num
        return C_code:Num(
            if (@min > @max) fail("Random minimum value (", @min, ") is larger than the maximum value (", @max, ")");
            if (@min == @max) return @min;

            union {
                Num_t num;
                uint64_t bits;
            } r = {.bits=0}, one = {.num=1.0};
            @num_buf = &r.num;
            @(rng._fill_bytes(num_buf, 8));

            // Set r.num to 1.<random-bits>
            r.bits &= ~(0xFFFULL << 52);
            r.bits |= (one.bits & (0xFFFULL << 52));

            r.num -= 1.0;

            (@min == 0.0 && @max == 1.0) ? r.num : ((1.0-r.num)*@min + r.num*@max)
        )

    func num32(rng:&RandomNumberGenerator, min=Num32(0.), max=Num32(1.) -> Num32)
        return Num32(rng.num(Num(min), Num(max)))

    func int(rng:&RandomNumberGenerator, min:Int, max:Int -> Int)
        return C_code:Int(
            if (likely(((@min.small & @max.small) & 1) != 0)) {
                int32_t r = @(rng.int32(Int32(min), Int32(max)));
                return I_small(r);
            }

            int32_t cmp = @(min <> max);
            if (cmp > 0)
                fail("Random minimum value (", @min, ") is larger than the maximum value (", @max, ")");
            if (cmp == 0) return @min;

            mpz_t range_size;
            mpz_init_set_int(range_size, @max);
            if (@min.small & 1) {
                mpz_t min_mpz;
                mpz_init_set_si(min_mpz, @min.small >> 2);
                mpz_sub(range_size, range_size, min_mpz);
            } else {
                mpz_sub(range_size, range_size, *@min.big);
            }

            gmp_randstate_t gmp_rng;
            gmp_randinit_default(gmp_rng);
            int64_t seed = @(rng.int64());
            gmp_randseed_ui(gmp_rng, (unsigned long)seed);

            mpz_t r;
            mpz_init(r);
            mpz_urandomm(r, gmp_rng, range_size);

            gmp_randclear(gmp_rng);
            Int$plus(@min, Int$from_mpz(r))
        )


func main()
    >> rng := RandomNumberGenerator.new()
    >> rng.num()
    >> rng.num()
    >> rng.num()
    >> rng.num(0, 100)
    >> rng.byte()
    >> rng.bytes(20)
    # >> rng.int(1, 100)
    # >> rng.int(1, 100)
    # >> rng.int(1, 100)
    # >> rng.int(1, 100)
