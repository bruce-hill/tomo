# A `packed_bools` struct stores each `Bool` field in one bit and each `Bool?`
# field in two. The layout itself is checked by the `_Static_assert`s that the
# compiler emits beside every struct definition; what's checked here is that
# values still behave exactly like they do in an unpacked struct.

struct Flags{a:Bool, b:Bool, c:Bool; packed_bools}

struct Loose{a:Bool, b:Bool, c:Bool}

struct Maybes{a:Bool?, b:Bool?, c:Bool?, d:Bool?, e:Bool?; packed_bools}

struct Ten{b1:Bool, b2:Bool, b3:Bool, b4:Bool, b5:Bool, b6:Bool, b7:Bool, b8:Bool, b9:Bool, b10:Bool; packed_bools}

# Seven bits, then a two-bit field that can't fit in what's left of the byte:
struct Straddle{b1:Bool, b2:Bool, b3:Bool, b4:Bool, b5:Bool, b6:Bool, b7:Bool, o:Bool?; packed_bools}

struct Mixed{a:Bool, n:Int32, b:Bool?, c:Bool, t:Text, d:Bool; packed_bools}

struct Secretive{a:Bool, b:Bool; secret, packed_bools}

# Same fields as the packed structs above, for measuring against:
struct LooseMaybes{a:Bool?, b:Bool?, c:Bool?, d:Bool?, e:Bool?}

struct LooseTen{b1:Bool, b2:Bool, b3:Bool, b4:Bool, b5:Bool, b6:Bool, b7:Bool, b8:Bool, b9:Bool, b10:Bool}

struct LooseStraddle{b1:Bool, b2:Bool, b3:Bool, b4:Bool, b5:Bool, b6:Bool, b7:Bool, o:Bool?}

struct LooseMixed{a:Bool, n:Int32, b:Bool?, c:Bool, t:Text, d:Bool}

# A lone bool between two aligned fields fits in padding either way, so packing
# it saves nothing -- the flag only pays off for *adjacent* booleans:
struct Lonely{n:Int32, only:Bool, m:Int32; packed_bools}

struct LooseLonely{n:Int32, only:Bool, m:Int32}

# Bit-packed fields in byte 0, then padding bytes 1-3 that belong to no field:
struct WithHole{a:Bool, b:Bool, c:Bool, n:Int32; packed_bools}

# The flag goes on individual enum members, not the enum, so packed and
# unpacked members can sit side by side:
enum Shape(Nothing, Packed{a:Bool, b:Bool, c:Bool; packed_bools}, Unpacked{a:Bool, b:Bool, c:Bool}, PackedMaybes{x:Bool?, y:Bool?, z:Bool?; packed_bools}, WithFields{n:Int32, a:Bool, b:Bool; packed_bools})

test "packed bools behave like unpacked ones"
    >> Flags{yes, no, yes}
    assert "$(Flags{yes, no, yes})" == "Flags{a=yes, b=no, c=yes}"
    assert "$(Flags{yes, no, yes})" == "$(Loose{yes, no, yes})".replace("Loose", "Flags")
    assert Flags{yes, no, yes} == Flags{yes, no, yes}
    assert Flags{yes, no, yes} != Flags{yes, yes, yes}
    assert Flags{yes, no, yes} != Flags{no, no, yes}
    assert Flags{yes, no, yes} != Flags{yes, no, no}

test "every field is independent"
    # Each field has to read and write only its own bit:
    for i in 3
        flags := Flags{i == 1, i == 2, i == 3}
        assert flags.a == (i == 1)
        assert flags.b == (i == 2)
        assert flags.c == (i == 3)

test "packed bool fields can be assigned through a pointer"
    flags := @Flags{no, no, no}
    flags.b = yes
    assert flags[] == Flags{no, yes, no}
    flags.a = yes
    flags.b = no
    assert flags[] == Flags{yes, no, no}

