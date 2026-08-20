# Tests for the 'match' block

test "match with multiple match values and else"
    >> answers := [
        (
            match x case "A","B" then "A or B"
            case "C" then "C"
            else "Other"
        ) for x in ["A", "B", "C", "D"]
    ]
    >> answers
    assert answers == ["A or B", "A or B", "C", "Other"]

test "match with computed match values"
    >> n := 23
    >> (
        match n case 1 Int64(1)
        case 2 Int64(2)
        case 21 + 2 Int64(23)
    )
    assert (
        match n case 1 Int64(1)
        case 2 Int64(2)
        case 21 + 2 Int64(23)
    ) == Int64(23)
