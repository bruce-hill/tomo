
struct Struct(x:Int, y:Text):
    func maybe(should_i:Bool->Struct?):
        if should_i:
            return Struct(123, "hello")
        else:
            return !Struct

enum Enum(X, Y(y:Int)):
    func maybe(should_i:Bool->Enum?):
        if should_i:
            return Enum.Y(123)
        else:
            return !Enum

func maybe_int(should_i:Bool->Int?):
    if should_i:
        return 123
    else:
        return !Int

func maybe_int64(should_i:Bool->Int64?):
    if should_i:
        return Int64(123)
    else:
        return !Int64

func maybe_array(should_i:Bool->[Int]?):
    if should_i:
        return [10, 20, 30]
    else:
        return ![Int]

func maybe_bool(should_i:Bool->Bool?):
    if should_i:
        return no
    else:
        return !Bool

func maybe_text(should_i:Bool->Text?):
    if should_i:
        return "Hello"
    else:
        return !Text

func maybe_num(should_i:Bool->Num?):
    if should_i:
        return 12.3
    else:
        return !Num

func maybe_lambda(should_i:Bool-> func()?):
    if should_i:
        return func(): say("hi!")
    else:
        return !func()

func maybe_c_string(should_i:Bool->CString?):
    if should_i:
        return ("hi":as_c_string())?
    else:
        return !CString

func maybe_channel(should_i:Bool->|Int|?):
    if should_i:
        return |:Int|?
    else:
        return !|Int|

func maybe_thread(should_i:Bool->Thread?):
    if should_i:
        return Thread.new(func(): pass)
    else:
        return !Thread

func main():
    >> 5?
    = 5? : Int?

    >> if no:
        !Int
    else:
        5
    = 5? : Int?

    >> 5? or -1
    = 5 : Int

    >> 5? or fail("Non-null is falsey")
    = 5 : Int

    >> 5? or exit("Non-null is falsey")
    = 5 : Int

    >> (!Int) or -1
    = -1 : Int

    do:
        !! Ints:
        >> yep := maybe_int(yes)
        = 123?
        >> nope := maybe_int(no)
        = !Int
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
        = 123?
        >> nope := maybe_int64(no)
        = !Int64
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
        = [10, 20, 30]?
        >> nope := maybe_array(no)
        = ![Int]
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
        = !Bool
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
        = !Text
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
        = !Num
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
        = func() [optionals.tm:54]?
        >> nope := maybe_lambda(no)
        = !func()
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
        = Struct(x=123, y="hello")?
        >> nope := Struct.maybe(no)
        = !Struct
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
        = !Enum
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
        = !CString
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
        = !|:Int|
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
        = !Thread
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
    >> (!Int == 5?)
    = no
    >> (5? == 5?)
    = yes
    >> {!Int, !Int}
    = {!Int}
    >> [5?, !Int, !Int, 6?]:sorted()
    = [!Int, !Int, 5?, 6?]

    do:
        >> value := if var := 5?:
            var
        else:
            0
        = 5

    do:
        >> value := if var := !Int:
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
        >> opt := !Int
        >> if opt:
            >> opt
        else:
            >> opt

    >> not 5?
    = no

    >> not !Int
    = yes

    >> [Struct(5,"A")?, Struct(6,"B"), Struct(7,"C")]
    = [Struct(x=5, y="A")?, Struct(x=6, y="B")?, Struct(x=7, y="C")?]
