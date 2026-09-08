# A value too wide for one line, behind `&` or `@`. It closes with a delimiter
# of its own, so it needs no parentheses added on top of it: `&(( ... ))` and
# `&( [ ... ] )` were both just noise around a list that already says where it
# ends.

func main()
    bodies := &[
        11111111111111111, 22222222222222222, 33333333333333333, 44444444444444444, 5555555555555555,
        66666666666666666, 77777777777777777, 88888888888888888, 99999999999999999, 1010101010101010,
    ]
    counts := @{
        "one": 11111111111111111, "two": 22222222222222222, "three": 3333333333333333, "four": 444444,
        "five": 55555555555555555,
    }
    >> bodies
    >> counts

func lambdas()
    # An element that trails off into an indented block has no line of its own
    # left to carry a comma, so the newline separates it -- the same shape an
    # argument list already handles this way.
    fns := [
        func()
            111111111111111 + 222222222222222 + 333333333333333 + 444444444444444 + 555555555555555 + 6666666666666
        func()
            2
    ]
    handlers := {
        "big": func()
            111111111111111 + 222222222222222 + 333333333333333 + 444444444444444 + 555555555555555 + 6666666666666
        "small": func()
            2
    }
    >> fns
    >> handlers
