# Top-level statements are separated by newlines or by `;`, so a whole program
# can be written on one line, which is what `tomo eval` is handed:
x := 5; y := x * 2; say("$x is $y halved")

func main()
    say("$x $y")
