# How parameter and field lists are spaced. Names sharing a type are separated
# like any other pair of arguments (`x, y:Int`); only the type they share waits
# for the last of them, and it stays against the name it belongs to.

func grouped(one, two:Int, three:Text -> Int)
    return two

func spread(a, b, c, d:Int -> Int)
    return a

# The same, long enough that the list has to break across lines: the two code
# paths that render an argument list used to disagree about this spacing.
func lengthy(first_argument, second_argument:Int, third_argument:Text, fourth_argument:[Int], fifth_argument:{Text:Int} -> Int)
    return first_argument

# The return type and the flags are part of what has to fit, so a signature can
# break on them alone. Once it does, they go down to the closing line: left on
# the last parameter's, after its comma, they read as one more parameter -- and
# it is the one place all three of these constructs accept them.
func on_the_suffix(arg1:Int, arg2:Int, arg3:Int, arg4:Int, arg5:Int, arg6:Int, arg7:Int, arg8:Int -> {Text:[Int]})
    return {}

func flagged(arg1xx:Int, arg2:Int, arg3:Int, arg4:Int, arg5:Int, arg6:Int, arg7:Int, arg8:Int -> Int; inline)
    return arg2

convert (arg1xxxxxxxx:Int, arg2:Int, arg3:Int, arg4:Int, arg5:Int, arg6:Int, arg7:Int, arg8:Int -> Text)
    return "$arg2"

func anonymous(-> func(a,b,c,d,e,f:Int -> Int))
    # An anonymous function wants its `)` against the return type, where a named
    # one wants it on a line of its own; the closing line is both.
    return func(first_argument:Int, second_argument:Int, third:Int, fourth:Int, fifth:Int, sixth:Int -> Int)
        first_argument

# A short flag alias travels with the name it aliases:
func main.go(paths:[Text], force|f:Bool=no, count|n:Int=1)
    say("$paths $force $count")

struct Grouped{x, y, z:Float64}
struct Mixed{pos, vel:Grouped, mass:Float64}
enum Tagged(Pair{a, b:Int}, Solo{t:Text})