test "optional bools keep all three states"
    >> Maybes{yes, none, no, none, yes}
    assert "$(Maybes{yes, none, no, none, yes})" == "Maybes{a=yes, b=none, c=no, d=none, e=yes}"
    assert Maybes{yes, none, no, none, yes} == Maybes{yes, none, no, none, yes}

    # `none` is distinct from both `yes` and `no`:
    assert Maybes{none, no, no, no, no} != Maybes{no, no, no, no, no}
    assert Maybes{none, no, no, no, no} != Maybes{yes, no, no, no, no}

test "bit runs longer than a byte"
    >> Ten{yes, no, yes, no, yes, no, yes, no, yes, no}
    assert Ten{yes, no, yes, no, yes, no, yes, no, yes, no}.b9
    assert not Ten{yes, no, yes, no, yes, no, yes, no, yes, no}.b10
    assert Ten{no, no, no, no, no, no, no, no, yes, no} != Ten{no, no, no, no, no, no, no, no, no, no}

test "a two-bit field that doesn't fit starts a new byte"
    >> Straddle{yes, yes, yes, yes, yes, yes, yes, none}
    assert Straddle{yes, yes, yes, yes, yes, yes, yes, none}.o == none
    assert Straddle{no, no, no, no, no, no, no, yes}.o == yes
    assert Straddle{no, no, no, no, no, no, no, none} != Straddle{no, no, no, no, no, no, no, no}

test "packed bools mixed with byte-addressed fields"
    m := Mixed{yes, Int32(42), none, yes, "hello", no}
    >> m
    assert "$m" == 'Mixed{a=yes, n=42, b=none, c=yes, t="hello", d=no}'
    assert m.n == Int32(42)
    assert m.t == "hello"
    assert m != Mixed{yes, Int32(42), none, yes, "hello", yes}
    assert m != Mixed{yes, Int32(43), none, yes, "hello", no}

test "packed structs hash and compare"
    >> counts := {Flags{yes, no, yes}: 1, Flags{no, yes, no}: 2}
    assert counts[Flags{yes, no, yes}]! == 1
    assert counts[Flags{no, yes, no}]! == 2
    assert counts[Flags{yes, yes, yes}] == none
    seen := {Maybes{yes, none, no, none, yes}: yes}
    assert seen.has(Maybes{yes, none, no, none, yes})
    assert Flags{no, no, yes} < Flags{yes, no, no}
    >> [Flags{yes, no, no}, Flags{no, no, yes}, Flags{no, yes, no}].sorted()

test "packed structs serialize"
    assert deserialize:Flags(serialize(Flags{yes, no, yes}))! == Flags{yes, no, yes}
    assert deserialize:Maybes(serialize(Maybes{yes, none, no, none, yes}))! == Maybes{yes, none, no, none, yes}
    ten := Ten{yes, no, yes, no, yes, no, yes, no, yes, no}
    assert deserialize:Ten(serialize(ten))! == ten
    straddle := Straddle{yes, yes, yes, yes, yes, yes, yes, none}
    assert deserialize:Straddle(serialize(straddle))! == straddle
    m := Mixed{yes, Int32(42), none, yes, "hello", no}
    assert deserialize:Mixed(serialize(m))! == m

test "packed structs in lists"
    list := [Flags{yes, no, no}, Flags{no, yes, no}, Flags{no, no, yes}]
    >> list
    assert list[1]! == Flags{yes, no, no}
    assert list[3]! == Flags{no, no, yes}
    assert list.length == 3

test "packed structs can also be secret"
    assert "$(Secretive{yes, no})" == "Secretive{...}"
    assert Secretive{yes, no} == Secretive{yes, no}
    assert Secretive{yes, no} != Secretive{yes, yes}

test "a pointer to a byte-addressed field of a packed struct still works"
    m := @Mixed{yes, Int32(42), none, yes, "hello", no}
    n := &m.n
    n[] = Int32(99)
    assert m.n == Int32(99)

