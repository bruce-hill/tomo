# Sets

Sets represent an unordered collection of unique elements. These are
implemented using hash tables.

```tomo
a := |10, 20, 30|
b := |20, 30|
>> a.overlap(b)
= |20|
```

## Syntax

Sets are written using `|...|` vertical pipes with comma-separated items:

```tomo
nums := |10, 20, 30|
```

Empty sets must specify the set type explicitly:

```tomo
empty : |Int| = ||
```

For type annotations, a set that holds items with type `T` is written as `|T|`.

### Comprehensions

Similar to lists, sets can use comprehensions:

```tomo
set := |10*i for i in 10|
set2 := |10*i for i in 10 if i mod 2 == 0|
set3 := |-10, 10*i for i in 10|
```

## Accessing Items

Sets internally store their items in a list, which you can access with the
`.items` field. This is a constant-time operation that produces an immutable
view:

```tomo
set := |10, 20, 30|
>> set.items
= [10, 20, 30]
```

## Length

Set length can be accessed by the `.length` field:

```tomo
>> |10, 20, 30|.length
= 3
```

## Iteration

You can iterate over the items in a table like this:

```tomo
for item in set
    ...

for i, item in set
    ...
```

Set iteration operates over the value of the set when the loop began, so
modifying the set during iteration is safe and will not result in the loop
iterating over any of the new values.

# API

[API documentation](../api/sets.md)
