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

>> iter := (./test.txt).each_line()
>> iter()
= "line one" : Text?
>> iter()
= "line two" : Text?
>> iter()
= "line three" : Text?
>> iter()
= none : Text?

for line in (./test.txt).each_line():
    pass
```

You can write your own iterator methods this way. For example, this iterator
iterates over prime numbers up to a given limit:

```tomo
func primes_up_to(limit:Int):
    n := 2
    return func():
        if n > limit:
            return !Int

        while not n.is_prime():
            n += 1

        n += 1
        return (n - 1)?

>> [p for p in primes_up_to(11)]
= [2, 3, 5, 7, 11]
```

