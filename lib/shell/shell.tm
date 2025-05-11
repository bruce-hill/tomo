use commands_v1.0

lang Shell
    convert(text:Text -> Shell)
        return Shell.from_text("'" ++ text.replace($/'/, `'"'"'`) ++ "'")

    convert(texts:[Text] -> Shell)
        return Shell.from_text(" ".join([Shell(t).text for t in texts]))

    convert(path:Path -> Shell)
        return Shell(Text(path.expand_home()))

    convert(paths:[Path] -> Shell)
        return Shell.from_text(" ".join([Shell(Text(p)).text for p in paths]))

    convert(n:Int -> Shell) return Shell.from_text(Text(n))
    convert(n:Int64 -> Shell) return Shell.from_text(Text(n))
    convert(n:Int32 -> Shell) return Shell.from_text(Text(n))
    convert(n:Int16 -> Shell) return Shell.from_text(Text(n))
    convert(n:Int8 -> Shell) return Shell.from_text(Text(n))
    convert(n:Num -> Shell) return Shell.from_text(Text(n))
    convert(n:Num32 -> Shell) return Shell.from_text(Text(n))

    func command(shell:Shell -> Command)
        return Command("sh", ["-c", shell.text])

    func result(shell:Shell, input="", input_bytes:[Byte]=[] -> ProgramResult)
        return shell.command().result(input=input, input_bytes=input_bytes)

    func run(shell:Shell -> ExitType)
        return shell.command().run()

    func get_output(shell:Shell, input="", trim_newline=yes -> Text?)
        return shell.command().get_output(input=input, trim_newline=trim_newline)

    func get_output_bytes(shell:Shell, input="", input_bytes:[Byte]=[] -> [Byte]?)
        return shell.command().get_output_bytes(input=input, input_bytes=input_bytes)

    func by_line(shell:Shell -> func(->Text?)?)
        return shell.command().by_line()

func main(command:Shell)
    for line in command.by_line()!
        >> line
