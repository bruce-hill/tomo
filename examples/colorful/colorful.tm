# Colorful language
HELP := "
    colorful: A domain-specific language for writing colored text to the terminal
    Usage: colorful [args...] [--by-line] [--files files...]
"

CSI := "$\033["

lang Colorful:
    convert(text:Text -> Colorful):
        text = text:replace_all({$/@/="@(at)", $/(/="@(lparen)", $/)/="@(rparen)"})
        return Colorful.without_escaping(text)

    func for_terminal(c:Colorful -> Text):
        return CSI ++ "m" ++ _for_terminal(c, _TermState())

    func print(c:Colorful, newline=yes):
        say(c:for_terminal(), newline=newline)


func main(texts:[Text], files=[:Path], by_line=no):
    for i,text in texts:
        colorful := Colorful.without_escaping(text)
        colorful:print(newline=no)
        if i == texts.length: say("")
        else: say(" ", newline=no)

    if texts.length == 0 and files.length == 0:
        files = [(/dev/stdin)]

    for file in files:
        if by_line:
            for line in file:by_line() or exit("Could not read file: $(file.text)"):
                colorful := Colorful.without_escaping(line)
                colorful:print()
        else:
            colorful := Colorful.without_escaping(file:read() or exit("Could not read file: $(file.text)"))
            colorful:print(newline=no)


func _for_terminal(c:Colorful, state:_TermState -> Text):
    return c.text:map($/@(?)/, func(m:Match): _add_ansi_sequences(m.captures[1], state))

