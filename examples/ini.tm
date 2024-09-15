_USAGE := "
    Usage: ini <filename> "[section[/key]]"
"
_HELP := "
    ini: A .ini config file reader tool.
    $_USAGE
"

func parse_ini(path:Path)->{Text:{Text:Text}}:
    text := path:read()
    sections := {:Text:@{Text:Text}}
    current_section := @{:Text:Text}
    sections:set("", current_section)

    # Line wraps:
    text = text:replace($/\{1 nl}{0+space}/, " ")

    for line in text:lines():
        line = line:trim()
        skip if line:starts_with(";") or line:starts_with("#")
        if line:matches($/[?]/):
            section_name := line:replace($/[?]/, "\1"):trim():lower()
            current_section = @{:Text:Text}
            sections:set(section_name, current_section)
        else if line:matches($/{..}={..}/):
            key := line:replace($/{..}={..}/, "\1"):trim():lower()
            value := line:replace($/{..}={..}/, "\2"):trim()
            current_section:set(key, value)

    return {k:v[] for k,v in sections}

func main(path:Path, key:Text):
    keys := key:split($Pattern"/")
    if keys.length > 2:
        exit("
            Too many arguments! 
            $_USAGE
        ")

    if not path:is_file() or path:is_pipe():
        exit("Could not read file: $(path.text_content)")

    data := parse_ini(path)
    if keys.length < 1 or keys[1] == '*':
        !! $data
        return

    section := keys[1]:lower()
    if not data:has(section):
        exit("Invalid section name: $section; valid names: $(", ":join([k:quoted() for k in data.keys]))")

    section_data := data:get(section)
    if keys.length < 2 or keys[2] == '*':
        !! $section_data
        return

    section_key := keys[2]:lower()
    if not section_data:has(section_key):
        exit("Invalid key: $section_key; valid keys: $(", ":join(section_data.keys))")

    say(section_data:get(section_key))
