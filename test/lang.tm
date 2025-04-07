lang HTML
	HEADER := $HTML"<!DOCTYPE HTML>"
	convert(t:Text->HTML)
		t = t.translate({
			"&"="&amp;",
			"<"="&lt;",
			">"="&gt;",
			'"'="&quot",
			"'"="&#39;",
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
	>> HTML.HEADER
	= $HTML"<!DOCTYPE HTML>"

	>> HTML.HEADER[1]
	= $HTML"<"

	>> HTML.HEADER.text
	= "<!DOCTYPE HTML>"

	>> user := "I <3 hax"
	>> html := $HTML"Hello $user!"
	= $HTML"Hello I &lt;3 hax!"
	>> html ++ $HTML"<br>"
	= $HTML"Hello I &lt;3 hax!<br>"

	>> $HTML"$(1 + 2)"
	= $HTML"3"

	>> $HTML"$(Int8(3))"
	= $HTML"3"

	>> html.paragraph()
	= $HTML"<p>Hello I &lt;3 hax!</p>"

	>> Text(html)
	= '\$HTML"Hello I &lt;3 hax!"'

	>> b := Bold("Some <text> with junk")
	>> $HTML"Your text: $b"
	= $HTML"Your text: <b>Some &lt;text&gt; with junk</b>"

