# A table too wide for one line. Every entry appears once: the multi-line
# renderer used to append each one twice, which no file in the tree caught
# because none of them has a table that has to break.

func main()
    >> {
        "one": 1, "two": 2, "three": 3, "four": 4, "five": 5, "six": 6, "seven": 7, "eight": 8,
        "nine": 99999999999,
    }
