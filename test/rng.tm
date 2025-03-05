# Random Number Generator tests

func main():
    !! Default RNG:
    >> random:int64()

    >> original_rng := RNG.new([:Byte])
    >> copy := original_rng:copy()

    for rng in [original_rng, copy]:
        !! RNG: $rng
        >> rng:int(1, 1000)
        = 921
        >> rng:int64(1, 1000)
        = 324 : Int64
        >> rng:int32(1, 1000)
        = 586 : Int32
        >> rng:int16(1, 1000)
        = 453 : Int16
        >> rng:int8(1, 100)
        = 53 : Int8
        >> rng:byte()
        = 0xDC : Byte
        >> rng:bytes(10)
        = [0xA0, 0x5A, 0x10, 0x3F, 0x6C, 0xD1, 0x35, 0xC2, 0x87, 0x8C]
        >> rng:bool(p=0.8)
        = yes
        >> rng:num()
        = 0.03492503353647658 : Num
        >> rng:num32(1, 1000)
        = 761.05908 : Num32

        !! Random array methods:
        >> nums := [10*i for i in 10]
        >> nums:shuffled(rng=rng)
        = [30, 50, 100, 20, 90, 10, 80, 40, 70, 60]
        >> nums:random(rng=rng)
        = 70
        >> nums:sample(10, weights=[1.0/Num(i) for i in nums.length], rng=rng)
        = [10, 20, 10, 10, 30, 70, 10, 40, 60, 80]