test "taking a pointer to a bit-packed bool is rejected"
    flags := @Flags{yes, no, yes}
    p := &flags.a
    >> p
fails_compile "has no address of its own to point at"

test "taking a pointer to a bit-packed optional bool is rejected"
    maybes := @Maybes{yes, none, no, none, yes}
    p := &maybes.b
    >> p
fails_compile "has no address of its own to point at"

test "implicitly taking a pointer to a bit-packed bool is rejected"
    flags := @Flags{yes, no, yes}
    p : &Bool = flags.a
    >> p
fails_compile "has no address of its own"

test "enum members can pack their bools"
    >> Shape.Packed{yes, no, yes}
    assert "$(Shape.Packed{yes, no, yes})" == "Packed{a=yes, b=no, c=yes}"
    assert Shape.Packed{yes, no, yes} == Shape.Packed{yes, no, yes}
    assert Shape.Packed{yes, no, yes} != Shape.Packed{yes, yes, yes}

    # A packed member and an unpacked one with the same values are still
    # different values of the enum:
    assert Shape.Packed{yes, no, yes} != Shape.Unpacked{yes, no, yes}
    >> Shape.PackedMaybes{yes, none, no}
    assert Shape.PackedMaybes{yes, none, no} != Shape.PackedMaybes{yes, no, no}
    >> Shape.WithFields{Int32(7), yes, no}

    match Shape.WithFields{Int32(7), yes, no}
    case WithFields{n, a, b}
        assert n == Int32(7)
        assert a and not b
    else
        fail("expected a WithFields shape")

test "packed enum members match, hash, and serialize"
    shapes := [
        Shape.Packed{yes, no, yes}, Shape.Unpacked{no, yes, no}, Shape.Nothing,
        Shape.PackedMaybes{yes, none, no},
    ]

    for shape in shapes
        assert deserialize:Shape(serialize(shape))! == shape

    match shapes[1]!
    case Packed{a, b, c}
        assert a and not b and c
    else
        fail("expected a Packed shape")

    counts := {Shape.Packed{yes, no, yes}: 1, Shape.PackedMaybes{yes, none, no}: 2}
    assert counts[Shape.Packed{yes, no, yes}]! == 1
    assert counts[Shape.PackedMaybes{yes, none, no}]! == 2
    assert counts[Shape.Packed{no, no, no}] == none

# `sizeof(@var)` avoids naming the mangled C type, which changes with the file.
func size_of_packed(x:Flags -> Int64)
    return C_code:Int64`(int64_t)sizeof(@x)`

test "packing shrinks structs"
    packed := Flags{yes, no, yes}
    loose := Loose{yes, no, yes}
    assert C_code:Int64`(int64_t)sizeof(@packed)` == 1
    assert C_code:Int64`(int64_t)sizeof(@loose)` == 3

    # Two bits each, four to a byte:
    maybes := Maybes{yes, none, no, none, yes}
    loose_maybes := LooseMaybes{yes, none, no, none, yes}
    assert C_code:Int64`(int64_t)sizeof(@maybes)` == 2
    assert C_code:Int64`(int64_t)sizeof(@loose_maybes)` == 5

    # A run longer than one byte spills into the next:
    ten := Ten{yes, no, yes, no, yes, no, yes, no, yes, no}
    loose_ten := LooseTen{yes, no, yes, no, yes, no, yes, no, yes, no}
    assert C_code:Int64`(int64_t)sizeof(@ten)` == 2
    assert C_code:Int64`(int64_t)sizeof(@loose_ten)` == 10

    # Seven bits used, a two-bit field that won't fit, so a second byte:
    straddle := Straddle{yes, yes, yes, yes, yes, yes, yes, none}
    loose_straddle := LooseStraddle{yes, yes, yes, yes, yes, yes, yes, none}
    assert C_code:Int64`(int64_t)sizeof(@straddle)` == 2
    assert C_code:Int64`(int64_t)sizeof(@loose_straddle)` == 8

