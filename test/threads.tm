enum Job(Increment(x:Int), Decrement(x:Int))

func main():
    jobs := |:Job; max_size=2|
    results := |:Int; max_size|
    >> thread := Thread.new(func():
        //! In another thread!
        while yes:
            >> got := jobs:get()
            when got is Increment(x):
                >> results:give(x+1)
            is Decrement(x):
                >> results:give(x-1)
    )

    >> jobs:give(Increment(5))
    >> jobs:give(Decrement(100))
    >> jobs:give(Decrement(100))
    >> jobs:give(Decrement(100))
    >> jobs:give(Decrement(100))
    >> jobs:give(Decrement(100))
    >> jobs:give(Decrement(100))

    >> results:get()
    = 6

    >> jobs:give(Increment(1000))
    >> results:get()
    = 99

    >> results:get()
    = 99
    >> results:get()
    = 99
    >> results:get()
    = 99
    >> results:get()
    = 99
    >> results:get()
    = 99

    >> results:get()
    = 1001

    //! Canceling...
    >> thread:cancel()
    //! Joining...
    >> thread:join()
    //! Done!
