# Metamethods

This language relies on a small set of "metamethods" which define special
behavior that is required for all types:

- `as_str(obj:&(optional)T, colorize=no)->Str`: a method to convert the type to a
  string. If `colorize` is `yes`, then the method should include ANSI escape
  codes for syntax highlighting. If the `obj` pointer is `NULL`, a string
  representation of the type will be returned instead.

- `compare(x:&T, y:&T)->Int32`: Return an integer representing the result
  of comparing `x` and `y`, where negative numbers mean `x` is less than `y`,
  zero means `x` is equal to `y`, and positive numbers mean `x` is greater than
  `y`. For the purpose of floating point numbers, `NaN` is sorted as greater
  than any other number value and `NaN` values are compared bitwise between
  each other.

- `equals(x:&T, y:&T)->Bool`: This is the same as comparing two numbers to
  check for zero, except for some minor differences: floating point `NaN`
  values are _not_ equal to each other (IEEE 754) and the implementation of
  `equals` may be faster to compute than `compare` for certain types, such as
  tables.

Metamethods are automatically defined for all user-defined structs, DSLs, and
enums.

## Generic Metamethods

Due to the presence of pointers, arrays, tables, and functions, there are
potentially a very large number of metamethods that would be required if
_every_ type had its own set of metamethods. To reduce the amount of generated
code, we use generic metamethods, which are general-purpose functions that take
an automatically compiled format string and variable number of arguments that
describe how to run a metamethod for that type. As a simple example, if `foo`
is an array of type `Foo`, which has a defined `as_str()` method, then
rather than define a separate `Foo_Array_as_str()` function that would be
99% identical to a `Baz_Array_as_str()` function, we instead insert a call
to `as_str(&foo, colorize, "[_]", Foo__as_str)` to convert a `[Foo]`
array to a string, and you call `as_str(&baz, colorize, "[_]",
Baz__as_str)` to convert a `[Baz]` array to a string. The generic metamethod
handles all the reusable logic like "an array's string form starts with a '[',
then iterates over the items, getting the item's string form (whatever that is)
and putting commas between them".

Similarly, to compare two tables, we would use `compare(&x, &y, "{_=>_}",
KeyType__compare, ValueType__compare)`. Or to hash an array of arrays of type
`Foo`, we would use `hash(&foo, "[[_]]", Foo__hash)`.
