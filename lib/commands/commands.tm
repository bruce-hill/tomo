# Functions for running system commands

use ./commands.c
use -lunistring

extern run_command : func(exe:Text, args:[Text], env:{Text=Text}, input:[Byte]?, output:&[Byte]?, error:&[Byte]? -> Int32)
extern command_by_line : func(exe:Text, args:[Text], env:{Text=Text} -> func(->Text?)?)

enum ExitType(Exited(status:Int32), Signaled(signal:Int32), Failed)
    func succeeded(e:ExitType -> Bool)
        when e is Exited(status) return (status == 0)
        else return no

    func or_fail(e:ExitType, message:Text?=none)
        if not e.succeeded()
            fail(message or "Program failed: $e")

struct ProgramResult(output:[Byte], errors:[Byte], exit_type:ExitType)
    func or_fail(r:ProgramResult, message:Text?=none -> ProgramResult)
        when r.exit_type is Exited(status)
            if status == 0
                return r
        else fail(message or "Program failed: $r")
        fail(message or "Program failed: $r")

    func output_text(r:ProgramResult, trim_newline=yes -> Text?)
        when r.exit_type is Exited(status)
            if status == 0
                if text := Text.from_bytes(r.output)
                    if trim_newline
                        text = text.without_suffix("\n")
                    return text
        else return none
        return none

    func error_text(r:ProgramResult -> Text?)
        when r.exit_type is Exited(status)
            if status == 0
                return Text.from_bytes(r.errors)
        else return none
        return none

    func succeeded(r:ProgramResult -> Bool)
        when r.exit_type is Exited(status)
            return (status == 0)
        else
            return no

struct Command(command:Text, args:[Text]=[], env:{Text=Text}={})
    func from_path(path:Path, args:[Text]=[], env:{Text=Text}={} -> Command)
        return Command(Text(path), args, env)

    func result(command:Command, input="", input_bytes:[Byte]=[] -> ProgramResult)
        if input.length > 0
            (&input_bytes).insert_all(input.bytes())

        output : [Byte]
        errors : [Byte]
        status := run_command(command.command, command.args, command.env, input_bytes, &output, &errors)

        if C_code:Bool(WIFEXITED(_$status))
            return ProgramResult(output, errors, ExitType.Exited(C_code:Int32(WEXITSTATUS(_$status))))

        if C_code:Bool(WIFSIGNALED(_$status))
            return ProgramResult(output, errors, ExitType.Signaled(C_code:Int32(WTERMSIG(_$status))))

        return ProgramResult(output, errors, ExitType.Failed)

    func run(command:Command, -> ExitType)
        status := run_command(command.command, command.args, command.env, none, none, none)

        if C_code:Bool(WIFEXITED(_$status))
            return ExitType.Exited(C_code:Int32(WEXITSTATUS(_$status)))

        if C_code:Bool(WIFSIGNALED(_$status))
            return ExitType.Signaled(C_code:Int32(WTERMSIG(_$status)))

        return ExitType.Failed

    func get_output(command:Command, input="", trim_newline=yes -> Text?)
        return command.result(input=input).output_text(trim_newline=trim_newline)

    func get_output_bytes(command:Command, input="", input_bytes:[Byte]=[] -> [Byte]?)
        result := command.result(input=input, input_bytes=input_bytes)
        when result.exit_type is Exited(status)
            if status == 0 return result.output
            return none
        else return none

    func by_line(command:Command -> func(->Text?)?)
        return command_by_line(command.command, command.args, command.env)