test "packing only pays off for adjacent booleans"
    # `only` sits in padding that alignment opens up either way, so both
    # layouts are the same size and the flag buys nothing here:
    packed := Lonely{Int32(1), yes, Int32(2)}
    loose := LooseLonely{Int32(1), yes, Int32(2)}
    assert C_code:Int64`(int64_t)sizeof(@packed)` == C_code:Int64`(int64_t)sizeof(@loose)`

test "packed and unpacked structs serialize identically"
    # Serialization is field-by-field, so the bytes can't depend on the layout.
    # This also fails if the bits are read from the wrong end of a byte.
    assert serialize(Flags{yes, no, yes}) == serialize(Loose{yes, no, yes})
    assert serialize(Maybes{yes, none, no, none, yes}) == serialize(LooseMaybes{yes, none, no, none, yes})
    ten := Ten{yes, no, yes, no, yes, no, yes, no, yes, no}
    loose_ten := LooseTen{yes, no, yes, no, yes, no, yes, no, yes, no}
    assert serialize(ten) == serialize(loose_ten)
    mixed := Mixed{yes, Int32(42), none, yes, "hi", no}
    loose_mixed := LooseMixed{yes, Int32(42), none, yes, "hi", no}
    assert serialize(mixed) == serialize(loose_mixed)

test "deserializing reuses buffers without carrying bits over"
    # Table and list deserialization reuse one buffer for every entry, so a `no`
    # read after a `yes` in the same bit position has to clear it:
    list := [Flags{yes, yes, yes}, Flags{no, no, no}, Flags{yes, no, yes}, Flags{no, yes, no}]
    assert deserialize:[Flags](serialize(list))! == list
    maybes := [
        Maybes{yes, yes, yes, yes, yes}, Maybes{none, none, none, none, none},
        Maybes{no, no, no, no, no},
    ]
    assert deserialize:[Maybes](serialize(maybes))! == maybes
    table := {Flags{yes, yes, yes}: 1, Flags{no, no, no}: 2, Flags{yes, no, yes}: 3}
    assert deserialize:{Flags:Int}(serialize(table))! == table

test "bits that belong to no field are not part of the value"
    # Every `·` in the layout holds whatever was in that memory. If equality or
    # hashing looked at whole bytes, dirtying them would break both.
    x := @WithHole{yes, no, yes, Int32(7)}
    y := @WithHole{yes, no, yes, Int32(7)}
    assert x[] == y[]
    C_code`((unsigned char*)@(y))[1] = (unsigned char)0xFF;` # a padding byte
    assert "$(y[])" == "$(x[])"
    assert x[] == y[]
    assert {x[]: 1}[y[]]! == 1
    assert serialize(x[]) == serialize(y[])

    # ...and the unused bits inside a byte the fields do share. Which bits those
    # are depends on which end the platform allocates from:
    p := @Flags{yes, no, yes}
    q := @Flags{yes, no, yes}
    C_code`
        #if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        *(unsigned char*)@(q) |= (unsigned char)0x1F;
        #else
        *(unsigned char*)@(q) |= (unsigned char)0xF8;
        #endif
    `
    assert "$(q[])" == "Flags{a=yes, b=no, c=yes}"
    assert p[] == q[]
    assert {p[]: 1}[q[]]! == 1
    assert serialize(p[]) == serialize(q[])

test "unpacked bool fields can still be pointed at"
    # The whole reason packing is opt-in: without the flag, a bool field has an
    # address like any other field.
    loose := @Loose{yes, no, yes}
    p := &loose.a
    p[] = no
    assert loose[] == Loose{no, no, yes}
    assert p[] == no

test "unpacked optional bool fields can be pointed at"
    loose := @LooseMaybes{yes, none, no, none, yes}
    p := &loose.b
    p[] = no
    assert loose.b == no
    p[] = none
    assert loose.b == none
