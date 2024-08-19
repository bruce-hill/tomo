# Metamethods

This language relies on a small set of "metamethods" which define special
behavior that is required for all types:

- `as_text(obj:&(optional)T, colorize=no, type:&TypeInfo)->Text`: a method to
  convert the type to a string. If `colorize` is `yes`, then the method should
  include ANSI escape codes for syntax highlighting. If the `obj` pointer is
  `NULL`, a string representation of the type will be returned instead.

- `compare(x:&T, y:&T, type:&TypeInfo)->Int32`: Return an integer representing
  the result of comparing `x` and `y`, where negative numbers mean `x` is less
  than `y`, zero means `x` is equal to `y`, and positive numbers mean `x` is
  greater than `y`. For the purpose of floating point numbers, `NaN` is sorted
  as greater than any other number value and `NaN` values are compared bitwise
  between each other.

- `equals(x:&T, y:&T, type:&TypeInfo)->Bool`: This is the same as comparing two
  numbers to check for zero, except for some minor differences: floating point
  `NaN` values are _not_ equal to each other (IEEE 754) and the implementation
  of `equals` may be faster to compute than `compare` for certain types, such
  as tables.

- `hash(x:&T, type:&TypeInfo)->Int32`: Values are hashed when used as keys in a
  table or set. Hashing is consistent with equality, so two values that are
  equal _must_ hash to the same hash value, ideally in a way that makes it
  unlikely that two different values will have the same hash value.

Metamethods are automatically defined for all user-defined structs, DSLs, and
enums. At this time, metamethods may not be overridden.

## Generic Metamethods

Due to the presence of pointers, arrays, tables, and functions, there are
potentially a very large number of metamethods that would be required if
_every_ type had its own set of metamethods. To reduce the amount of generated
code, Tomo uses generic metamethods, which are general-purpose functions that
take an object pointer and a type info struct pointer that has metadata about
the object's type. That metadata is added automatically at compile time and
used to perform the appropriate operations. As an example, every array follows
the same logic when performing comparisons, except that each item is compared
using the item's comparison function. Therefore, we can compile a single array
comparison function and reuse it for each type of array if we pass in some
metadata about how to compare the array's items.

When possible, we avoid calling metamethods (for example, doing fixed-sized
integer comparisons does not require calling a function), but metamethods are
available as a fallback or for working with container types or pointers.
