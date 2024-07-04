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
            say("Got {word} deferred")

        if word == "second":
            say("<skipped>")
            skip
        else if word == "third":
            say("<stopped>")
            stop

        for i in 3:
            defer:
                say("Inner loop deferred {i}")

            if i == 2:
                say("<skipped inner>")
                skip
            else if i == 3:
                say("<stopped inner>")
                stop

            say("Made it through inner loop")

        say("Made it through the loop")
