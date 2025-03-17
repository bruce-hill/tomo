# Functions for running system commands

use ./commands.c

extern run_command:func(exe:Text, args:[Text], env:{Text,Text}, input:[Byte], output:&[Byte], error:&[Byte] -> Int32)

enum ExitType(Exited(status:Int32), Signaled(signal:Int32), Failed)

struct ProgramResult(stdout:[Byte], stderr:[Byte], exit_type:ExitType):
    func or_fail(r:ProgramResult -> ProgramResult):
        when r.exit_type is Exited(status):
            if status == 0:
                return r
        else: fail("Program failed: $r")
        fail("Program failed: $r")

    func output_text(r:ProgramResult, trim_newline=yes -> Text?):
        when r.exit_type is Exited(status):
            if status == 0:
                if text := Text.from_bytes(r.stdout):
                    if trim_newline:
                        text = text:trim($/{1 nl}/, trim_left=no, trim_right=yes)
                    return text
        else: return none
        return none

    func error_text(r:ProgramResult -> Text?):
        when r.exit_type is Exited(status):
            if status == 0:
                return Text.from_bytes(r.stderr)
        else: return none
        return none

struct Command(command:Text, args=[:Text], env={:Text,Text}):
    func run(command:Command, input="", input_bytes=[:Byte] -> ProgramResult):
        if input.length > 0:
            (&input_bytes):insert_all(input:bytes())

        stdout := [:Byte]
        stderr := [:Byte]
        status := run_command(command.command, command.args, command.env, input_bytes, &stdout, &stderr)

        if inline C : Bool { WIFEXITED(_$status) }:
            return ProgramResult(stdout, stderr, ExitType.Exited(inline C : Int32 { WEXITSTATUS(_$status) }))

        if inline C : Bool { WIFSIGNALED(_$status) }:
            return ProgramResult(stdout, stderr, ExitType.Signaled(inline C : Int32 { WTERMSIG(_$status) }))

        return ProgramResult(stdout, stderr, ExitType.Failed)

    func get_output(command:Command, input="", trim_newline=yes -> Text?):
        return command:run(input=input):output_text(trim_newline=trim_newline)

    func get_output_bytes(command:Command, input="", input_bytes=[:Byte] -> [Byte]?):
        result := command:run(input=input, input_bytes=input_bytes)
        when result.exit_type is Exited(status):
            if status == 0: return result.stdout
            return none
        else: return none

func main(command:Text, args:[Text], input=""):
    cmd := Command(command, args)
    say(cmd:get_output(input=input)!)
