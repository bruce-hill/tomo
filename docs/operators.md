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

### Addition

```
func plus(T, T)->T
```

The `+` operator invokes the `plus()` method, which takes two objects of the
same type and returns a new value of the same type.

### Subtraction

```
func minus(T, T)->T
```

The `-` operator invokes the `minus()` method, which takes two objects of the
same type and returns a new value of the same type.

### Multiplication

```
func times(T, T)->T
```

The `*` operator invokes the `times()` method, which takes two objects of the
same type and returns a new value of the same type. This should _not_ be used
to implement either a dot product or a cross product. Dot products and cross
products should be implemented as explicitly named methods.

```
func scaled_by(T, N)->T
```

In a multiplication expression, `a*b`, if either `a` or `b` has type `T` and
the other has a numeric type `N` (either `Int8`, `Int16`, `Int32`, `Int`,
`Num32`, or `Num`), then the method `scaled_by()` will be invoked.

### Division

```
func divided_by(T, N)->T
```

In a division expression, `a/b`, if `a` has type `T` and `b` has a numeric
type `N`, then the method `divided_by()` will be invoked.

### Exponentiation

```
func power(T, N)->T
```

In an exponentiation expression, `a^b`, if `a` has type `T` and `b` has a
numeric type `N`, then the method `power()` will be invoked.

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

In a unary negative expression `-x`, the method `negative()` will be invoked.

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

In a bitwise binary operation `a and b`, `a or b`, or `a xor b`, then the
method `bit_and()`, `bit_or()`, or `bit_xor()` will be invoked.

### Bitwise Negation

```
func negated(T)->T
```

In a unary bitwise negation expression `not x`, the method `negated()` will be
invoked.
