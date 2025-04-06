
use patterns

_USAGE := "
    Usage: ini <filename> "[section[/key]]"
"
_HELP := "
    ini: A .ini config file reader tool.
    $_USAGE
"

func parse_ini(path:Path -> {Text={Text=Text}})
    text := path.read() or exit("Could not read INI file: $\[31;1]$(path)$\[]")
    sections : @{Text=@{Text=Text}} = @{}
    current_section : @{Text=Text} = @{}

    # Line wraps:
    text = text.replace_pattern($Pat/\{1 nl}{0+space}/, " ")

    for line in text.lines()
        line = line.trim()
        skip if line.starts_with(";") or line.starts_with("#")
        if line.matches_pattern($Pat/[?]/)
            section_name := line.replace($Pat/[?]/, "\1").trim().lower()
            current_section = @{}
            sections[section_name] = current_section
        else if line.matches_pattern($Pat/{..}={..}/)
            key := line.replace_pattern($Pat/{..}={..}/, "\1").trim().lower()
            value := line.replace_pattern($Pat/{..}={..}/, "\2").trim()
            current_section[key] = value

    return {k=v[] for k,v in sections[]}

func main(path:Path, key:Text?)
    keys := (key or "").split($|/|)
    if keys.length > 2
        exit("
            Too many arguments! 
            $_USAGE
        ")

    data := parse_ini(path)
    if keys.length < 1 or keys[1] == '*'
        say("$data")
        return

    section := keys[1].lower()
    section_data := data[section] or exit("
        Invalid section name: $\[31;1]$section$\[]
        Valid names: $\[1]$(", ".join([k.quoted() for k in data.keys]))$\[]
    ")
    if keys.length < 2 or keys[2] == '*'
        say("$section_data")
        return

    section_key := keys[2].lower()
    value := section_data[section_key] or exit("
        Invalid key: $\[31;1]$section_key$\[]
        Valid keys: $\[1]$(", ".join([s.quoted() for s in section_data.keys]))$\[]
    ")
    say(value)
