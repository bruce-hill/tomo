# Mutexed Data

To serve the general case of synchronizing access to shared datastructures,
Tomo uses the `mutexed` keyword, which allocates a mutex and some heap memory
for a value. Access to the heap-allocated value's memory can only be obtained
by using a `holding` block. `holding` blocks ensure that the underlying mutex
is locked before entering the block and unlocked before leaving it (even if a
short-circuiting control flow statement like `return` or `stop` is used). Here
is a simple example:

```tomo
nums := mutexed [10, 20, 30]

>> nums
= mutexed [Int]<0x12345678> : mutexed([Int])

holding nums:
    # Inside this block, the type of `nums` is `&[Int]`
    >> nums
    = &[10, 20, 30] : &[Int]

thread := Thread.new(func():
    holding nums:
        nums:insert(30)
)

holding nums:
    nums:insert(40)
    
thread:join()
```

Without using a mutex, the code above could run into concurrency issues leading
to data corruption.
