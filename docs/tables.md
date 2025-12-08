# Tables

Tables are Tomo's associative mapping structure, also known as a Dictionary or
Map. Tables are efficiently implemented as a hash table that preserves
insertion order and has fast access to keys and values as list slices. Tables
support *all* types as both keys and values.

## Syntax

Tables are written using `{}` curly braces with `:` colons associating key
expressions with value expressions and commas between entries:

```tomo
table := {"A": 10, "B": 20}
```

Empty tables must specify the key and value types explicitly:

```tomo
empty : {Text:Int} = {}
```

For type annotations, a table that maps keys with type `K` to values of type
`V` is written as `{K:V}`.

### Comprehensions

Similar to lists, tables can use comprehensions to dynamically construct tables:

```tomo
t := {i: 10*i for i in 10}
t := {i: 10*i for i in 10 if i mod 2 == 0}
t := {-1: -10, i: 10*i for i in 10}
```

## Accessing Values

Table values can be accessed with square bracket indexing. The result is an
optional value:

```tomo
table := {"A": 1, "B": 2}
assert table["A"] == 1
assert table["missing"] == none
```

As with all optional values, you can use the `!` postfix operator to assert
that the value is non-none (and create a runtime error if it is), or you can
use the `or` operator to provide a fallback value in the case that it's none:

```tomo
assert table["A"]! == 1

assert (table["missing"] or -1) == -1
```

### Fallback Tables

Tables can specify a fallback table that is used when looking up a value if it
is not found in the table itself:

```tomo
t := {"A": 10}
t2 := {"B": 20; fallback=t}
assert t2["A"] == 10
```

The fallback is available by the `.fallback` field, which returns an optional
table value:

```tomo
assert t2.fallback == {"A": 10}
assert t.fallback == none
```

### Default Values

Tables can specify a default value which will be returned if a value is not
present in the table or its fallback (if any).

```tomo
counts := &{"foo": 12; default=0}
assert counts["foo"] == 12
assert counts["baz"] == 0
counts["baz"] += 1
assert counts["baz"] == 1
```

When values are accessed from a table with a default value, the return type
is non-optional (because a value will always be present).

## Setting Values

You can assign a new key/value mapping or overwrite an existing one using
`.set(key, value)` or an `=` assignment statement:

```tomo
t := {"A": 1, "B": 2}
t["B"] = 222
t["C"] = 333
assert t == {"A": 1, "B": 222, "C": 333}
```

## Length

Table length can be accessed by the `.length` field:

```tomo
assert {"A": 10, "B": 20}.length == 2
```

## Accessing Keys and Values

The keys and values of a table can be efficiently accessed as lists using a
constant-time immutable slice of the internal data from the table:

```tomo
t := {"A": 10, "B": 20}
assert t.keys == ["A", "B"]
assert t.values == [10, 20]
```

## Iteration

You can iterate over the key/value pairs in a table like this:

```tomo
for key, value in table
    ...

for key in table
    ...
```

Table iteration operates over the value of the table when the loop began, so
modifying the table during iteration is safe and will not result in the loop
iterating over any of the new values.

## Sets

For an interface similar to Python's Sets, Tomo tables can be used with an
empty struct as its value type. For convenience, if a value or value is
omitted, Tomo will assign a default value type of `struct Present()` (an empty
struct). This way, the values stored in the table take up no space, but you
still have an easy way to represent Set-like data.

```tomo
nums := {10, 20, 30, 10}
assert nums.items == [10, 20, 30]
assert nums[10] == Present()
assert nums[99] == none
```

The following set-theoretic operations are available for tables:

- Set union: (AKA `or`) `{10, 20, 30}.with({30, 40})` -> `{10, 20, 30, 40}`
- Set intersection (AKA `and`) `{10, 20, 30}.intersection({30, 40})` -> `{10,
  20, 30, 40}`
- Set difference (AKA, `xor`, disjunctive union, symmetric difference) `{10,
  20, 30}.difference({30, 40})` -> `{10, 20, 40}`
- Set subtraction (AKA, `-`, asymmetric difference) `{10, 20, 30}.without({30,
  40})` -> `{10, 20}`

# API

[API documentation](../api/tables.md)
