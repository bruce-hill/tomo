# Domain-Specific Languages

Tomo supports defining different flavors of text that represent specific
languages, with type safety guarantees that help prevent code injection. Code
injection occurs when you insert untrusted user input into a string without
properly escaping the user input. Tomo's `lang` feature addresses this issue by
letting you define custom text types that automatically escape interpolated
values and give type checking errors if you attempt to use one type of string
where a different type of string is needed.

```tomo
lang HTML:
    func HTML(t:Text -> HTML):
        t = t:replace_all({
            $/&/ = "&amp;",
            $/</ = "&lt;",
            $/>/ = "&gt;",
            $/"/ = "&quot",
            $/'/ = "&#39;",
        })
        return HTML.without_escaping(t)

    func paragraph(content:HTML -> HTML):
        return $HTML"<p>$content</p>"
```

In this example, we're representing HTML as a language and we want to avoid
situations where a malicious user might set their username to something like
`<script>alert('pwned')</script>`.

```
>> username := Text.read_line("Choose a username: ")
= "<script>alert('pwned')</script>"
page := $HTML"
    <html><body>
    Hello $username! How are you?
    </body></html>
"
say(page.text_content)
```

What we _don't_ want to happen is to get a page that looks like:

```html
<html><body>
Hello <script>alert('pwned')</script>! How are you?
</body></html>
```

Thankfully, Tomo handles automatic escaping and gives you a properly sanitized
result:

```html
<html><body>
Hello &lt;script&gt;alert(&#39;pwned&#39;)&lt;/script&gt;! How are you?
</body></html>
```

This works because the compiler checks for a function in the HTML namespace
that was defined with the name `HTML` that takes a `Text` argument and returns
an `HTML` value (a constructor). When performing interpolation, the
interpolation will only succeed if such a function exists and it will apply
that function to the value before concatenating it.

If you have a function that only accepts an `HTML` argument, you cannot use a
`Text` value, you must produce a valid `HTML` value instead. The same is true
for returning a value for a function that returns an `HTML` value or assigning
to a variable that holds `HTML` values.

Languages can also be built around a namespace-based method call API, instead
of building a global function API that takes language arguments. For example,
instead of building a global function called `execute()` that takes a
`ShellScript` argument, you could instead build something like this:

```tomo
lang Sh:
    func Sh(text:Text -> Sh):
        return Sh.without_escaping("'" ++ text:replace($/'/, "''") ++ "'")

    func execute(sh:Sh -> Text):
        ...

dir := ask("List which dir? ")
cmd := $Sh@(ls -l @dir)
result := cmd:execute()
```
