# Tests for the 'when' block

func main():
    str := "B"
    when str is "A":
        fail("First")
    is "B":
        say("Success")
    is "C":
        fail("Third")

    n := 23
    >> when n is 1: Int64(1)
    is 2: Int64(2)
    is 21 + 2: Int64(23)
    = 23 : Int64?