enum _Color(Default, Bright(color:Int16), Color8Bit(color:Int16), Color24Bit(color:Int32)):
    func from_text(text:Text -> _Color?):
        if text:matches($/#{3-6 hex}/):
            hex := text:from(2)
            return none unless hex.length == 3 or hex.length == 6
            if hex.length == 3:
                hex = hex[1]++hex[1]++hex[2]++hex[2]++hex[3]++hex[3]
            n := Int32.parse("0x" ++ hex) or return none
            return Color24Bit(n)
        else if text:matches($/{1-3 digit}/):
            n := Int16.parse(text) or return none
            if n >= 0 and n <= 255: return Color8Bit(n)
        else if text == "black": return _Color.Color8Bit(0)
        else if text == "red": return _Color.Color8Bit(1)
        else if text == "green": return _Color.Color8Bit(2)
        else if text == "yellow": return _Color.Color8Bit(3)
        else if text == "blue": return _Color.Color8Bit(4)
        else if text == "magenta": return _Color.Color8Bit(5)
        else if text == "cyan": return _Color.Color8Bit(6)
        else if text == "white": return _Color.Color8Bit(7)
        else if text == "default": return _Color.Default
        else if text == "BLACK": return _Color.Bright(0)
        else if text == "RED": return _Color.Bright(1)
        else if text == "GREEN": return _Color.Bright(2)
        else if text == "YELLOW": return _Color.Bright(3)
        else if text == "BLUE": return _Color.Bright(4)
        else if text == "MAGENTA": return _Color.Bright(5)
        else if text == "CYAN": return _Color.Bright(6)
        else if text == "WHITE": return _Color.Bright(7)
        return none

    func fg(c:_Color -> Text):
        when c is Color8Bit(color):
            if color >= 0 and color <= 7: return "$(30+color)"
            else if color >= 0 and color <= 255: return "38;5;$color"
        is Color24Bit(hex):
            if hex >= 0 and hex <= 0xFFFFFF:
                return "38;2;$((hex >> 16) and 0xFF);$((hex >> 8) and 0xFF);$((hex >> 0) and 0xFF)"
        is Bright(color):
            if color <= 7: return "$(90+color)"
        is Default:
            return "39"
        fail("Invalid foreground color: '$c'")

    func bg(c:_Color -> Text):
        when c is Color8Bit(color):
            if color >= 0 and color <= 7: return "$(40+color)"
            else if color >= 0 and color <= 255: return "48;5;$color"
        is Color24Bit(hex):
            if hex >= 0 and hex <= 0xFFFFFF:
                return "48;2;$((hex >> 16) and 0xFF);$((hex >> 8) and 0xFF);$((hex >> 0) and 0xFF)"
        is Bright(color):
            if color <= 7: return "$(90+color)"
        is Default:
            return "49"
        fail("Invalid background color: '$c'")

    func underline(c:_Color -> Text):
        when c is Color8Bit(color):
            if color >= 0 and color <= 255: return "58;5;$color"
        is Color24Bit(hex):
            if hex >= 0 and hex <= 0xFFFFFF:
                return "58;2;$((hex >> 16) and 0xFF);$((hex >> 8) and 0xFF);$((hex >> 0) and 0xFF)"
        is Default:
            return "59"
        is Bright(color):
            pass
        fail("Invalid underline color: '$c'")

func _toggle(sequences:&[Text], cur,new:Bool, apply,unapply:Text; inline):
    if new and not cur:
        sequences:insert(apply)
    else if cur and not new:
        sequences:insert(unapply)

func _toggle2(sequences:&[Text], cur1,cur2,new1,new2:Bool, apply1,apply2,unapply:Text; inline):
    return if new1 == cur1 and new2 == cur2
    if (cur1 and not new1) or (cur2 and not new2): # Gotta wipe at least one
        sequences:insert(unapply)
        cur1, cur2 = no, no # Wiped out

    if new1 and not cur1:
        sequences:insert(apply1)
    if new2 and not cur2:
        sequences:insert(apply2)

struct _TermState(
    bold=no, dim=no, italic=no, underline=no, blink=no,
    reverse=no, conceal=no, strikethrough=no, fraktur=no, frame=no,
    encircle=no, overline=no, superscript=no, subscript=no,
    bg=_Color.Default, fg=_Color.Default, underline_color=_Color.Default,
):

    func apply(old,new:_TermState -> Text):
        sequences := &[:Text]
        _toggle2(sequences, old.bold, old.dim, new.bold, new.dim, "1", "2", "22")
        _toggle2(sequences, old.italic, old.fraktur, new.italic, new.fraktur, "3", "20", "23")
        _toggle(sequences, old.underline, new.underline, "4", "24")
        _toggle(sequences, old.blink, new.blink, "5", "25")
        _toggle(sequences, old.reverse, new.reverse, "7", "27")
        _toggle(sequences, old.conceal, new.conceal, "8", "28")
        _toggle(sequences, old.strikethrough, new.strikethrough, "9", "29")
        _toggle2(sequences, old.frame, old.encircle, new.frame, new.frame, "51", "52", "54")
        _toggle(sequences, old.overline, new.overline, "53", "55")
        _toggle2(sequences, old.subscript, old.subscript, new.superscript, new.superscript, "73", "74", "75")

        if new.bg != old.bg:
            sequences:insert(new.bg:bg())

        if new.fg != old.fg:
            sequences:insert(new.fg:fg())

        if new.underline_color != old.underline_color:
            sequences:insert(new.underline_color:underline())

        if sequences.length == 0:
            return ""
        return CSI ++ ";":join(sequences) ++ "m"

func _add_ansi_sequences(text:Text, prev_state:_TermState -> Text):
    if text == "lparen": return "("
    else if text == "rparen": return ")"
    else if text == "@" or text == "at": return "@"
    parts := (
        text:matches($/{0+..}:{0+..}/) or
        return "@("++_for_terminal(Colorful.without_escaping(text), prev_state)++")"
    )
    attributes := parts[1]:split($/{0+space},{0+space}/)
    new_state := prev_state
    for attr in attributes:
        if attr:starts_with("fg="):
            new_state.fg = _Color.from_text(attr:from(4))!
        else if attr:starts_with("bg="):
            new_state.bg = _Color.from_text(attr:from(4))!
        else if attr:starts_with("ul="):
            new_state.underline_color = _Color.from_text(attr:from(4))!
        else if color := _Color.from_text(attr):
            new_state.fg = color
        else if attr == "b" or attr == "bold":
            new_state.bold = yes
        else if attr == "d" or attr == "dim":
            new_state.dim = yes
        else if attr == "i" or attr == "italic":
            new_state.italic = yes
        else if attr == "u" or attr == "underline":
            new_state.underline = yes
        else if attr == "B" or attr == "blink":
            new_state.blink = yes
        else if attr == "r" or attr == "reverse":
            new_state.reverse = yes
        else if attr == "fraktur":
            new_state.fraktur = yes
        else if attr == "frame":
            new_state.frame = yes
        else if attr == "encircle":
            new_state.encircle = yes
        else if attr == "overline":
            new_state.overline = yes
        else if attr == "super" or attr == "superscript":
            new_state.superscript = yes
        else if attr == "sub" or attr == "subscript":
            new_state.subscript = yes
        else:
            fail("Invalid attribute: '$attr'")

    result := prev_state:apply(new_state)
    result ++= parts[2]:map($/@(?)/, func(m:Match): _add_ansi_sequences(m.captures[1], new_state))
    result ++= new_state:apply(prev_state)
    return result
