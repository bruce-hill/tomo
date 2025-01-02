# Mutexed Data

To serve the general case of synchronizing access to shared datastructures,
Tomo uses the `mutexed` keyword, which returns a function which can be used
to ensure that all access to a datastructure is guarded by a mutex:

```tomo
with_nums := mutexed [10, 20, 30]

thread := Thread.new(func():
    with_nums(func(nums:&[Int]):
        nums:insert(30)
    )
)

with_nums(func(nums:&[Int]):
    nums:insert(40)
)
    
thread:join()
```

Without having a mutex guard, the code above could run into concurrency issues
leading to data corruption.
