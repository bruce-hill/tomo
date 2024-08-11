enum Job(Increment(x:Int), Decrement(x:Int))

func main():
    jobs := |:Job|
    results := |:Int|
    >> thread := Thread.new(func():
        //! In another thread!
        while yes:
            when jobs:pop() is Increment(x):
                >> results:push(x+1)
            is Decrement(x):
                >> results:push(x-1)
    )

    >> jobs:push(Increment(5))
    >> jobs:push(Decrement(100))

    >> results:pop()
    = 6

    >> jobs:push(Increment(1000))
    >> results:pop()
    = 99

    >> results:pop()
    = 1001

    //! Canceling...
    >> thread:cancel()
    //! Joining...
    >> thread:join()
    //! Done!
