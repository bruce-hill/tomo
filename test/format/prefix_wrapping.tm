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
