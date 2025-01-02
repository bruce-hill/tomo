enum Job(Increment(x:Int), Decrement(x:Int))

func main():
    do:
        >> with_nums := mutexed [10, 20, 30]
        with_nums(func(nums:&[Int]):
            >> nums[]
            = [10, 20, 30]
            nums:insert(40)
        )
        with_nums(func(nums:&[Int]):
            >> nums[]
            = [10, 20, 30, 40]
        )


    with_jobs := mutexed [Job.Increment(5)]
    enqueue_job := func(job:Job):
        with_jobs(func(jobs:&[Job]): jobs:insert(job))
    dequeue_job := func():
        job := @none:Job
        with_jobs(func(jobs:&[Job]): job[] = jobs:pop(1))
        job[]

    with_results := mutexed [:Int]
    enqueue_result := func(result:Int):
        with_results(func(results:&[Int]): results:insert(result))
    dequeue_result := func():
        result := @none:Int
        repeat:
            with_results(func(results:&[Int]): result[] = results:pop(1))
            stop if result[]
            sleep(0.00001)
        result[]!

    >> thread := Thread.new(func():
        !! In another thread!
        repeat:
            job := dequeue_job() or stop
            when job is Increment(x):
                enqueue_result(x + 1)
            is Decrement(x):
                enqueue_result(x - 1)
    )

    enqueue_job(Decrement(100))
    enqueue_job(Decrement(200))
    enqueue_job(Decrement(300))
    enqueue_job(Decrement(400))
    enqueue_job(Decrement(500))
    enqueue_job(Decrement(600))

    >> enqueue_job(Increment(1000))

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
