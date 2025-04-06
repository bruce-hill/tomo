
struct Struct(x:Int, y:Text)
    func maybe(should_i:Bool->Struct?)
        if should_i
            return Struct(123, "hello")
        else
            return none

enum Enum(X, Y(y:Int))
    func maybe(should_i:Bool->Enum?)
        if should_i
            return Enum.Y(123)
        else
            return none

func maybe_int(should_i:Bool->Int?)
    if should_i
        return 123
    else
        return none

func maybe_int64(should_i:Bool->Int64?)
    if should_i
        return Int64(123)
    else
        return none

func maybe_array(should_i:Bool->[Int]?)
    if should_i
        return [10, 20, 30]
    else
        return none

func maybe_bool(should_i:Bool->Bool?)
    if should_i
        return no
    else
        return none

func maybe_text(should_i:Bool->Text?)
    if should_i
        return "Hello"
    else
        return none

func maybe_num(should_i:Bool->Num?)
    if should_i
        return 12.3
    else
        return none

func maybe_lambda(should_i:Bool-> func()?)
    if should_i
        return func() say("hi!")
    else
        return none

func maybe_c_string(should_i:Bool->CString?)
    if should_i
        return ("hi".as_c_string())?
    else
        return none

func main()
    >> 5?
    = 5?

    >> if no
        x : Int? = none
        x
    else
        5
    = 5?

    >> 5? or -1
    = 5

    >> 5? or fail("Non-null is falsey")
    = 5

    >> 5? or exit("Non-null is falsey")
    = 5

    >> none_int : Int? = none
    >> none_int or -1
    = -1

    do
        say("Ints:")
        >> yep := maybe_int(yes)
        = 123?
        >> nope := maybe_int(no)
        = none
        >> if yep
            >> yep
            = 123
        else fail("Falsey: $yep")
        >> if nope
            fail("Truthy: $nope")
        else say("Falsey: $nope")

    do
        say("Int64s:")
        >> yep := maybe_int64(yes)
        = Int64(123)?
        >> nope := maybe_int64(no)
        = none
        >> if yep
            >> yep
            = Int64(123)
        else fail("Falsey: $yep")
        >> if nope
            fail("Truthy: $nope")
        else say("Falsey: $nope")

    do
        say("Arrays:")
        >> yep := maybe_array(yes)
        = [10, 20, 30]?
        >> nope := maybe_array(no)
        = none
        >> if yep
            >> yep
            = [10, 20, 30]
        else fail("Falsey: $yep")
        >> if nope
            fail("Truthy: $nope")
        else say("Falsey: $nope")

    do
        say("...")
        say("Bools:")
        >> yep := maybe_bool(yes)
        = no?
        >> nope := maybe_bool(no)
        = none
        >> if yep
            >> yep
            = no
        else fail("Falsey: $yep")
        >> if nope
            fail("Truthy: $nope")
        else say("Falsey: $nope")

    do
        say("...")
        say("Text:")
        >> yep := maybe_text(yes)
        = "Hello"?
        >> nope := maybe_text(no)
        = none
        >> if yep
            >> yep
            = "Hello"
        else fail("Falsey: $yep")
        >> if nope
            fail("Truthy: $nope")
        else say("Falsey: $nope")

    do
        say("...")
        say("Nums:")
        >> yep := maybe_num(yes)
        = 12.3?
        >> nope := maybe_num(no)
        = none
        >> if yep
            >> yep
            = 12.3
        else fail("Falsey: $yep")
        >> if nope
            fail("Truthy: $nope")
        else say("Falsey: $nope")

    do
        say("...")
        say("Lambdas:")
        # >> yep := maybe_lambda(yes)
        # = func() [optionals.tm:54] : func()?
        >> nope := maybe_lambda(no)
        = none
        # >> if yep
        #     >> yep
        #     = func() [optionals.tm:54]
        # else fail("Falsey: $yep")
        >> if nope
            fail("Truthy: $nope")
        else say("Falsey: $nope")

    do
        say("...")
        say("Structs:")
        >> yep := Struct.maybe(yes)
        = Struct(x=123, y="hello")?
        >> nope := Struct.maybe(no)
        = none
        >> if yep
            >> yep
            = Struct(x=123, y="hello")
        else fail("Falsey: $yep")
        >> if nope
            fail("Truthy: $nope")
        else say("Falsey: $nope")

    do
        say("...")
        say("Enums:")
        >> yep := Enum.maybe(yes)
        = Enum.Y(123)?
        >> nope := Enum.maybe(no)
        = none
        >> if yep
            >> yep
            = Enum.Y(123)
        else fail("Falsey: $yep")
        >> if nope
            fail("Truthy: $nope")
        else say("Falsey: $nope")

    do
        say("...")
        say("C Strings:")
        >> yep := maybe_c_string(yes)
        = CString("hi")?
        >> nope := maybe_c_string(no)
        = none
        >> if yep
            >> yep
            = CString("hi")
        else fail("Falsey: $yep")
        >> if nope
            fail("Truthy: $nope")
        else say("Falsey: $nope")

    if yep := maybe_int(yes)
        >> yep
        = 123
    else fail("Unreachable")

    >> maybe_int(yes)!
    = 123

    # Test comparisons, hashing, equality:
    >> (none == 5?)
    = no
    >> (5? == 5?)
    = yes
    >> nones : |Int?| = |none, none|
    >> also_nones : |Int?| = |none|
    >> nones == also_nones
    >> [5?, none, none, 6?].sorted()
    = [none, none, 5, 6]

    do
        >> value := if var := 5?
            var
        else
            0
        = 5

    do
        >> value := if var : Int? = none
            var
        else
            0
        = 0

    do
        >> opt := 5?
        >> if opt
            >> opt
        else
            >> opt

    do
        >> opt : Int? = none
        >> if opt
            >> opt
        else
            >> opt

    >> not 5?
    = no

    >> nah : Int? = none
    >> not nah
    = yes

    >> [Struct(5,"A")?, Struct(6,"B"), Struct(7,"C")]
    = [Struct(x=5, y="A")?, Struct(x=6, y="B")?, Struct(x=7, y="C")?]

    if 5? or no
        say("Binary op 'or' works with optionals")
    else
        fail("Failed to do binary op 'or' on optional")
