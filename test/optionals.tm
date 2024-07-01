
func main():
    >> opt := @5?
    when opt is @nonnull:
        >> nonnull[]
        = 5
    else:
        fail("Oops")

    >> opt = !@Int
    when opt is @nonnull:
        fail("Oops")
    else:
        >> opt
        = !Int
