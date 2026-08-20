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

struct Bold{text:Text}
	convert(b:Bold -> HTML)
		return $HTML"<b>$(b.text)</b>"

test "HTML header constant"
	>> HTML.HEADER
	assert HTML.HEADER == $HTML"<!DOCTYPE HTML>"
	>> HTML.HEADER[1]
	assert HTML.HEADER[1] == $HTML"<"
	>> HTML.HEADER.text
	assert HTML.HEADER.text == "<!DOCTYPE HTML>"

test "HTML interpolation escapes user input"
	>> user := "I <3 hax"
	>> html := $HTML"Hello $user!"
	assert html == $HTML"Hello I &lt;3 hax!"
	>> html ++ $HTML"<br>"
	assert html ++ $HTML"<br>" == $HTML"Hello I &lt;3 hax!<br>"

test "HTML interpolation of numbers"
	>> $HTML"$(1 + 2)"
	assert $HTML"$(1 + 2)" == $HTML"3"
	>> $HTML"$(Int8(3))"
	assert $HTML"$(Int8(3))" == $HTML"3"

test "HTML paragraph helper and text conversion"
	>> user := "I <3 hax"
	>> html := $HTML"Hello $user!"
	>> html.paragraph()
	assert html.paragraph() == $HTML"<p>Hello I &lt;3 hax!</p>"
	>> Text(html)
	assert Text(html) == '\$HTML"Hello I &lt;3 hax!"'

test "custom struct to HTML conversion"
	>> b := Bold{"Some <text> with junk"}
	>> $HTML"Your text: $b"
	assert $HTML"Your text: $b" == $HTML"Your text: <b>Some &lt;text&gt; with junk</b>"
