use <time.h>
use <stdio.h>

timestamp_format := CString("%F %T")

logfiles : @{Path} = @{}

func _timestamp(->Text):
    c_str := inline C:CString {
        char *str = GC_MALLOC_ATOMIC(20);
        time_t t; time(&t);
        struct tm *tm_info = localtime(&t);
        strftime(str, 20, "%F %T", tm_info);
        str
    }
    return c_str:as_text()

func info(text:Text, newline=yes):
    say("$\[2]⚫ $text$\[]", newline)
    for file in logfiles:
        file:append("$(_timestamp()) [info]  $text$\n")

func debug(text:Text, newline=yes):
    say("$\[32]🟢 $text$\[]", newline)
    for file in logfiles:
        file:append("$(_timestamp()) [debug] $text$\n")

func warn(text:Text, newline=yes):
    say("$\[33;1]🟡 $text$\[]", newline)
    for file in logfiles:
        file:append("$(_timestamp()) [warn]  $text$\n")

func error(text:Text, newline=yes):
    say("$\[31;1]🔴 $text$\[]", newline)
    for file in logfiles:
        file:append("$(_timestamp()) [error] $text$\n")

func add_logfile(file:Path):
    logfiles:add(file)

func remove_logfile(file:Path):
    logfiles:remove(file)

func main():
    add_logfile((./log.txt))
    >> info("Hello")
    >> debug("Hello")
    >> warn("Hello")
    >> error("Hello")

