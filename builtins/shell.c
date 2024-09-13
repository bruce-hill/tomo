// A lang for Shell Command Language
#include <stdbool.h>
#include <stdint.h>

#include "array.h"
#include "functions.h"
#include "integers.h"
#include "pattern.h"
#include "shell.h"
#include "text.h"
#include "types.h"
#include "util.h"

public Shell_t Shell$escape_text(Text_t text)
{
    // TODO: optimize for ASCII and short strings
    Array_t shell_graphemes = {.atomic=1};
#define add_char(c) Array$insert(&shell_graphemes, (uint32_t[1]){c}, I_small(0), sizeof(uint32_t))
    add_char('\'');
    const char *text_utf8 = Text$as_c_string(text);
    for (const char *p = text_utf8; *p; p++) {
        if (*p == '\'') {
            add_char('\'');
            add_char('"');
            add_char('\'');
            add_char('"');
            add_char('\'');
        } else
            add_char((uint8_t)*p);
    }
    add_char('\'');
#undef add_char
    return (Text_t){.length=shell_graphemes.length, .tag=TEXT_GRAPHEMES, .graphemes=shell_graphemes.data};
}

public Text_t Shell$run(Shell_t command, int32_t *status)
{
    const char *cmd_str = Text$as_c_string(command);
    FILE *prog = popen(cmd_str, "r");

    const int chunk_size = 256;
    char *buf = GC_MALLOC_ATOMIC(chunk_size);
    Text_t output = Text("");
    size_t just_read;
    do {
        just_read = fread(buf, sizeof(char), chunk_size, prog);
        if (just_read > 0) {
            output = Texts(output, Text$from_strn(buf, just_read));
            buf = GC_MALLOC_ATOMIC(chunk_size);
        }
    } while (just_read > 0);

    if (status)
        *status = WEXITSTATUS(pclose(prog));
    else
        pclose(prog);

    return Text$trim(output, Pattern("{1 nl}"), false, true);
}

public const TypeInfo Shell$info = {
    .size=sizeof(Shell_t),
    .align=__alignof__(Shell_t),
    .tag=TextInfo,
    .TextInfo={.lang="Shell"},
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
