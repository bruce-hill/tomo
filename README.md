# Tomo - Tomorrow's Language

Tomo is a programming language designed to anticipate and influence the
language design decisions of the future.

```
func greeting(name:Text)->Text
	greeting := "hello {name}!"
	return greeting:title()

>> greeting("world")
= "Hello World!"
```

Check out the [test/](test/) folder to see some examples.

## Dependencies

Tomo uses the [Boehm garbage collector](https://www.hboehm.info/gc/) for
runtime garbage collection (which is available from your package manager of
choice, for example: `pacman -S gc`).
