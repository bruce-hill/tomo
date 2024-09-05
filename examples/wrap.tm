HELP := "
    wrap: A tool for wrapping lines of text that are piped in through standard input

    usage: wrap [--help] [--width=80] [--min_split=3] [--no-rewrap] [--hyphen='-']
        --help: Print this message and exit
        --width=N: The width to wrap the text
        --min-split=N: The minimum amount of text on either end of a hyphenation split
        --rewrap|--no-rewrap: Whether to rewrap text that is already wrapped or only split long lines
        --hyphen='...': The text to use for hyphenation
"

file := use ./file.tm

UNICODE_HYPHEN := Text.from_codepoint_names(["hyphen"])

func unwrap(text:Text, preserve_paragraphs=yes, hyphen="-")->Text:
    if preserve_paragraphs:
        paragraphs := text:split($/{2+ nl}/)
        if paragraphs.length > 1:
            return \n\n:join([unwrap(p, hyphen=hyphen, preserve_paragraphs=no) for p in paragraphs])

    return text:replace($/$(hyphen)$(\n)/, "")

func wrap(text:Text, width:Int, min_split=3, hyphen="-")->Text:
    if width <= 0:
        fail("Width must be a positive integer, not $width")

    if 2*min_split - hyphen.length > width:
        fail("
            Minimum word split length ($min_split) is too small for the given wrap width ($width)!

            I can't fit a $(2*min_split - hyphen.length)-wide word on a line without splitting it,
        ... and I can't split it without splitting into chunks smaller than $min_split.
        ")

    lines := [:Text]
    line := ""
    for word in text:split($/{whitespace}/):
        letters := word:split()
        skip if letters.length == 0

        while not _can_fit_word(line, letters, width):
            line_space := width - line.length
            if line != "": line_space -= 1

            if min_split > 0 and line_space >= min_split + hyphen.length and letters.length >= 2*min_split:
                # Split word with a hyphen:
                split := line_space - hyphen.length
                split = split _max_ min_split
                split = split _min_ (letters.length - min_split)
                if line != "": line ++= " "
                line ++= ((++) letters:to(split)) ++ hyphen
                letters = letters:from(split + 1)
            else if line == "":
                # Force split word without hyphenation:
                if line != "": line ++= " "
                line ++= ((++) letters:to(line_space))
                letters = letters:from(line_space + 1)
            else:
                pass # Move to next line

            lines:insert(line)
            line = ""

        if letters.length > 0:
            if line != "": line ++= " "
            line ++= (++) letters

    if line != "":
        lines:insert(line)

    return \n:join(lines)

func _can_fit_word(line:Text, letters:[Text], width:Int; inline)->Bool:
    if line == "":
        return letters.length <= width
    else:
        return line.length + 1 + letters.length <= width

func main(width=80, min_split=3, rewrap=yes, hyphen="-"):
    when file.read("/dev/stdin") is Failure(reason):
        fail(reason)
    is Success(text):
        if rewrap:
            text = unwrap(text)

        first := yes
        for paragraph in text:split($/{2+ nl}/):
            if not first:
                say(\n, newline=no)
            say(wrap(paragraph, width=width, min_split=min_split, hyphen=hyphen))
            first = no
