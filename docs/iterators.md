# Iterators

Tomo supports using functions as iterable objects. This allows you to write
arbitrary iteration behavior, such as using a polling-based API, and write
regular loops or comprehensions over that API.

For example, the `Path.each_line()` API method returns a function that
successively gets one line from a file at a time until the file is exhausted:

```tomo
(./test.txt).write("
    line one
    line two
    line three
")

iter := (./test.txt).each_line()
assert iter() == "line one"
assert iter() == "line two"
assert iter() == "line three"
assert iter() == none

for line in (./test.txt).each_line()
    pass
```

You can write your own iterator methods this way. For example, this iterator
iterates over prime numbers up to a given limit:

```tomo
func primes_up_to(limit:Int)
    n := 2
    return func()
        if n > limit
            return none

        while not n.is_prime()
            n += 1

        n += 1
        return n - 1

assert [p for p in primes_up_to(11)] == [2, 3, 5, 7, 11]
```


## Multi-Value Iterators

An iterator that returns `T?` yields one value per iteration. To yield more
than one value per iteration, use the *out-parameter protocol*: a function
whose arguments are all non-escaping `&` pointers and whose return type is
`Bool`. Each call either writes the next values through the out-parameters
and returns `yes`, or returns `no` when the iteration is finished:

```tomo
# Yield each pair of distinct items (i < j) from a list:
func pairs(xs:[Int] -> func(a:&Int, b:&Int -> Bool))
    i := 1
    j := 1
    return func(a:&Int, b:&Int -> Bool)
        j += 1
        if j > xs.length
            i += 1
            j = i + 1
        if j > xs.length
            return no
        a[] = xs[i]!
        b[] = xs[j]!
        return yes

for a, b in pairs([10, 20, 30])
    say("$(a) $(b)")  # 10 20, 10 30, 20 30
```

The number of `&` arguments is the number of values the iterator yields, and
a loop over it must bind exactly that many variables (a mismatch is a compile
error). The loop variables themselves are the storage the iterator writes
into--no tuple, struct, or list is created per iteration. The yielded types
can differ from each other, `_` discards any position, and everything works
the same in comprehensions and reducers:

```tomo
distances := (+: a.dist(b) for a, b in pairs(points)) or 0.0
```

As with any loop, `at` binds an `Int64` iteration counter:

```tomo
for a, b at i in pairs([10, 20, 30])
    ...
```

## Lockstep Iteration

A loop can iterate over several iterables at once by listing them after `in`,
separated by commas. All of them advance together each iteration, and the loop
ends as soon as *any* of them runs out:

```tomo
xs := [10, 20, 30]
ys := ["a", "b", "c"]
for x, y in xs, ys
    say("$(x) $(y)")  # 10 a, 20 b, 30 c

dot := (+: x*y for x, y in [1, 2, 3], [4, 5, 6]) or 0  # 32
```

Each iterable's yielded values bind to its slice of the loop variables, in
order, and the total number of variables must match exactly (a mismatch is a
compile error). Any iterable kind can participate--lists, counts, ranges,
text, tables (which yield a key and a value), or iterator functions (which
yield one value per `&` out-parameter):

```tomo
for k1, v1, k2, v2 in t1, t2  # two tables, four values per iteration
    ...
```

`_` discards a value you don't need, and `at` binds an `Int64` iteration
counter as usual:

```tomo
for _, y at i in xs, ys
    ...
```

The iterables are evaluated once each, left to right, before the loop begins.
An `else` block runs if the loop body never ran (i.e. some iterable was empty
from the start).
