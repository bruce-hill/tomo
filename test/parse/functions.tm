func no_args()
    pass

func with_return(x:Int -> Int)
    return x + 1

func with_default(first:Int, second=0 -> Text)
    return "$first $second"

func main()
    with_default(1, 2)
    with_default(second=20, 10)
