enum Job(Increment(x:Int), Decrement(x:Int))

func main():
    do:
        >> nums := mutexed [10, 20, 30]
        holding nums:
            >> nums[]
            = [10, 20, 30]
            nums:insert(40)

        holding nums:
            >> nums[]
            = [10, 20, 30, 40]


    jobs := mutexed [Job.Increment(5)]

    results := mutexed [:Int]

    >> thread := Thread.new(func():
        !! In another thread!
        repeat:
            job := holding jobs: (jobs:pop(1) or stop)
            when job is Increment(x):
                holding results: results:insert(x + 1)
            is Decrement(x):
                holding results: results:insert(x - 1)
    )

    holding jobs:
        jobs:insert(Decrement(100))
        jobs:insert(Decrement(200))
        jobs:insert(Decrement(300))
        jobs:insert(Decrement(400))
        jobs:insert(Decrement(500))
        jobs:insert(Decrement(600))
        jobs:insert(Increment(1000))

    dequeue_result := func():
        result := none:Int
        repeat:
            result = (holding results: results:pop(1))
            stop if result
            sleep(0.00001)
        return result!

    >> dequeue_result()
    = 6
    >> dequeue_result()
    = 99

    >> dequeue_result()
    = 199
    >> dequeue_result()
    = 299
    >> dequeue_result()
    = 399
    >> dequeue_result()
    = 499
    >> dequeue_result()
    = 599

    >> dequeue_result()
    = 1001

    !! Canceling...
    >> thread:cancel()
    !! Joining...
    >> thread:join()
    !! Done!
