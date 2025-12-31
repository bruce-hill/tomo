#pragma once

#include <ctype.h> // IWYU pragma: export
#include <stdio.h> // IWYU pragma: export
#include <stdlib.h> // IWYU pragma: export
#include <string.h> // IWYU pragma: export

#include "../stdlib/files.h" // IWYU pragma: export
#include "../stdlib/print.h" // IWYU pragma: export
#include "../stdlib/stacktrace.h" // IWYU pragma: export
#include "../stdlib/stdlib.h" // IWYU pragma: export
#include "context.h" // IWYU pragma: export
#include "utils.h" // IWYU pragma: export

//
// Print a parse error and exit (or use the on_err longjmp)
//
#define parser_err(ctx, start, end, ...)                                                                               \
    ({                                                                                                                 \
        if (USE_COLOR) fputs("\x1b[31;1;7m", stderr);                                                                  \
        fprint_inline(stderr, (ctx)->file->relative_filename, ":", get_line_number((ctx)->file, (start)), ".",         \
                      get_line_column((ctx)->file, (start)), ": ", __VA_ARGS__);                                       \
        if (USE_COLOR) fputs(" \x1b[m", stderr);                                                                       \
        fputs("\n\n", stderr);                                                                                         \
        highlight_error((ctx)->file, (start), (end), "\x1b[31;1;7m", 2, USE_COLOR);                                    \
        fputs("\n", stderr);                                                                                           \
        if (getenv("TOMO_STACKTRACE")) print_stacktrace(stderr, 1);                                                    \
        if ((ctx)->on_err) longjmp(*((ctx)->on_err), 1);                                                               \
        raise(SIGABRT);                                                                                                \
        exit(1);                                                                                                       \
    })

//
// Expect a string (potentially after whitespace) and emit a parser error if it's not there
//
#define expect_str(ctx, start, pos, target, ...)                                                                       \
    ({                                                                                                                 \
        spaces(pos);                                                                                                   \
        if (!match(pos, target)) {                                                                                     \
            if (USE_COLOR) fputs("\x1b[31;1;7m", stderr);                                                              \
            parser_err(ctx, start, *pos, __VA_ARGS__);                                                                 \
        }                                                                                                              \
        char _lastchar = target[strlen(target) - 1];                                                                   \
        if (isalpha(_lastchar) || isdigit(_lastchar) || _lastchar == '_') {                                            \
            if (is_xid_continue_next(*pos)) {                                                                          \
                if (USE_COLOR) fputs("\x1b[31;1;7m", stderr);                                                          \
                parser_err(ctx, start, *pos, __VA_ARGS__);                                                             \
            }                                                                                                          \
        }                                                                                                              \
    })

//
// Helper for matching closing parens with good error messages
//
#define expect_closing(ctx, pos, close_str, ...)                                                                       \
    ({                                                                                                                 \
        const char *_start = *pos;                                                                                     \
        spaces(pos);                                                                                                   \
        if (!match(pos, (close_str))) {                                                                                \
            const char *_eol = strchr(*pos, '\n');                                                                     \
            const char *_next = strstr(*pos, (close_str));                                                             \
            const char *_end = _eol < _next ? _eol : _next;                                                            \
            if (USE_COLOR) fputs("\x1b[31;1;7m", stderr);                                                              \
            parser_err(ctx, _start, _end, __VA_ARGS__);                                                                \
        }                                                                                                              \
    })

#define expect(ctx, start, pos, parser, ...)                                                                           \
    ({                                                                                                                 \
        const char **_pos = pos;                                                                                       \
        spaces(_pos);                                                                                                  \
        __typeof(parser(ctx, *_pos)) _result = parser(ctx, *_pos);                                                     \
        if (!_result) {                                                                                                \
            if (USE_COLOR) fputs("\x1b[31;1;7m", stderr);                                                              \
            parser_err(ctx, start, *_pos, __VA_ARGS__);                                                                \
        }                                                                                                              \
        *_pos = _result->end;                                                                                          \
        _result;                                                                                                       \
    })

#define optional(ctx, pos, parser)                                                                                     \
    ({                                                                                                                 \
        const char **_pos = pos;                                                                                       \
        spaces(_pos);                                                                                                  \
        __typeof(parser(ctx, *_pos)) _result = parser(ctx, *_pos);                                                     \
        if (_result) *_pos = _result->end;                                                                             \
        _result;                                                                                                       \
    })
