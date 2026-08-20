# Nums

`Num` is Tomo's default numeric type, and it is **exact**. A number literal
like `1.1` is a `Num`, and arithmetic on `Num`s gives the mathematically
correct answer rather than the nearest one a machine word can hold:

```tomo
assert 0.1 + 0.2 == 0.3
assert (1./3.) * 3 == 1
assert (2.).sqrt()! * (2.).sqrt()! == 2
```

None of those hold for hardware floating point, where `0.1` isn't a tenth and
`0.1 + 0.2` is `0.30000000000000004`. If you want floating point -- because you
need hardware speed and can accept the error -- use
[`Float64`/`Float32`](floats.md).

## What a Num can hold

Three kinds of value, chosen automatically:

- **Small rationals** are packed into the value itself, with no allocation.
  This is the overwhelmingly common case and it is fast.
- **Big rationals** grow without limit: `(2.).power(100)` is exact, and so is
  a fraction with a thousand-digit denominator.
- **Irrationals** like `sqrt(2)`, `pi`, and `sin(2)` are kept symbolically, so
  identities hold exactly: `sqrt(2) * sqrt(2)` is `2`, not `1.9999999999999998`.

Literals can have a decimal point (`5.`), scientific notation (`1e8`), a
percent sign, or a `deg` suffix. A percent is a division by 100 (`5% == 0.05`)
and `deg` converts to radians -- exactly, since pi is exact here:

```tomo
assert 50% == 0.5
assert 180deg == Num.PI
```

Literals cost nothing at runtime: `0.5` compiles to a constant, the same as a
float literal would.

## Printing

Printing a `Num` never rounds. A value with a terminating decimal prints as
one; anything else prints in the exact form it has:

```tomo
assert "$(0.1 + 0.2)" == "0.3"
assert "$(1./3.)" == "1/3"
assert "$((2.).sqrt()!)" == "sqrt(2)"
assert "$(Num.PI)" == "pi"
```

## Getting digits

Since a `Num` is exact, turning one into digits means saying how many you
want. `:digits()` gives the decimal expansion, correctly rounded, and
`:is_exact()` says whether that many digits capture the value:

```tomo
assert (1./3.).digits(10) == "0.3333333333"
assert Num.PI.digits(10) == "3.1415926536"
assert not (1./3.).is_exact(10)

# An exact value stops early rather than padding zeros:
assert (0.25).digits(10) == "0.25"
assert (0.25).is_exact(10)
```

`:symbolic()` gives the exact form as text, and `:tex()` gives it as TeX:

```tomo
assert (2.).sqrt()!.symbolic() == "sqrt(2)"
assert (1./3.).tex() == "\\frac{1}{3}"
```

## Errors

Operators fail immediately on an undefined result, so a `Num` you are holding
is always a real number:

```tomo
# 1.0 / 0.0 raises a runtime error at the division
```

Methods whose argument can legitimately be out of range return `none` instead,
so they can be handled with `or` and `!`:

```tomo
assert (-1.).sqrt() == none
assert (0.).log() == none
assert (4.).sqrt()! == 2
assert ((-1.).sqrt() or 0.) == 0
```

Optional `Num`s cost nothing extra: `Num?` is the same size as `Num`.

## Equality

Rationals and the common irrational forms compare exactly, including across
different spellings of the same value:

```tomo
assert (2.).sqrt()! == (8.).sqrt()! / 2
assert Num.PI * 2 == Num.TAU
```

Deciding whether two arbitrary irrationals are equal is undecidable in
general, so Tomo settles it two ways: expressions with identical structure are
the same value, and anything still undecided counts as equal once the two
agree to 40 digits.

```tomo
assert (2.).sin() == (2.).sin()
assert (3. + 2 * (2.).sqrt()!).sqrt()! == 1 + (2.).sqrt()!
```

The 40-digit rule means two values differing only past the 40th digit are
treated as the same. In exchange, comparison always terminates and always
gives an answer.

## Converting

To an integer, strictly by default -- pass `truncate=yes` to round toward
zero:

```tomo
assert Int(4.) == 4
assert Int(4.9, truncate=yes) == 4
assert Int((2.).power(100)) == 1267650600228229401496703205376
# Int(4.9) would fail: it isn't a whole number
```

To a float, approximating by default, since a float *is* the approximation:

```tomo
assert Float64(0.5) == 0.5
# Float64(1./3., truncate=no) would fail: a third has no float
```

From an integer or a float, always exactly, so there's no `truncate` argument.
Note that converting a float converts the value the float actually holds:

```tomo
assert Num(5) == 5
assert Num(Float64(0.5)) == 0.5
assert Num(Float64(0.1)) != 0.1   # that float is not a tenth
```

## Performance

Exactness is not free. Small rational arithmetic is fast -- comparable to
hardware floating point for integers and simple fractions -- but big rationals
allocate, and irrationals build an expression that is evaluated to whatever
precision is demanded of it. Accumulating irrationals in a loop grows the
expression with the loop, so code that needs bounded cost should round to a
rational at chosen points with `:round()` or `:digits()`, accepting the
explicit loss of exactness. Code that needs raw speed and can accept error
should use [`Float64`](floats.md).

# API

[API documentation](../api/nums.md)
