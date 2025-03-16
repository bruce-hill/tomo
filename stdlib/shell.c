// A lang for Shell Command Language
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistr.h>

#include "arrays.h"
#include "integers.h"
#include "paths.h"
#include "patterns.h"
#include "shell.h"
#include "text.h"
#include "types.h"
#include "util.h"

public Shell_t Shell$escape_text(Text_t text)
{
    return Texts(Text("'"), Text$replace(text, Text("'"), Text("'\"'\"'"), Text(""), false), Text("'"));
}

public Shell_t Shell$escape_path(Path_t path)
{
    return Shell$escape_text(Path$as_text(&path, false, &Path$info));
}

public Shell_t Shell$escape_text_array(Array_t texts)
{
    Array_t all_escaped = {};
    for (int64_t i = 0; i < texts.length; i++) {
        Text_t raw = *(Text_t*)(texts.data + i*texts.stride);
        Text_t escaped = Shell$escape_text(raw);
        Array$insert(&all_escaped, &escaped, I(0), sizeof(Text_t));
    }
    return Text$join(Text(" "), all_escaped);
}

public OptionalArray_t Shell$run_bytes(Shell_t command)
{
    const char *cmd_str = Text$as_c_string(command);
    FILE *prog = popen(cmd_str, "r");
    if (!prog)
        return NONE_ARRAY;

    size_t capacity = 256, len = 0;
    char *content = GC_MALLOC_ATOMIC(capacity);
    char chunk[256];
    size_t just_read;
    do {
        just_read = fread(chunk, 1, sizeof(chunk), prog);
        if (just_read == 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            break;
        }

        if (len + (size_t)just_read >= capacity)
            content = GC_REALLOC(content, (capacity *= 2));

        memcpy(content + len, chunk, (size_t)just_read);
        len += (size_t)just_read;
    } while (just_read == sizeof(chunk));

    int status = pclose(prog);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return NONE_ARRAY;

    return (Array_t){.data=content, .atomic=1, .stride=1, .length=len};
}

public OptionalText_t Shell$run(Shell_t command)
{
    Array_t bytes = Shell$run_bytes(command);
    if (bytes.length < 0)
        return NONE_TEXT;

    if (bytes.length > 0 && *(char*)(bytes.data + (bytes.length-1)*bytes.stride) == '\n') {
        --bytes.length;
        if (bytes.length > 0 && *(char*)(bytes.data + (bytes.length-1)*bytes.stride) == '\r')
            --bytes.length;
    }
    return Text$from_bytes(bytes);
}

public OptionalInt32_t Shell$execute(Shell_t command)
{
    const char *cmd_str = Text$as_c_string(command);
    int status = system(cmd_str);
    if (WIFEXITED(status))
        return (OptionalInt32_t){.i=WEXITSTATUS(status)};
    else
        return (OptionalInt32_t){.is_none=true};
}

static void _line_reader_cleanup(FILE **f)
{
    if (f && *f) {
        pclose(*f);
        *f = NULL;
    }
}

static Text_t _next_line(FILE **f)
{
    if (!f || !*f) return NONE_TEXT;

    char *line = NULL;
    size_t size = 0;
    ssize_t len = getline(&line, &size, *f);
    if (len <= 0) {
        _line_reader_cleanup(f);
        return NONE_TEXT;
    }

    while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n'))
        --len;

    if (u8_check((uint8_t*)line, (size_t)len) != NULL)
        fail("Invalid UTF8!");

    Text_t line_text = Text$format("%.*s", len, line);
    free(line);
    return line_text;
}

public OptionalClosure_t Shell$by_line(Shell_t command)
{
    const char *cmd_str = Text$as_c_string(command);
    FILE *prog = popen(cmd_str, "r");
    if (!prog)
        return NONE_CLOSURE;

    FILE **wrapper = GC_MALLOC(sizeof(FILE*));
    *wrapper = prog;
    GC_register_finalizer(wrapper, (void*)_line_reader_cleanup, NULL, NULL, NULL);
    return (Closure_t){.fn=(void*)_next_line, .userdata=wrapper};
}

public const TypeInfo_t Shell$info = {
    .size=sizeof(Shell_t),
    .align=__alignof__(Shell_t),
    .tag=TextInfo,
    .TextInfo={.lang="Shell"},
    .metamethods=Text$metamethods,
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
