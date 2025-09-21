# Tests for the 'when' block

func main()
    answers := [
        (
            when x is "A","B" then "A or B"
            is "C" then "C"
            else "Other"
        ) for x in ["A", "B", "C", "D"]
    ]
    assert answers == ["A or B", "A or B", "C", "Other"]

    n := 23
    assert (
        when n is 1 Int64(1)
        is 2 Int64(2)
        is 21 + 2 Int64(23)
    ) == Int64(23)
