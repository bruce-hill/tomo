# Operator Overloading

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

## Available Operator Overloads

When performing a math operation between any two types that are not both
numerical or boolean, the compiler will look up the appropriate method and
insert a function call to that method. Math overload operations are all assumed
to return a value that is the same type as the first argument and the second
argument must be either the same type as the first argument or a number,
depending on the specifications of the specific operator.

If no suitable method is found with the appropriate types, a compiler error
will be raised.

### Addition

```
func plus(T, T)->T
```

In an addition expression `a + b` between two objects of the same type, the
method `a:plus(b)` will be invoked, which returns a new value of the same type.

### Subtraction

```
func minus(T, T)->T
```

In a subtraction expression `a - b` between two objects of the same type, the
method `a:minus(b)` will be invoked, which returns a new value of the same type.

### Multiplication

```
func times(T, T)->T
func scaled_by(T, N)->T
```

The multiplication expression `a * b` invokes either the `a:times(b)` method,
if `a` and `b` are the same non-numeric type, or `a:scaled_by(b)` if `a` is
non-numeric and `b` is numeric, or `b:scaled_by(a)` if `b` is non-numeric and
`a` is numeric. In all cases, a new value of the non-numeric type is returned.

### Division

```
func divided_by(T, N)->T
```

In a division expression `a / b` the method `a:divided_by(b)` will be invoked
if `a` has type `T` and `b` has a numeric type `N`.

### Exponentiation

```
func power(T, N)->T
```

In an exponentiation expression, `a ^ b`, if `a` has type `T` and `b` has a
numeric type `N`, then the method `a:power(b)` will be invoked.

### Modulus

```
func mod(T, N)->T
func mod1(T, N)->T
```

In a modulus expression, `a mod b` or `a mod1 b`, if `a` has type `T` and `b`
has a numeric type `N`, then the method `mod()` or `mod1()` will be invoked.

### Negative

```
func negative(T)->T
```

In a unary negative expression `-x`, the method `negative()` will be invoked
and will return a value of the same type.

### Bit Operations

```
func left_shift(T, Int)->T
func right_shift(T, Int)->T
func bit_and(T, T)->T
func bit_or(T, T)->T
func bit_xor(T, T)->T
```

In a bit shifting expression, `a >> b` or `a << b`, if `a` has type `T` and `b`
is an `Int`, then the method `left_shift()` or `right_shift()` will be invoked.
A value of type `T` will be returned.

In a bitwise binary operation `a and b`, `a or b`, or `a xor b`, then the
method `bit_and()`, `bit_or()`, or `bit_xor()` will be invoked, assuming that
`a` and `b` have the same type, `T`. A value of type `T` will be returned.

### Bitwise Negation

```
func negated(T)->T
```

In a unary bitwise negation expression `not x`, the method `negated()` will be
invoked and will return a value of the same type.
