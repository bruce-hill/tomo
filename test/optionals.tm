
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

func maybe_list(should_i:Bool->[Int]?)
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
        return "hi".as_c_string()
    else
        return none

func main()
    optional : Int? = 5
    assert optional == 5

    assert (
        if no
            x : Int? = none
            x
        else
            5
    ) == 5

    assert (optional or -1) == 5

    assert (optional or fail("Non-none is falsey")) == 5

    assert (optional or exit("Non-none is falsey")) == 5

    >> none_int : Int? = none
    assert none_int or -1 == -1

    do
        say("Ints:")
        yep := maybe_int(yes)
        assert yep == 123
        nope := maybe_int(no)
        assert nope == none
        >> if yep
            assert yep == 123
        else fail("Falsey: $yep")
        >> if nope
            fail("Truthy: $nope")
        else say("Falsey: $nope")

    do
        say("Int64s:")
        yep := maybe_int64(yes)
        assert yep == Int64(123)
        nope := maybe_int64(no)
        assert nope == none
        >> if yep
            assert yep == Int64(123)
        else fail("Falsey: $yep")
        >> if nope
            fail("Truthy: $nope")
        else say("Falsey: $nope")

    do
        say("Lists:")
        yep := maybe_list(yes)
        assert yep == [10, 20, 30]
        nope := maybe_list(no)
        assert nope == none
        >> if yep
            assert yep == [10, 20, 30]
        else fail("Falsey: $yep")
        >> if nope
            fail("Truthy: $nope")
        else say("Falsey: $nope")

    do
        say("...")
        say("Bools:")
        yep := maybe_bool(yes)
        assert yep == no
        nope := maybe_bool(no)
        assert nope == none
        >> if yep
            assert yep == no
        else fail("Falsey: $yep")
        >> if nope
            fail("Truthy: $nope")
        else say("Falsey: $nope")

    do
        say("...")
        say("Text:")
        yep := maybe_text(yes)
        assert yep == "Hello"
        nope := maybe_text(no)
        assert nope == none
        >> if yep
            assert yep == "Hello"
        else fail("Falsey: $yep")
        >> if nope
            fail("Truthy: $nope")
        else say("Falsey: $nope")

    do
        say("...")
        say("Nums:")
        yep := maybe_num(yes)
        assert yep == 12.3
        nope := maybe_num(no)
        assert nope == none
        >> if yep
            assert yep == 12.3
        else fail("Falsey: $yep")
        >> if nope
            fail("Truthy: $nope")
        else say("Falsey: $nope")

    do
        say("...")
        say("Lambdas:")
        nope := maybe_lambda(no)
        assert nope == none
        >> if nope
            fail("Truthy: $nope")
        else say("Falsey: $nope")

    do
        say("...")
        say("Structs:")
        yep := Struct.maybe(yes)
        assert yep == Struct(x=123, y="hello")
        nope := Struct.maybe(no)
        assert nope == none
        >> if yep
            assert yep == Struct(x=123, y="hello")
        else fail("Falsey: $yep")
        >> if nope
            fail("Truthy: $nope")
        else say("Falsey: $nope")

    do
        say("...")
        say("Enums:")
        yep := Enum.maybe(yes)
        assert yep == Enum.Y(123)
        nope := Enum.maybe(no)
        assert nope == none
        >> if yep
            assert yep == Enum.Y(123)
        else fail("Falsey: $yep")
        >> if nope
            fail("Truthy: $nope")
        else say("Falsey: $nope")

    do
        say("...")
        say("C Strings:")
        yep := maybe_c_string(yes)
        assert yep == CString("hi")
        nope := maybe_c_string(no)
        assert nope == none
        >> if yep
            assert yep == CString("hi")
        else fail("Falsey: $yep")
        >> if nope
            fail("Truthy: $nope")
        else say("Falsey: $nope")

    if yep := maybe_int(yes)
        assert yep == 123
    else fail("Unreachable")

    assert maybe_int(yes)! == 123

    # Test comparisons, hashing, equality:
    assert none != optional
    assert optional == 5
    >> nones : {Int?:Bool} = {none: yes, none: yes}
    assert nones.keys == [none]
    assert [5, none, none, 6].sorted() == [none, none, 5, 6]

    do
        assert (if var := optional then var else 0) == 5

    do
        assert (if var : Int? = none then var else 0) == 0

    do
        >> opt : Int? = 5
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

    assert not optional == no

    >> nah : Int? = none
    assert not nah == yes

    assert [none, Struct(5,"A"), Struct(6,"B"), Struct(7,"C")] == [none, Struct(x=5, y="A"), Struct(x=6, y="B"), Struct(x=7, y="C")]

    if optional or no
        say("Binary op 'or' works with optionals")
    else
        fail("Failed to do binary op 'or' on optional")
