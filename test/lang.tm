lang HTML
	HEADER := $HTML"<!DOCTYPE HTML>"
	convert(t:Text->HTML)
		t = t.translate({
			"&": "&amp;",
			"<": "&lt;",
			">": "&gt;",
			'"': "&quot",
			"'": "&#39;",
		})

		return HTML.from_text(t)

	convert(i:Int->HTML)
		return HTML.from_text("$i")
	
	func paragraph(content:HTML->HTML)
		return $HTML"<p>$content</p>"

struct Bold(text:Text)
	convert(b:Bold -> HTML)
		return $HTML"<b>$(b.text)</b>"

func main()
	assert HTML.HEADER == $HTML"<!DOCTYPE HTML>"

	assert HTML.HEADER[1] == $HTML"<"

	assert HTML.HEADER.text == "<!DOCTYPE HTML>"

	>> user := "I <3 hax"
	html := $HTML"Hello $user!"
	assert html == $HTML"Hello I &lt;3 hax!"
	assert html ++ $HTML"<br>" == $HTML"Hello I &lt;3 hax!<br>"

	assert $HTML"$(1 + 2)" == $HTML"3"

	assert $HTML"$(Int8(3))" == $HTML"3"

	assert html.paragraph() == $HTML"<p>Hello I &lt;3 hax!</p>"

	assert Text(html) == '\$HTML"Hello I &lt;3 hax!"'

	>> b := Bold("Some <text> with junk")
	assert $HTML"Your text: $b" == $HTML"Your text: <b>Some &lt;text&gt; with junk</b>"

