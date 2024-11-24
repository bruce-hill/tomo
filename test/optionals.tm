
struct Struct(x:Int, y:Text):
    func maybe(should_i:Bool->Struct?):
        if should_i:
            return Struct(123, "hello")
        else:
            return NONE

enum Enum(X, Y(y:Int)):
    func maybe(should_i:Bool->Enum?):
        if should_i:
            return Enum.Y(123)
        else:
            return NONE

func maybe_int(should_i:Bool->Int?):
    if should_i:
        return 123
    else:
        return NONE

func maybe_int64(should_i:Bool->Int64?):
    if should_i:
        return Int64(123)
    else:
        return NONE

func maybe_array(should_i:Bool->[Int]?):
    if should_i:
        return [10, 20, 30]
    else:
        return NONE

func maybe_bool(should_i:Bool->Bool?):
    if should_i:
        return no
    else:
        return NONE

func maybe_text(should_i:Bool->Text?):
    if should_i:
        return "Hello"
    else:
        return NONE

func maybe_num(should_i:Bool->Num?):
    if should_i:
        return 12.3
    else:
        return NONE

func maybe_lambda(should_i:Bool-> func()?):
    if should_i:
        return func(): say("hi!")
    else:
        return NONE

func maybe_c_string(should_i:Bool->CString?):
    if should_i:
        return ("hi":as_c_string())?
    else:
        return NONE

func maybe_channel(should_i:Bool->|Int|?):
    if should_i:
        return |:Int|?
    else:
        return NONE

func maybe_thread(should_i:Bool->Thread?):
    if should_i:
        return Thread.new(func(): pass)
    else:
        return NONE

func main():
    >> 5?
    = 5 : Int?

    >> if no:
        NONE:Int
    else:
        5
    = 5 : Int?

    >> 5? or -1
    = 5 : Int

    >> 5? or fail("Non-null is falsey")
    = 5 : Int

    >> 5? or exit("Non-null is falsey")
    = 5 : Int

    >> (NONE:Int) or -1
    = -1 : Int

    do:
        !! Ints:
        >> yep := maybe_int(yes)
        = 123 : Int?
        >> nope := maybe_int(no)
        = NONE : Int?
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
        = 123 : Int64?
        >> nope := maybe_int64(no)
        = NONE : Int64?
        >> if yep:
            >> yep
            = 123
        else: fail("Falsey: $yep")
        >> if nope:
            fail("Truthy: $nope")
        else: !! Falsey: $nope

    do:
        !! ...
        !! Arrays:
        >> yep := maybe_array(yes)
        = [10, 20, 30] : [Int]?
        >> nope := maybe_array(no)
        = NONE : [Int]?
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
        = no : Bool?
        >> nope := maybe_bool(no)
        = NONE : Bool?
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
        = "Hello" : Text?
        >> nope := maybe_text(no)
        = NONE : Text?
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
        = 12.3 : Num?
        >> nope := maybe_num(no)
        = NONE : Num?
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
        >> yep := maybe_lambda(yes)
        = func() [optionals.tm:54] : func()?
        >> nope := maybe_lambda(no)
        = NONE : func()?
        >> if yep:
            >> yep
            = func() [optionals.tm:54]
        else: fail("Falsey: $yep")
        >> if nope:
            fail("Truthy: $nope")
        else: !! Falsey: $nope

    do:
        !! ...
        !! Structs:
        >> yep := Struct.maybe(yes)
        = Struct(x=123, y="hello") : Struct?
        >> nope := Struct.maybe(no)
        = NONE : Struct?
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
        = Enum.Y(123) : Enum?
        >> nope := Enum.maybe(no)
        = NONE : Enum?
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
        = CString("hi") : CString?
        >> nope := maybe_c_string(no)
        = NONE : CString?
        >> if yep:
            >> yep
            = CString("hi")
        else: fail("Falsey: $yep")
        >> if nope:
            fail("Truthy: $nope")
        else: !! Falsey: $nope

    do:
        !! ...
        !! Channels:
        >> yep := maybe_channel(yes)
        # No "=" test here because channels use addresses in the text version
        >> nope := maybe_channel(no)
        = NONE : |:Int|?
        >> if yep: >> yep
        else: fail("Falsey: $yep")
        >> if nope:
            fail("Truthy: $nope")
        else: !! Falsey: $nope

    do:
        !! ...
        !! Threads:
        >> yep := maybe_thread(yes)
        # No "=" test here because threads use addresses in the text version
        >> nope := maybe_thread(no)
        = NONE : Thread?
        >> if yep: >> yep
        else: fail("Falsey: $yep")
        >> if nope:
            fail("Truthy: $nope")
        else: !! Falsey: $nope

    if yep := maybe_int(yes):
        >> yep
        = 123 : Int
    else: fail("Unreachable")

    >> maybe_int(yes)!
    = 123 : Int

    # Test comparisons, hashing, equality:
    >> (NONE:Int == 5?)
    = no
    >> (5? == 5?)
    = yes
    >> {NONE:Int, NONE:Int}
    = {NONE}
    >> [5?, NONE:Int, NONE:Int, 6?]:sorted()
    = [NONE, NONE, 5, 6]

    do:
        >> value := if var := 5?:
            var
        else:
            0
        = 5

    do:
        >> value := if var := NONE:Int:
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
        >> opt := NONE:Int
        >> if opt:
            >> opt
        else:
            >> opt

    >> not 5?
    = no

    >> not NONE:Int
    = yes

    >> [Struct(5,"A")?, Struct(6,"B"), Struct(7,"C")]
    = [Struct(x=5, y="A"), Struct(x=6, y="B"), Struct(x=7, y="C")]
