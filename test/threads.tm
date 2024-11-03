enum Job(Increment(x:Int), Decrement(x:Int))

func main():

    do:
        >> channel := |:Int|
        >> channel:give(10)
        >> channel:give(20)
        >> channel:give(30)
        >> channel:view()
        = [10, 20, 30]
        >> channel:peek()
        = 10
        >> channel:peek(front=no)
        = 30

        >> channel:give(-10, front=yes)
        >> channel:view()
        = [-10, 10, 20, 30]


    jobs := |:Job; max_size=2|
    >> jobs:give(Increment(5))
    >> jobs:peek()
    = Job.Increment(5)

    results := |:Int; max_size|
    >> thread := Thread.new(func():
        !! In another thread!
        repeat:
            >> got := jobs:get()
            when got is Increment(x):
                >> results:give(x+1)
            is Decrement(x):
                >> results:give(x-1)
    )

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

    !! Canceling...
    >> thread:cancel()
    !! Joining...
    >> thread:join()
    !! Done!
