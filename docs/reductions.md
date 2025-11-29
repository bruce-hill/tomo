# Reductions

In Tomo, reductions are a way to express the idea of folding or reducing a
collection of values down to a single value. Reductions use a parenthesized
infix operator followed by a colon, followed by a collection:

```tomo
nums := [10, 20, 30]
sum := (+: nums)
assert sum == 60
```

Reductions return an optional value which will be a null value if the thing
being iterated over has no values. In such cases, the reduction is undefined.
As with all optionals, you can use either the postfix `!` operator to perform
a runtime check and error if there's a null value, or you can use `or` to
provide a fallback value:

```tomo
nums : [Int] = []
sum := (+: nums)

assert sum == none

assert sum or 0 == 0

nums = [10, 20]
assert (+: nums)! == 30
```

Reductions can be used as an alternative to generic functions like `sum()`,
`product()`, `any()`, and `all()` in Python, or higher-order functions like
`foldl` and `foldr` in functional programming:

```tomo
# Sum:
assert (+: [10, 20, 30])! == 60

# Product:
assert (*: [2, 3, 4])! == 24

# Any:
assert (or: [no, yes, no])! == yes

# All:
assert (and: [no, yes, no])! == no
```

## Minimum and Maximum

Reductions are _especially_ useful for finding the minimum or maximum values in
a collection using the `_min_` and `_max_` infix operators.

```tomo
# Get the maximum value:
assert (_max_: [10, 30, 20])! == 30

# Get the minimum value:
assert (_min_: [10, 30, 20])! == 10
```

Reducers also support field and method call suffixes, which makes it very easy
to compute the argmin/argmax (or keyed minimum/maximum) of a collection. This
is when you want to get the minimum or maximum value _according to some
feature_.

```tomo
# Get the longest text:
assert (_max_.length: ["z", "aaaaa", "mmm"])! == "aaaaa"

# Get the number with the biggest absolute value:
assert (_max_.abs(): [1, -2, 3, -4])! == -4
```

You can also use suffixes on other operators:

```tomo
texts := ["x", "y", "z"]
assert (==: texts) == no
assert (==.length: texts) == yes
assert (+.length: texts) == 3

nums := [1, 2, -3]
assert (+.abs(): nums) == 6
```

## Comprehensions

Reductions work not only with iterable values (lists, sets, integers, etc.),
but also with comprehensions. You can use comprehensions to perform reductions
while filtering out values or while applying a transformation:

```tomo
# Sum the lengths of these texts:
assert (+: t.length for t in ["a", "bc", "def"])! == 6

# Sum the primes between 1-100:
assert (+: i for i in 100 if i.is_prime())! == 1060
```
