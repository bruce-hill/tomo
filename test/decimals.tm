# Tests for decimal numbers

func square(n:Dec -> Dec)
    return n * n

func main()
    >> one_third := $1/$3
    = $0.333333333333333333333333333333
    >> two_thirds := $2/$3
    = $0.666666666666666666666666666667
    >> one_third + two_thirds == $1
    = yes

    >> square(5) # Promotion
    = $25

    >> square(Dec(1.5))
    = $2.25

    # Round-to-even:
    >> $1.5.round()
    = $2
    >> $2.5.round()
    = $2

    >> $2 + $3
    = $5

    >> $2 - $3
    = -$1

    >> $2 * $3
    = $6

    >> $3 ^ $2
    = $9

    >> $10.1 mod 3
    >> $1.1

    >> $10 mod1 5
    >> $5

    >> $1 + 2
    = $3

    >> $1 + Int64(2)
    = $3

