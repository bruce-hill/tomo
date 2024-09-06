USAGE := "
    Usage: ini <filename> "[section[/key]]"
"
HELP := "
    ini: A .ini config file reader tool.
    $USAGE
"

file := use ./file.tm

func parse_ini(filename:Text)->{Text:{Text:Text}}:
    text := when file.read(filename) is Failure(err): fail(err)
    is Success(text): text

    sections := {:Text:@{Text:Text}}
    current_section := @{:Text:Text}
    sections:set("", current_section)

    # Line wraps:
    text = text:replace($/\{1 nl}{0+space}/, " ")

    for line in text:lines():
        line = line:trim()
        if line:matches($/{0+space}[{..}]/):
            section_name := line:replace($/{0+space}[{..}]{..}/, "\2"):trim():lower()
            current_section = @{:Text:Text}
            sections:set(section_name, current_section)
        else if line:matches($/{..}={..}/):
            key := line:replace($/{..}={..}/, "\1"):trim():lower()
            value := line:replace($/{..}={..}/, "\2"):trim()
            current_section:set(key, value)

    return {k:v[] for k,v in sections}

func main(filename:Text, key:Text):
    keys := key:split($Pattern"/")
    if keys.length > 2:
        fail("
            Too many arguments! 
            $USAGE
        ")

    data := parse_ini(filename)
    if keys.length < 1 or keys[1] == '*':
        !! $data
        return

    section := keys[1]:lower()
    if not data:has(section):
        fail("Invalid section name: $section; valid names: $(", ":join([k:quoted() for k in data.keys]))")

    section_data := data:get(section)
    if keys.length < 2 or keys[2] == '*':
        !! $section_data
        return

    section_key := keys[2]:lower()
    if not section_data:has(section_key):
        fail("Invalid key: $section_key; valid keys: $(", ":join(section_data.keys))")

    say(section_data:get(section_key))
