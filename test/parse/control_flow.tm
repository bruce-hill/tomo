func main()
    x := 5
    if x == 1
        say("one")
    else if x == 2
        say("two")
    else
        say("many")

    for i in 10
        continue if i == 3
        break if i == 5

    while x > 0
        x = x - 1

    say("done") if x == 0
