# The flags and modifiers a definition carries, and the `at` of a loop. None of
# these is written anywhere but the source, so the parse tree is the only place
# they are observable -- and `tomo format --verify` compares two parse trees to
# decide that formatting was faithful, so what is missing here it cannot check.
# A dropped `~colorized` passed as faithful for exactly that reason.

struct Secret{x:Int; secret}
struct External{x:Int; external}
struct Opaque{; opaque}
struct Packed{a:Bool, b:Bool; secret; packed_bools}

enum Tagged(Plain, Hidden{x:Int; secret}, Bits{a:Bool, b:Bool; packed_bools})

func inlined(x:Int -> Int; inline)
    return x

func remembered(x:Int -> Int; cached)
    return x

func bounded(x:Int -> Int; cache_size=100)
    return x

func both(x:Int -> Int; inline; cached)
    return x

func main()
    xs := [10, 20]
    counts : {Text:Int; default=0} = {}
    >> counts["absent"]
    for x at i in xs
        say("$i:$x")
    >> [x for x at i in xs]
    >> "a $xs value"~colorized
