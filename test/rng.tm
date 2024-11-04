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
        = 324[64]
        >> rng:int32(1, 1000)
        = 586[32]
        >> rng:int16(1, 1000)
        = 453[16]
        >> rng:int8(1, 100)
        = 53[8]
        >> rng:byte()
        = 220[B]
        >> rng:bytes(10)
        = [160[B], 90[B], 16[B], 63[B], 108[B], 209[B], 53[B], 194[B], 135[B], 140[B]]
        >> rng:bool(p=0.8)
        = yes
        >> rng:num()
        = 0.03492503353647658
        >> rng:num32(1, 1000)
        = 761.05908_f32

        !! Random array methods:
        >> nums := [10*i for i in 10]
        >> nums:shuffled(rng=rng)
        = [30, 50, 100, 20, 90, 10, 80, 40, 70, 60]
        >> nums:random(rng=rng)
        = 70
        >> nums:sample(10, weights=[1.0/i for i in nums.length], rng=rng)
        = [10, 20, 10, 10, 30, 70, 10, 40, 60, 80]
