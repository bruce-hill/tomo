# Tests for the 'when' block

test "when with multiple match values and else"
    >> answers := [
        (
            when x is "A","B" then "A or B"
            is "C" then "C"
            else "Other"
        ) for x in ["A", "B", "C", "D"]
    ]
    >> answers
    assert answers == ["A or B", "A or B", "C", "Other"]

test "when with computed match values"
    >> n := 23
    >> (
        when n is 1 Int64(1)
        is 2 Int64(2)
        is 21 + 2 Int64(23)
    )
    assert (
        when n is 1 Int64(1)
        is 2 Int64(2)
        is 21 + 2 Int64(23)
    ) == Int64(23)
