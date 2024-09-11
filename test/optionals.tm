
struct Struct(x:Int, y:Text):
    func maybe(should_i:Bool)->Struct?:
        if should_i:
            return Struct(123, "hello")
        else:
            return !Struct

enum Enum(X, Y(y:Int)):
    func maybe(should_i:Bool)->Enum?:
        if should_i:
            return Enum.Y(123)
        else:
            return !Enum

func maybe_int(should_i:Bool)->Int?:
    if should_i:
        return 123
    else:
        return !Int

func maybe_array(should_i:Bool)->[Int]?:
    if should_i:
        return [10, 20, 30]
    else:
        return ![Int]

func maybe_bool(should_i:Bool)->Bool?:
    if should_i:
        return no
    else:
        return !Bool

func maybe_text(should_i:Bool)->Text?:
    if should_i:
        return "Hello"
    else:
        return !Text

func maybe_num(should_i:Bool)->Num?:
    if should_i:
        return 12.3
    else:
        return !Num

func maybe_lambda(should_i:Bool)-> func()?:
    if should_i:
        return func(): say("hi!")
    else:
        return !func()

func main():
    >> 5?
    = 5? : Int?

    >> if no:
        !Int
    else:
        5
    = 5? : Int?

    do:
        !! Ints:
        >> yep := maybe_int(yes)
        = 123?
        >> nope := maybe_int(no)
        = !Int
        >> if yep: >> yep
        else: fail("Falsey: $yep")
        >> if nope:
            fail("Truthy: $nope")
        else: !! Falsey: $nope

    do:
        !! ...
        !! Arrays:
        >> yep := maybe_array(yes)
        = [10, 20, 30]?
        >> nope := maybe_array(no)
        = ![Int]
        >> if yep: >> yep
        else: fail("Falsey: $yep")
        >> if nope:
            fail("Truthy: $nope")
        else: !! Falsey: $nope

    do:
        !! ...
        !! Bools:
        >> yep := maybe_bool(yes)
        = no?
        >> nope := maybe_bool(no)
        = !Bool
        >> if yep: >> yep
        else: fail("Falsey: $yep")
        >> if nope:
            fail("Truthy: $nope")
        else: !! Falsey: $nope

    do:
        !! ...
        !! Text:
        >> yep := maybe_text(yes)
        = "Hello"?
        >> nope := maybe_text(no)
        = !Text
        >> if yep: >> yep
        else: fail("Falsey: $yep")
        >> if nope:
            fail("Truthy: $nope")
        else: !! Falsey: $nope

    do:
        !! ...
        !! Nums:
        >> yep := maybe_num(yes)
        = 12.3?
        >> nope := maybe_num(no)
        = !Num
        >> if yep: >> yep
        else: fail("Falsey: $yep")
        >> if nope:
            fail("Truthy: $nope")
        else: !! Falsey: $nope

    do:
        !! ...
        !! Lambdas:
        >> yep := maybe_lambda(yes)
        = func(): ...?
        >> nope := maybe_lambda(no)
        = !func()
        >> if yep: >> yep
        else: fail("Falsey: $yep")
        >> if nope:
            fail("Truthy: $nope")
        else: !! Falsey: $nope

    do:
        !! ...
        !! Structs:
        >> yep := Struct.maybe(yes)
        = Struct(x=123, y="hello")?
        >> nope := Struct.maybe(no)
        = !Struct
        >> if yep: >> yep
        else: fail("Falsey: $yep")
        >> if nope:
            fail("Truthy: $nope")
        else: !! Falsey: $nope

    do:
        !! ...
        !! Enums:
        >> yep := Enum.maybe(yes)
        = Enum.Y(y=123)?
        >> nope := Enum.maybe(no)
        = !Enum
        >> if yep: >> yep
        else: fail("Falsey: $yep")
        >> if nope:
            fail("Truthy: $nope")
        else: !! Falsey: $nope

    if yep := maybe_int(yes):
        >> yep
        = 123 : Int
    else: fail("Unreachable")
