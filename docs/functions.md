# Functions and Lambdas

In Tomo, you can define functions with the `func` keyword:

```tomo
func add(x:Int, y:Int -> Int):
    return x + y
```

Functions require you to explicitly write out the types of each argument and
the return type of the function (unless the function doesn't return any values).

For convenience, you can lump arguments with the same type together to avoid
having to retype the same type name: `func add(x, y:Int -> Int)`

## Default Arguments

Instead of giving a type, you can provide a default argument and the type
checker will infer the type of the argument from that value:

```tomo
func increment(x:Int, amount=1 -> Int):
    return x + amount
```

Default arguments are used to fill in arguments that were not provided at the
callsite:

```tomo
>> increment(5)
= 6

>> increment(5, 10)
= 15
```

**Note:** Default arguments are re-evaluated at the callsite for each function
call, so if your default argument is `func foo(x=random:int(1,10) -> Int)`, then
each time you call the function without an `x` argument, it will give you a new
random number.

## Keyword Arguments

Tomo supports calling functions using keyword arguments to specify the values
of any argument. Keyword arguments can be at any position in the function call
and are bound to arguments first, followed by binding positional arguments to
any unbound arguments, in order:

```tomo
func foo(x:Int, y:Text, z:Num):
    return "x=$x y=$y z=$z"

>> foo(x=1, y="hi", z=2.5)
= "x=1 y=hi z=2.5"

>> foo(z=2.5, 1, "hi")
= "x=1 y=hi z=2.5"
```

As an implementation detail, all function calls are compiled to normal
positional argument passing, the compiler just does the work to determine which
order the arguments will be placed. Arguments are _evaluated_ in the order in
which they appear in code.

## Function Caching

Tomo supports automatic function caching using the `cached` or `cache_size=N`
attributes on a function definition:

```tomo
func add(x, y:Int -> Int; cached):
    return x + y
```

Cached functions are outwardly identical to uncached functions, but internally,
they maintain a table that maps a struct containing the input arguments to the
return value for those arguments. The above example is functionally similar to
the following code:

```tomo
func _add(x, y:Int -> Int):
    return x + y

struct add_args(x,y:Int)
add_cache := @{:add_args:Int}

func add(x, y:Int -> Int):
    args := add_args(x, y)
    if cached := add_cache[args]:
        return cached
    ret := _add(x, y)
    add_cache[args] = ret
    return ret
```

You can also set a maximum cache size, which causes a random cache entry to be
evicted if the cache has reached the maximum size and needs to insert a new
entry:

```tomo
func doop(x:Int, y:Text, z:[Int]; cache_size=100 -> Text):
    return "x=$x y=$y z=$z"
```

## Inline Functions

Functions can also be given an `inline` attribute, which encourages the
compiler to inline the function when possible:

```tomo
func add(x, y:Int -> Int; inline):
    return x + y
```

This will directly translate to putting the `inline` keyword on the function in
the transpiled C code.

# Lambdas

In Tomo, you can define lambda functions, also known as anonymous functions, like
this:

```tomo
fn := func(x,y:Int): x + y
```

The normal form of a lambda is to give a return expression after the colon,
but you can also use a block that includes statements:

```tomo
fn := func(x,y:Int):
    if x == 0:
        return y
    return x + y
```

Lambda functions must declare the types of their arguments, but do not require
declaring the return type. Because lambdas cannot be recursive or corecursive
(since they aren't declared with a name), it is always possible to infer the
return type without much difficulty. If you do choose to declare a return type,
the compiler will attempt to promote return values to that type, or give a
compiler error if the return value is not compatible with the declared return
type.

## Closures

When declaring a lambda function, any variables that are referenced from the
enclosing scope will be implicitly copied into a heap-allocated userdata
structure and attached to the lambda so that it can continue to reference those
values. **Captured values are copied to a new location at the moment the lambda
is created and will not reflect changes to local variables.**

```tomo
func create_adder(n:Int -> func(i:Int -> Int)):
    adder := func(i:Int):
        return n + i

    n = -1 // This does not affect the adder
    return adder
...
add10 := create_adder(10)
>> add10(5)
= 15
```

Under the hood, all user functions that are passed around in Tomo are passed as
a struct with two members: a function pointer and a pointer to any captured
values. When compiling the lambda to a function in C, we implicitly add a
`userdata` parameter and access fields on that structure when we need to access
variables from the closure. Captured variables _can_ be modified by the lambda
function, but those changes will only be visible to that particular lambda
function.

**Note:** if a captured value is a pointer to a value that lives in heap
memory, the pointer is copied, not the value in heap memory. This means that
you can have a lambda that captures a reference to a mutable object on the heap
and can modify that object. However, lambdas are not allowed to capture stack
pointers and the compiler will give you an error if you attempt to do so.
