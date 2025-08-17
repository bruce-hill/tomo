# Base 64 encoding and decoding
use patterns

enum JSONDecodeResult(
    Success(json:JSON)
    Failure(reason:Text)
)
    func invalid(text:Text -> JSONDecodeResult)
        return Failure("Unrecognized JSON: $(text.quoted())")

extend Text
    func json_quoted(text:Text -> Text)
        return '"' ++ text.translate({
            "\\"="\\\\",
            '"'='\\"',
            "\f"="\\f",
            "\r"="\\r",
            "\n"="\\n",
            "\b"="\\b",
            "\t"="\\t",
        }) ++ '"'

enum JSON(
    Object(items:{Text=JSON})
    Array(items:[JSON])
    Boolean(value:Bool)
    String(text:Text)
    Number(n:Num)
    Null
)
    func encode(j:JSON -> Text)
        when j is Object(items)
            return "{" ++ ", ".join([
                '$(k.json_quoted()): $(v.encode())'
                for k,v in items
            ]) ++ "}"
        is Array(items)
            return "[" ++ ", ".join([item.encode() for item in items]) ++ "]"
        is Boolean(value)
            return (if value then "true" else "false")
        is String(text)
            return text.json_quoted()
        is Number(n)
            return "$n"
        is Null
            return "null"

    func pretty_print(j:JSON, max_line:Int=80, indent:Text="  ", current_indent:Text="" -> Text)
        inline := j.encode()
        if inline.length > max_line
            next_indent := current_indent ++ indent
            when j is Object(items)
                return "{\n$next_indent" ++ ",\n$next_indent".join([
                    '$(k.json_quoted()): $(v.pretty_print(max_line, indent, next_indent))'
                    for k,v in items
                ]) ++ "\n$current_indent}"
            is Array(items)
                return "[\n$next_indent" ++ ",\n$next_indent".join([item.pretty_print(max_line, indent, next_indent) for item in items]) ++ "\n$current_indent]"
            else pass

        return inline

    func parse_text(text:Text, remainder:&Text? = none -> JSONDecodeResult)
        if text.starts_with('"')
            string := ""
            pos := 2
            escapes := {"n"="\n", "t"="\t", "r"="\r", '"'='"', "\\"="\\", "/"="/", "b"="\b", "f"="\f"}
            while pos <= text.length
                c := text[pos]
                if c == '"'
                    if remainder
                        remainder[] = text.from(pos + 1)
                    return Success(JSON.String(string))

                if c == "\\"
                    stop if pos + 1 > text.length

                    if esc := escapes[text[pos+1]]
                        string ++= esc
                        pos += 2
                    else if m := text.matching_pattern($Pat/u{4 digit}/)
                        string ++= Text.from_codepoints([Int32.parse(m.captures[1])!])
                        pos += 1 + m.text.length
                    else
                        if remainder
                            remainder[] = text
                        return JSONDecodeResult.invalid(text)
                else    
                    string ++= c
                    pos += 1

        if remainder
            remainder[] = text
        return JSONDecodeResult.invalid(text)

    func parse(text:Text, remainder:&Text? = none, trailing_commas:Bool=no -> JSONDecodeResult)
        if text.starts_with("true", remainder)
            return Success(JSON.Boolean(yes))
        else if text.starts_with("false", remainder)
            return Success(JSON.Boolean(no))
        else if text.starts_with("null", remainder)
            return Success(JSON.Null)
        else if n := Num.parse(text, remainder)
            return Success(JSON.Number(n))
        else if text.starts_with('"')
            return JSON.parse_text(text, remainder)
        else if text.starts_with("[")
            elements : &[JSON]
            text = text.from(2).trim_pattern($Pat"{whitespace}", right=no)
            repeat
                when JSON.parse(text, &text) is Success(elem)
                    elements.insert(elem)
                else stop

                if delim := text.matching_pattern($Pat'{0+ ws},{0+ ws}')
                    text = text.from(delim.text.length + 1)
                else stop

            if trailing_commas
                if delim := text.matching_pattern($Pat'{0+ ws},{0+ ws}')
                    text = text.from(delim.text.length + 1)
                
            if terminator := text.matching_pattern($Pat'{0+ ws}]')
                if remainder
                    remainder[] = text.from(terminator.text.length + 1)
                return Success(JSON.Array(elements))
        else if text.starts_with("{")
            object : &{Text=JSON}
            text = text.from(2).trim_pattern($Pat"{whitespace}", right=no)
            repeat
                key_text := text
                when JSON.parse_text(text, &text) is Success(key)
                    if separator := text.matching_pattern($Pat'{0+ ws}:{0+ ws}')
                        text = text.from(separator.text.length + 1)
                    else
                        return JSONDecodeResult.invalid(text)

                    when JSON.parse(text, &text) is Success(value)
                        when key is String(str)
                            object[str] = value
                        else
                            return JSONDecodeResult.invalid(key_text)
                    else
                        return JSONDecodeResult.invalid(text)
                else stop

                if delim := text.matching_pattern($Pat'{0+ ws},{0+ ws}')
                    text = text.from(delim.text.length + 1)
                else stop

            if trailing_commas
                if delim := text.matching_pattern($Pat'{0+ ws},{0+ ws}')
                    text = text.from(delim.text.length + 1)
                
            if terminator := text.matching_pattern($Pat'{0+ ws}{}}')
                if remainder
                    remainder[] = text.from(terminator.text.length + 1)
                return Success(JSON.Object(object))

        return JSONDecodeResult.invalid(text)

func main(input=(/dev/stdin), pretty_print:Bool = no, trailing_commas:Bool = yes)
    text := (input.read() or exit("Invalid file: $input")).trim_pattern($Pat"{whitespace}")
    while text.length > 0
        when JSON.parse(text, remainder=&text, trailing_commas=trailing_commas) is Success(json)
            if pretty_print
                say(json.pretty_print())
            else
                say(json.encode())
        is Failure(msg)
            exit("\033[31;1m$msg\033[m", code=1)

        text = text.trim_pattern($Pat"{whitespace}")
