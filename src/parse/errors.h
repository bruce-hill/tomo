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
// Fail the parse: record the error if the caller wants it as a value, unwind if
// there's somewhere to unwind to, and only report it if neither happened --
// printing is the last resort, for when nothing else will handle this.
//
#define parser_err(ctx, err_start, err_end, ...)                                                                       \
    ({                                                                                                                 \
        parse_error_t _parse_err = {                                                                                   \
            .file = (ctx)->file,                                                                                       \
            .start = (err_start),                                                                                      \
            .end = (err_end),                                                                                          \
            .message = String(__VA_ARGS__),                                                                            \
        };                                                                                                             \
        if ((ctx)->error) *(ctx)->error = _parse_err;                                                                  \
        if ((ctx)->on_err) longjmp(*((ctx)->on_err), 1);                                                               \
        print_parse_error(_parse_err);                                                                                 \
        exit(1);                                                                                                       \
    })

//
// Expect a string (potentially after whitespace) and emit a parser error if it's not there
//
#define expect_str(ctx, start, pos, target, ...)                                                                       \
    ({                                                                                                                 \
        spaces(pos);                                                                                                   \
        if (!match(pos, target)) {                                                                                     \
            parser_err(ctx, start, *pos, __VA_ARGS__);                                                                 \
        }                                                                                                              \
        char _lastchar = target[strlen(target) - 1];                                                                   \
        if (isalpha(_lastchar) || isdigit(_lastchar) || _lastchar == '_') {                                            \
            if (is_xid_continue_next(*pos)) {                                                                          \
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
            parser_err(ctx, _start, _end, __VA_ARGS__);                                                                \
        }                                                                                                              \
    })

#define expect(ctx, start, pos, parser, ...)                                                                           \
    ({                                                                                                                 \
        const char **_pos = pos;                                                                                       \
        spaces(_pos);                                                                                                  \
        __typeof(parser(ctx, *_pos)) _result = parser(ctx, *_pos);                                                     \
        if (!_result) {                                                                                                \
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
