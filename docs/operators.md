# Operators

Tomo supports a number of operators, both infix and prefix:

- `+`,`-`,`*`,`/`: addition, subtraction, multiplication, and division for
  integers and floating point numbers
- `^`: exponentiation for integers and floating point numbers
- `mod`: modulus for integers and floating point numbers
- `mod1`: clock-style modulus, which is equivalent to `1 + ((x-1) mod y)`. This
  is particularly useful for doing wraparound behavior on 1-indexed lists.
- `++`: concatenation (for text and lists)
- `<<`, `>>`: bitwise left shift and right shift for integers
- `<<<`, `>>>`: unsigned bitwise left shift and right shift for integers
- `_min_`/`_max_`: minimum and maximum (see below)
- `<`, `<=`, `>`, `>=`, `==`, `!=`: comparisons
- `<>`: the signed comparison operator (see below)
- `and`, `or`, `xor`: logical operations for `Bool`s and bitwise operations for
  integers

## Signed Comparison Operator

When performing comparisons, Tomo will internally use APIs that take two
objects and return a signed 32-bit integer representing the relationship
between the two objects. The `<>` operator exposes this signed comparison
value to the user.

```tomo
>> 0 <> 99
= -1[32]
>> 5 <> 5
= 0[32]
>> 99 <> 0
= 1[32]
```

It's particularly handy for using the list `sort()` method, which takes a
function that returns a signed integer:

```tomo
>> foos:sort(func(a,b:&Foo): a.length <> b.length)
```

## Reducers

A novel feature introduced by Tomo is "reducer" operators, which function
similar to the `reduce` function in functional programming, but implemented
as a core language feature that runs as an inline loop. Reducers eliminate
the need for several polymorphic functions used in other languages like
`sum`, `any`, `all`, `reduce`, and `fold`. Here are some simple examples of
reducers in action:

```tomo
>> nums := [10, 20, 30]
>> (+: nums)!
= 60
>> (or: n > 15 for n in nums)!
= yes

>> texts := ["one", "two", "three"]
>> (++: texts)!
= "onetwothree"
```

The simplest form of a reducer is an infix operator surrounded by parentheses,
followed by either something that can be iterated over or a comprehension.
Results are produced by taking the first item in the iteration and repeatedly
applying the infix operator on subsequent items in the iteration to get a
result. Specifically for `and` and `or`, the operations are short-circuiting,
so the iteration will terminate early when possible.

## Handling Empty Iterables

In the case of an empty iteration cycle, there are two options available. The
first option is to not account for it, in which case you'll get a runtime error
if you use a reducer on something that has no values:

```tomo
>> nums := [:Int]
>> (+: nums)!

Error: this collection was empty!
```

If you want to handle this case, you can either wrap it in a conditional
statement or you can provide a fallback option with `else` like this:

```tomo
>> (+: nums) or 0
= 0
```

The `else` clause must be a value of the same type that would be returned.

## Extra Features

As syntactic sugar, reducers can also access fields, indices, or method calls.
This simplifies things if you want to do a reduction without writing a full
comprehension:

```tomo
struct Foo(x,y:Int):
    func is_even(f:Foo)->Bool:
        return (f.x + f.y) mod 2 == 0

>> foos := [Foo(1, 2), Foo(-10, 20)]

>> (+.x: foos)
= -9
// Shorthand for:
>> (+: f.x for f in foos)
= -9

>> (or):is_even() foos
= yes
// Shorthand for:
>> (or) f:is_even() for f in foos

>> (+.x:abs(): foos)
= 11
```

## `_min_` and `_max_`

Tomo introduces a new pair of operators that may be unfamiliar: `_min_` and
`_max_`. The `_min_` and `_max_` operators are infix operators that return the
larger or smaller of two elements:

```tomo
>> 3 _max_ 5
= 5
>> "XYZ" _min_ "ABC"
= "ABC"
```

Initially, this might seem like a fairly useless operator, but there are two
tricks that make these operators extremely versatile.

### Keyed Comparisons

The first trick is that the comparison operation supports keyed comparisons
that operate on a field, index, method call, or function call. This lets you
choose the larger or smaller of two elements _according to any standard_.
Here's some examples:

