// Built-in functions

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "datatypes/closure.h"
#include "datatypes/num.h"
#include "datatypes/typeinfo.h"

extern bool USE_COLOR;
extern Text_t TOMO_VERSION_TEXT;

void tomo_configure(void);
void tomo_init(void);
// Run on the way out, by atexit() and by the fatal-signal handler alike.
// This is how anything optional attaches itself to program exit without the
// runtime having to name it: `--instrument` builds register their profile
// report here (profiling.c), which is what keeps profiling.c out of the link
// for everyone else, since one direct reference would defeat --gc-sections.
// An #ifdef could not do that job: libtomo.a is compiled once and shipped, so
// it cannot know which programs will be instrumented.
void tomo_at_cleanup(Closure_t fn);
void tomo_cleanup(void);

__attribute__((nonnull)) void start_inspect(const char *filename, int64_t start, int64_t end);
void end_inspect(const void *expr, const TypeInfo_t *type);
#define inspect(type, expr, typeinfo, start, end)                                                                      \
    {                                                                                                                  \
        start_inspect(__SOURCE_FILE__, start, end);                                                                    \
        type _expr = expr;                                                                                             \
        end_inspect(&_expr, typeinfo);                                                                                 \
    }
#define inspect_void(expr, typeinfo, start, end)                                                                       \
    {                                                                                                                  \
        start_inspect(__SOURCE_FILE__, start, end);                                                                    \
        expr;                                                                                                          \
        end_inspect(NULL, typeinfo);                                                                                   \
    }

void say(Text_t text, bool newline);
Text_t ask(Text_t prompt, bool bold, bool force_tty);
_Noreturn void tomo_exit(Text_t text, int32_t status);

Closure_t spawn(Closure_t fn);
void sleep_seconds(Num_t seconds);
OptionalText_t getenv_text(Text_t name);
void setenv_text(Text_t name, Text_t value);
