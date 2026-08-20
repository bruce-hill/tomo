test "defer in do block"
    >> x := 123
    nums : @[Int]
    do
        defer
            nums.insert(x)
        >> x = 999

    >> nums[]
    assert nums[] == [123]
    >> x
    assert x == 999

test "defer in loops"
    defer
        say("All done!")

    for word in ["first", "second", "third"]
        defer
            say("Got $word deferred")

        if word == "second"
            say("<skipped>")
            continue
        else if word == "third"
            say("<stopped>")
            break

        for i in 3
            defer
                say("Inner loop deferred $i")

            if i == 2
                say("<skipped inner>")
                continue
            else if i == 3
                say("<stopped inner>")
                break

            say("Made it through inner loop")

        say("Made it through the loop")

test "defer in closures"
    thunk := func(return_early=no)
        say("Entering thunk")
        defer
            say("Deferred thunk cleanup")

        if return_early
            say("Returning early...")
            return

        say("Finished thunk")

    >> thunk(no)
    >> thunk(yes)

test "defer in functions"
    >> defer_func(yes)
    >> defer_func(no)

test "defer in counter closure"
    >> counter := make_counter()
    assert counter() == 1
    assert counter() == 2
    assert counter() == 3

func defer_func(return_early=no)
    say("Entering defer_func")
    defer
        say("Deferred defer_func cleanup")

    if return_early
        say("Returning early...")
        return

    say("Finished defer_func")

func make_counter(->func(->Int))
    i := 1
    return func()
        defer i += 1
        return i