```tomo
// Get the largest absolute value number:
>> 3 _max_:abs() -15
= -15

struct Person(name:Text, age:Int)

// Get the oldest of two people:
>> Person("Alice", 33) _max_.age Person("Bob", 20)
= Person(name="Alice", age=33)

// Get the longest of two lists:
>> [10, 20, 30, 40] _max_.length [99, 1]
= [10, 20, 30, 40]

// Get the list with the highest value in the last position:
>> [10, 20, 999] _max_[-1] [99, 1]
= [10, 20, 999]
```

The keyed comparison can chain together multiple field accesses, list index
operations, method calls, etc. If you wanted to, for example, get the item
whose `x` field has the highest absolute value, you could use `_max_.x:abs()`.

### Working with Reducers

The second trick is that the `_min_` and `_max_` operators work with reducers.
This means that you get get the minimum or maximum element from an iterable
object using them:

```tomo
>> nums := [10, -20, 30, -40]
>> (_max_) nums
= 30

>> (_max_:abs()) nums
= -40
```

## Operator Overloading

Operator overloading is supported, but _strongly discouraged_. Operator
overloading should only be used for types that represent mathematical concepts
that users can be reliably expected to understand how they behave with math
operators, and for which the implementations are extremely efficient. Operator
overloading should not be used to hide expensive computations or to create
domain-specific syntax to make certain operations more concise. Examples of
good candidates for operator overloading would include:

- Mathematical vectors
- Matrices
- Quaternions
- Complex numbers

Bad candidates would include:

- Arbitrarily sized datastructures
- Objects that represent regular expressions
- Objects that represent filesystem paths

### Available Operator Overloads

When performing a math operation between any two types that are not both
numerical or boolean, the compiler will look up the appropriate method and
insert a function call to that method. Math overload operations are all assumed
to return a value that is the same type as the first argument and the second
argument must be either the same type as the first argument or a number,
depending on the specifications of the specific operator.

If no suitable method is found with the appropriate types, a compiler error
will be raised.

#### Addition

```
func plus(T, T)->T
```

In an addition expression `a + b` between two objects of the same type, the
method `a:plus(b)` will be invoked, which returns a new value of the same type.

#### Subtraction

```
func minus(T, T)->T
```

In a subtraction expression `a - b` between two objects of the same type, the
method `a:minus(b)` will be invoked, which returns a new value of the same type.

#### Multiplication

```
func times(T, T)->T
func scaled_by(T, N)->T
```

The multiplication expression `a * b` invokes either the `a:times(b)` method,
if `a` and `b` are the same non-numeric type, or `a:scaled_by(b)` if `a` is
non-numeric and `b` is numeric, or `b:scaled_by(a)` if `b` is non-numeric and
`a` is numeric. In all cases, a new value of the non-numeric type is returned.

#### Division

```
func divided_by(T, N)->T
```

In a division expression `a / b` the method `a:divided_by(b)` will be invoked
if `a` has type `T` and `b` has a numeric type `N`.

#### Exponentiation

```
func power(T, N)->T
```

In an exponentiation expression, `a ^ b`, if `a` has type `T` and `b` has a
numeric type `N`, then the method `a:power(b)` will be invoked.

#### Modulus

```
func mod(T, N)->T
func mod1(T, N)->T
```

In a modulus expression, `a mod b` or `a mod1 b`, if `a` has type `T` and `b`
has a numeric type `N`, then the method `mod()` or `mod1()` will be invoked.

#### Negative

```
func negative(T)->T
```

In a unary negative expression `-x`, the method `negative()` will be invoked
and will return a value of the same type.

#### Bit Operations

```
func left_shifted(T, Int)->T
func right_shifted(T, Int)->T
func unsigned_left_shifted(T, Int)->T
func unsigned_right_shifted(T, Int)->T
func bit_and(T, T)->T
func bit_or(T, T)->T
func bit_xor(T, T)->T
```

In a bit shifting expression, `a >> b` or `a << b`, if `a` has type `T` and `b`
is an `Int`, then the method `left_shift()` or `right_shift()` will be invoked.
A value of type `T` will be returned. The same is true for `>>>`
(`unsigned_right_shift()`) and `<<<` (`unsigned_left_shift`).

In a bitwise binary operation `a and b`, `a or b`, or `a xor b`, then the
method `bit_and()`, `bit_or()`, or `bit_xor()` will be invoked, assuming that
`a` and `b` have the same type, `T`. A value of type `T` will be returned.

#### Bitwise Negation

```
func negated(T)->T
```

In a unary bitwise negation expression `not x`, the method `negated()` will be
invoked and will return a value of the same type.
