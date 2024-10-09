func main():
    x := 123
    nums := @[:Int]
    do:
        defer:
            nums:insert(x)
        x = 999

    >> nums
    = @[123]
    >> x
    = 999

    defer:
        say("All done!")

    for word in ["first", "second", "third"]:
        defer:
            say("Got $word deferred")

        if word == "second":
            say("<skipped>")
            skip
        else if word == "third":
            say("<stopped>")
            stop

        for i in 3:
            defer:
                say("Inner loop deferred $i")

            if i == 2:
                say("<skipped inner>")
                skip
            else if i == 3:
                say("<stopped inner>")
                stop

            say("Made it through inner loop")

        say("Made it through the loop")
    
    >> thunk := func(return_early=no):
        say("Entering thunk")
        defer:
            say("Deferred thunk cleanup")

        if return_early:
            say("Returning early...")
            return

        say("Finished thunk")

    >> thunk(no)
    >> thunk(yes)

    >> defer_func(yes)
    >> defer_func(no)

    >> counter := make_counter()
    >> counter()
    = 1
    >> counter()
    = 2
    >> counter()
    = 3

func defer_func(return_early=no):
    say("Entering defer_func")
    defer:
        say("Deferred defer_func cleanup")

    if return_early:
        say("Returning early...")
        return

    say("Finished defer_func")

func make_counter()->func()->Int:
    i := 1
    return func():
        defer: i += 1
        return i

