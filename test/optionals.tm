
struct Struct{x:Int, y:Text}
    func maybe(should_i:Bool->Struct?)
        if should_i
            return Struct{123, "hello"}
        else
            return none

enum Enum(X, Y{y:Int})
    func maybe(should_i:Bool->Enum?)
        if should_i
            return Enum.Y{123}
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

func maybe_num(should_i:Bool->Float64?)
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

func maybe_path(should_i:Bool->Path?)
    if should_i
        return ./foo
    else
        return none

test "basic optional values"
    >> optional : Int? = 5
    >> optional
    assert optional == 5

    assert (
        if no
            x : Int? = none
            x
        else
            5
    ) == 5

    >> (optional or -1)
    assert (optional or -1) == 5
    assert (optional or fail("Non-none is falsey")) == 5
    assert (optional or exit("Non-none is falsey")) == 5
    >> none_int : Int? = none
    >> none_int
    assert none_int or -1 == -1

test "optional Ints"
    >> yep := maybe_int(yes)
    assert yep == 123
    >> nope := maybe_int(no)
    assert nope == none
    if yep
        assert yep == 123
    else fail("Falsey: $yep")
    if nope
        fail("Truthy: $nope")

test "optional Int64s"
    >> yep := maybe_int64(yes)
    assert yep == Int64(123)
    >> nope := maybe_int64(no)
    assert nope == none
    if yep
        assert yep == Int64(123)
    if nope
        fail("Truthy: $nope")

test "optional Lists"
    >> yep := maybe_list(yes)
    assert yep == [10, 20, 30]
    >> nope := maybe_list(no)
    assert nope == none
    if yep
        assert yep == [10, 20, 30]
    else fail("Falsey: $yep")
    if nope
        fail("Truthy: $nope")

test "optional Bools"
    >> yep := maybe_bool(yes)
    assert yep == no
    >> nope := maybe_bool(no)
    assert nope == none

test "Can't coerce optional booleans to booleans"
    yep := maybe_bool(yes)
    if yep
        assert yep == no
fails_compile "use an explicit `yep != none` check"

test "optional Text"
    >> yep := maybe_text(yes)
    assert yep == "Hello"
    >> nope := maybe_text(no)
    assert nope == none
    if yep
        assert yep == "Hello"
    else fail("Falsey: $yep")
    if nope
        fail("Truthy: $nope")

test "optional Nums"
    >> yep := maybe_num(yes)
    assert yep == 12.3
    >> nope := maybe_num(no)
    assert nope == none
    if yep
        assert yep == 12.3
    if nope
        fail("Truthy: $nope")

test "optional Lambdas"
    >> nope := maybe_lambda(no)
    >> nope
    assert nope == none
    if nope
        fail("Truthy: $nope")

test "optional Structs"
    >> yep := Struct.maybe(yes)
    assert yep == Struct{x=123, y="hello"}
    >> nope := Struct.maybe(no)
    assert nope == none
    if yep
        assert yep == Struct{x=123, y="hello"}
    if nope
        fail("Truthy: $nope")

test "optional Enums"
    >> yep := Enum.maybe(yes)
    assert yep == Enum.Y{123}
    >> nope := Enum.maybe(no)
    assert nope == none
    if yep
        assert yep == Enum.Y{123}
    if nope
        fail("Truthy: $nope")

test "optional C Strings"
    >> yep := maybe_c_string(yes)
    assert yep == CString("hi")
    >> nope := maybe_c_string(no)
    assert nope == none
    if yep
        assert yep == CString("hi")
    if nope
        fail("Truthy: $nope")

test "optional Paths"
    >> yep := maybe_path(yes)
    assert yep == ./foo
    >> nope := maybe_path(no)
    assert nope == none
    if yep
        assert yep == ./foo
    if nope
        fail("Truthy: $nope")

test "if-binding, force-unwrap, comparisons and hashing"
    >> optional : Int? = 5

    if yep := maybe_int(yes)
        >> yep
        assert yep == 123
    else fail("Unreachable")

    >> maybe_int(yes)!
    assert maybe_int(yes)! == 123

    # Test comparisons, hashing, equality:
    assert none != optional
    assert optional == 5
    >> nones : {Int?:Bool} = {none: yes, none: yes}
    >> nones.keys
    assert nones.keys == [none]
    >> [5, none, none, 6].sorted()
    assert [5, none, none, 6].sorted() == [none, none, 5, 6]

test "if-let binding uses value when present"
    >> optional : Int? = 5
    assert (if var := optional then var else 0) == 5

test "if-let binding falls through on none"
    assert (if var : Int? = none then var else 0) == 0

test "printing a present optional in if/else"
    opt : Int? = 5
    if opt
        >> opt
    else
        >> opt

test "printing a none optional in if/else"
    opt : Int? = none
    if opt
        >> opt
    else
        >> opt

test "'or' fallbacks can be untyped literals"
    # The fallback takes the optional's own type, so a bare literal works for
    # any of them, not just the bignum `Int`, whose type a literal already
    # has on its own.
    none_i64 : Int64? = none
    none_byte : Byte? = none
    none_f64 : Float64? = none
    none_f32 : Float32? = none
    none_num : Num? = none
    >> none_i64 or 0
    assert (none_i64 or 0) == Int64(0)
    assert (none_byte or 3) == Byte(3)
    assert (none_f64 or 0.0) == Float64(0)
    assert (none_f64 or 1.5) == Float64(1.5)
    assert (none_f32 or 1.5) == Float32(1.5)
    assert (none_num or 1./3.) == 1./3.
    # A present value still wins, and still comes out at that type:
    some_f64 : Float64? = 2.5
    >> kept : Float64 = some_f64 or 0.0
    assert kept == 2.5

test "negation and 'or' with optionals"
    >> optional : Int? = 5
    >> not optional
    assert (not optional) == no
    >> nah : Int? = none
    >> not nah
    assert (not nah) == yes
    assert [none, Struct{5,"A"}, Struct{6,"B"}, Struct{7,"C"}] == [none, Struct{x=5, y="A"}, Struct{x=6, y="B"}, Struct{x=7, y="C"}]
    if optional or no
        say("Binary op 'or' works with optionals")
    else
        fail("Failed to do binary op 'or' on optional")

test "force-unwrapping a none value panics"
    x : Int? = none
    _ := x!
fails "This was expected to be a value, but it's `none`"

test "dereferencing a maybe-none pointer is rejected"
    p : @Int? = none
    _ := p[]
fails_compile "Only pointers can use the '[]' operator to dereference the entire value."

test "reading a field through an optional is rejected"
    s : Struct? = Struct{5, "A"}
    _ := s.x
fails_compile "This value is optional (Struct?), so it might be none and I can't read its 'x' field"

test "a misspelled field on an optional still gets a suggestion"
    xs : [Int]? = [10, 20]
    n := xs.lenth
fails_compile "Did you mean 'length'?"

# `x or y` shares the `Or` AST node with bitwise-or, so it used to be mistaken
# for arithmetic whose type could be pushed into its operands:
test "an 'or' fallback can be used in arithmetic"
    n : Int? = 5
    assert (n or 0) + 1 == 6
    missing : Int? = none
    assert (missing or 0) + 1 == 1
    assert 2 * (n or 0) == 10
