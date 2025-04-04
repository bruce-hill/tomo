
struct Struct(x:Int, y:Text):
    func maybe(should_i:Bool->Struct?):
        if should_i:
            return Struct(123, "hello")
        else:
            return none

enum Enum(X, Y(y:Int)):
    func maybe(should_i:Bool->Enum?):
        if should_i:
            return Enum.Y(123)
        else:
            return none

func maybe_int(should_i:Bool->Int?):
    if should_i:
        return 123
    else:
        return none

func maybe_int64(should_i:Bool->Int64?):
    if should_i:
        return Int64(123)
    else:
        return none

func maybe_array(should_i:Bool->[Int]?):
    if should_i:
        return [10, 20, 30]
    else:
        return none

func maybe_bool(should_i:Bool->Bool?):
    if should_i:
        return no
    else:
        return none

func maybe_text(should_i:Bool->Text?):
    if should_i:
        return "Hello"
    else:
        return none

func maybe_num(should_i:Bool->Num?):
    if should_i:
        return 12.3
    else:
        return none

func maybe_lambda(should_i:Bool-> func()?):
    if should_i:
        return func(): say("hi!")
    else:
        return none

func maybe_c_string(should_i:Bool->CString?):
    if should_i:
        return ("hi":as_c_string())?
    else:
        return none

func main():
    >> 5?
    = 5?

    >> if no:
        none:Int
    else:
        5
    = 5?

    >> 5? or -1
    = 5

    >> 5? or fail("Non-null is falsey")
    = 5

    >> 5? or exit("Non-null is falsey")
    = 5

    >> (none:Int) or -1
    = -1

    do:
        !! Ints:
        >> yep := maybe_int(yes)
        = 123?
        >> nope := maybe_int(no)
        = none:Int
        >> if yep:
            >> yep
            = 123
        else: fail("Falsey: $yep")
        >> if nope:
            fail("Truthy: $nope")
        else: !! Falsey: $nope

    do:
        !! ...
        !! Int64s:
        >> yep := maybe_int64(yes)
        = Int64(123)?
        >> nope := maybe_int64(no)
        = none:Int64
        >> if yep:
            >> yep
            = Int64(123)
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
        = none:[Int]
        >> if yep:
            >> yep
            = [10, 20, 30]
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
        = none:Bool
        >> if yep:
            >> yep
            = no
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
        = none:Text
        >> if yep:
            >> yep
            = "Hello"
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
        = none:Num
        >> if yep:
            >> yep
            = 12.3
        else: fail("Falsey: $yep")
        >> if nope:
            fail("Truthy: $nope")
        else: !! Falsey: $nope

    do:
        !! ...
        !! Lambdas:
        # >> yep := maybe_lambda(yes)
        # = func() [optionals.tm:54] : func()?
        >> nope := maybe_lambda(no)
        = none : func()
        # >> if yep:
        #     >> yep
        #     = func() [optionals.tm:54]
        # else: fail("Falsey: $yep")
        >> if nope:
            fail("Truthy: $nope")
        else: !! Falsey: $nope

    do:
        !! ...
        !! Structs:
        >> yep := Struct.maybe(yes)
        = Struct(x=123, y="hello")?
        >> nope := Struct.maybe(no)
        = none:Struct
        >> if yep:
            >> yep
            = Struct(x=123, y="hello")
        else: fail("Falsey: $yep")
        >> if nope:
            fail("Truthy: $nope")
        else: !! Falsey: $nope

    do:
        !! ...
        !! Enums:
        >> yep := Enum.maybe(yes)
        = Enum.Y(123)?
        >> nope := Enum.maybe(no)
        = none : Enum
        >> if yep:
            >> yep
            = Enum.Y(123)
        else: fail("Falsey: $yep")
        >> if nope:
            fail("Truthy: $nope")
        else: !! Falsey: $nope

    do:
        !! ...
        !! C Strings:
        >> yep := maybe_c_string(yes)
        = CString("hi")?
        >> nope := maybe_c_string(no)
        = none : CString
        >> if yep:
            >> yep
            = CString("hi")
        else: fail("Falsey: $yep")
        >> if nope:
            fail("Truthy: $nope")
        else: !! Falsey: $nope

    if yep := maybe_int(yes):
        >> yep
        = 123
    else: fail("Unreachable")

    >> maybe_int(yes)!
    = 123

    # Test comparisons, hashing, equality:
    >> (none:Int == 5?)
    = no
    >> (5? == 5?)
    = yes
    >> {none:Int, none:Int}
    = {none:Int}
    >> {:Int? none, none}
    = {none:Int}
    >> [5?, none:Int, none:Int, 6?]:sorted()
    = [none:Int, none:Int, 5, 6]

    do:
        >> value := if var := 5?:
            var
        else:
            0
        = 5

    do:
        >> value := if var := none:Int:
            var
        else:
            0
        = 0

    do:
        >> opt := 5?
        >> if opt:
            >> opt
        else:
            >> opt

    do:
        >> opt := none:Int
        >> if opt:
            >> opt
        else:
            >> opt

    >> not 5?
    = no

    >> not none:Int
    = yes

    >> [Struct(5,"A")?, Struct(6,"B"), Struct(7,"C")]
    = [Struct(x=5, y="A")?, Struct(x=6, y="B")?, Struct(x=7, y="C")?]

    if 5? or no:
        say("Binary op 'or' works with optionals")
    else:
        fail("Failed to do binary op 'or' on optional")
