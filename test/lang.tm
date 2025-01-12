lang HTML:
	HEADER := $HTML"<!DOCTYPE HTML>"
	func escape(t:Text->HTML):
		t = t:replace_all({
			$/&/="&amp;",
			$/</="&lt;",
			$/>/="&gt;",
			$/"/="&quot",
			$/'/="&#39;",
		})

		return HTML.without_escaping(t)

	func escape_int(i:Int->HTML):
		return HTML.without_escaping("$i")
	
	func paragraph(content:HTML->HTML):
		return $HTML"<p>$content</p>"

func main():
	>> HTML.HEADER
	= $HTML"<!DOCTYPE HTML>"

	>> HTML.HEADER[1]
	= $HTML"<"

	>> HTML.HEADER.text_content
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

	>> html:paragraph()
	= $HTML"<p>Hello I &lt;3 hax!</p>"

	>> Text(html)
	= '$HTML"Hello I &lt;3 hax!"'
