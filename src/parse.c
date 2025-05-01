// Recursive descent parser for parsing code
#include <ctype.h>
#include <gc.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

#ifndef __GLIBC__
#define __GLIBC__ 2
#include <unistr.h>
#undef __GLIBC__
#else
#include <unistr.h>
#endif

#include <unictype.h>
#include <uniname.h>
#include <signal.h>

#include "ast.h"
#include "cordhelpers.h"
#include "stdlib/integers.h"
#include "stdlib/paths.h"
#include "stdlib/print.h"
#include "stdlib/stacktrace.h"
#include "stdlib/stdlib.h"
#include "stdlib/tables.h"
#include "stdlib/text.h"
#include "stdlib/util.h"

// The cache of {filename -> parsed AST} will hold at most this many entries:
#ifndef PARSE_CACHE_SIZE
#define PARSE_CACHE_SIZE 100
#endif

static const double RADIANS_PER_DEGREE = 0.0174532925199432957692369076848861271344287188854172545609719144;
static const char closing[128] = {['(']=')', ['[']=']', ['<']='>', ['{']='}'};

typedef struct {
    file_t *file;
    jmp_buf *on_err;
    int64_t next_lambda_id;
} parse_ctx_t;

#define SPACES_PER_INDENT 4

#define PARSER(name) ast_t *name(parse_ctx_t *ctx, const char *pos)

int op_tightness[] = {
    [Power]=9,
    [Multiply]=8, [Divide]=8, [Mod]=8, [Mod1]=8,
    [Plus]=7, [Minus]=7,
    [Concat]=6,
    [LeftShift]=5, [RightShift]=5, [UnsignedLeftShift]=5, [UnsignedRightShift]=5,
    [Min]=4, [Max]=4,
    [Equals]=3, [NotEquals]=3,
    [LessThan]=2, [LessThanOrEquals]=2, [GreaterThan]=2, [GreaterThanOrEquals]=2,
    [Compare]=2,
    [And]=1, [Or]=1, [Xor]=1,
};

static const char *keywords[] = {
    "C_code", "_max_", "_min_", "and", "assert", "break", "continue", "defer", "deserialize", "do", "else",
    "enum", "extend", "extern", "for", "func", "if", "in", "lang", "mod", "mod1", "no", "none",
    "not", "or", "pass", "return", "skip", "skip", "stop", "struct", "then", "unless", "use", "when",
    "while", "xor", "yes",
};

enum {NORMAL_FUNCTION=0, EXTERN_FUNCTION=1};

static INLINE size_t some_of(const char **pos, const char *allow);
static INLINE size_t some_not(const char **pos, const char *forbid);
static INLINE size_t spaces(const char **pos);
static INLINE void whitespace(const char **pos);
static INLINE size_t match(const char **pos, const char *target);
static INLINE size_t match_word(const char **pos, const char *word);
static INLINE const char* get_word(const char **pos);
static INLINE const char* get_id(const char **pos);
static INLINE bool comment(const char **pos);
static INLINE bool indent(parse_ctx_t *ctx, const char **pos);
static INLINE ast_e match_binary_operator(const char **pos);
static ast_t *parse_comprehension_suffix(parse_ctx_t *ctx, ast_t *expr);
static ast_t *parse_field_suffix(parse_ctx_t *ctx, ast_t *lhs);
static ast_t *parse_fncall_suffix(parse_ctx_t *ctx, ast_t *fn);
static ast_t *parse_index_suffix(parse_ctx_t *ctx, ast_t *lhs);
static ast_t *parse_method_call_suffix(parse_ctx_t *ctx, ast_t *self);
static ast_t *parse_non_optional_suffix(parse_ctx_t *ctx, ast_t *lhs);
static ast_t *parse_optional_conditional_suffix(parse_ctx_t *ctx, ast_t *stmt);
static ast_t *parse_optional_suffix(parse_ctx_t *ctx, ast_t *lhs);
static arg_ast_t *parse_args(parse_ctx_t *ctx, const char **pos);
static type_ast_t *parse_list_type(parse_ctx_t *ctx, const char *pos);
static type_ast_t *parse_func_type(parse_ctx_t *ctx, const char *pos);
static type_ast_t *parse_non_optional_type(parse_ctx_t *ctx, const char *pos);
static type_ast_t *parse_pointer_type(parse_ctx_t *ctx, const char *pos);
static type_ast_t *parse_set_type(parse_ctx_t *ctx, const char *pos);
static type_ast_t *parse_table_type(parse_ctx_t *ctx, const char *pos);
static type_ast_t *parse_type(parse_ctx_t *ctx, const char *pos);
static type_ast_t *parse_type_name(parse_ctx_t *ctx, const char *pos);
static PARSER(parse_list);
static PARSER(parse_assignment);
static PARSER(parse_block);
static PARSER(parse_bool);
static PARSER(parse_convert_def);
static PARSER(parse_declaration);
static PARSER(parse_defer);
static PARSER(parse_do);
static PARSER(parse_doctest);
static PARSER(parse_assert);
static PARSER(parse_enum_def);
static PARSER(parse_expr);
static PARSER(parse_extended_expr);
static PARSER(parse_extern);
static PARSER(parse_file_body);
static PARSER(parse_for);
static PARSER(parse_func_def);
static PARSER(parse_heap_alloc);
static PARSER(parse_if);
static PARSER(parse_inline_c);
static PARSER(parse_int);
static PARSER(parse_lambda);
static PARSER(parse_lang_def);
static PARSER(parse_extend);
static PARSER(parse_namespace);
static PARSER(parse_negative);
static PARSER(parse_not);
static PARSER(parse_none);
static PARSER(parse_num);
static PARSER(parse_parens);
static PARSER(parse_pass);
static PARSER(parse_path);
static PARSER(parse_reduction);
static PARSER(parse_repeat);
static PARSER(parse_return);
static PARSER(parse_set);
static PARSER(parse_skip);
static PARSER(parse_stack_reference);
static PARSER(parse_statement);
static PARSER(parse_stop);
static PARSER(parse_struct_def);
static PARSER(parse_table);
static PARSER(parse_term);
static PARSER(parse_term_no_suffix);
static PARSER(parse_text);
static PARSER(parse_update);
static PARSER(parse_use);
static PARSER(parse_var);
static PARSER(parse_when);
static PARSER(parse_while);
static PARSER(parse_deserialize);
static ast_list_t *_parse_text_helper(parse_ctx_t *ctx, const char **out_pos, char open_quote, char close_quote, char open_interp, bool allow_escapes);

//
// Print a parse error and exit (or use the on_err longjmp)
//
#define parser_err(ctx, start, end, ...) ({ \
    if (USE_COLOR) \
        fputs("\x1b[31;1;7m", stderr); \
    fprint_inline(stderr, (ctx)->file->relative_filename, ":", get_line_number((ctx)->file, (start)), \
                  ".", get_line_column((ctx)->file, (start)), ": ", __VA_ARGS__); \
    if (USE_COLOR) \
        fputs(" \x1b[m", stderr); \
    fputs("\n\n", stderr); \
    highlight_error((ctx)->file, (start), (end), "\x1b[31;1;7m", 2, USE_COLOR); \
    fputs("\n", stderr); \
    if (getenv("TOMO_STACKTRACE")) \
        print_stacktrace(stderr, 1); \
    if ((ctx)->on_err) \
        longjmp(*((ctx)->on_err), 1); \
    raise(SIGABRT); \
    exit(1); \
})

//
// Expect a string (potentially after whitespace) and emit a parser error if it's not there
//
#define expect_str(ctx, start, pos, target, ...) ({ \
    spaces(pos); \
    if (!match(pos, target)) { \
        if (USE_COLOR) \
            fputs("\x1b[31;1;7m", stderr); \
        parser_err(ctx, start, *pos, __VA_ARGS__); \
    } \
    char _lastchar = target[strlen(target)-1]; \
    if (isalpha(_lastchar) || isdigit(_lastchar) || _lastchar == '_') { \
        if (is_xid_continue_next(*pos)) { \
            if (USE_COLOR) \
                fputs("\x1b[31;1;7m", stderr); \
            parser_err(ctx, start, *pos, __VA_ARGS__); \
        } \
    } \
})

//
// Helper for matching closing parens with good error messages
//
#define expect_closing(ctx, pos, close_str, ...) ({ \
    const char *_start = *pos; \
    spaces(pos); \
    if (!match(pos, (close_str))) { \
        const char *_eol = strchr(*pos, '\n'); \
        const char *_next = strstr(*pos, (close_str)); \
        const char *_end = _eol < _next ? _eol : _next; \
        if (USE_COLOR) \
            fputs("\x1b[31;1;7m", stderr); \
        parser_err(ctx, _start, _end, __VA_ARGS__); \
    } \
})

#define expect(ctx, start, pos, parser, ...) ({ \
    const char **_pos = pos; \
    spaces(_pos); \
    __typeof(parser(ctx, *_pos)) _result = parser(ctx, *_pos); \
    if (!_result) { \
        if (USE_COLOR) \
            fputs("\x1b[31;1;7m", stderr); \
        parser_err(ctx, start, *_pos, __VA_ARGS__); \
    } \
    *_pos = _result->end; \
    _result; })

#define optional(ctx, pos, parser) ({ \
    const char **_pos = pos; \
    spaces(_pos); \
    __typeof(parser(ctx, *_pos)) _result = parser(ctx, *_pos); \
    if (_result) *_pos = _result->end; \
    _result; })

//
// Convert an escape sequence like \n to a string
//
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstack-protector"
#endif
static const char *unescape(parse_ctx_t *ctx, const char **out) {
    const char **endpos = out;
    const char *escape = *out;
    static const char *unescapes[256] = {['a']="\a",['b']="\b",['e']="\x1b",['f']="\f",['n']="\n",['r']="\r",['t']="\t",['v']="\v",['_']=" "};
    assert(*escape == '\\');
    if (unescapes[(int)escape[1]]) {
        *endpos = escape + 2;
        return GC_strdup(unescapes[(int)escape[1]]);
    } else if (escape[1] == '[') {
        // ANSI Control Sequence Indicator: \033 [ ... m
        size_t len = strcspn(&escape[2], "\r\n]");
        if (escape[2+len] != ']')
            parser_err(ctx, escape, escape + 2 + len, "Missing closing ']'");
        *endpos = escape + 3 + len;
        return String("\033[", string_slice(&escape[2], len), "m");
    } else if (escape[1] == '{') {
        // Unicode codepoints by name
        size_t len = strcspn(&escape[2], "\r\n}");
        if (escape[2+len] != '}')
            parser_err(ctx, escape, escape + 2 + len, "Missing closing '}'");
        char name[len+1];
        memcpy(name, &escape[2], len);
        name[len] = '\0';

        if (name[0] == 'U') {
            for (char *p = &name[1]; *p; p++) {
                if (!isxdigit(*p)) goto look_up_unicode_name;
            }
            // Unicode codepoints by hex
            char *endptr = NULL;
            long codepoint = strtol(name+1, &endptr, 16);
            uint32_t ustr[2] = {codepoint, 0};
            size_t bufsize = 8;
            uint8_t buf[bufsize];
            (void)u32_to_u8(ustr, bufsize, buf, &bufsize);
            *endpos = escape + 3 + len;
            return GC_strndup((char*)buf, bufsize);
        }

      look_up_unicode_name:;

        uint32_t codepoint = unicode_name_character(name);
        if (codepoint == UNINAME_INVALID)
            parser_err(ctx, escape, escape + 3 + len,
                       "Invalid unicode codepoint name: ", quoted(name));
        *endpos = escape + 3 + len;
        char *str = GC_MALLOC_ATOMIC(16);
        size_t u8_len = 16;
        (void)u32_to_u8(&codepoint, 1, (uint8_t*)str, &u8_len);
        str[u8_len] = '\0';
        return str;
    } else if (escape[1] == 'x' && escape[2] && escape[3]) {
        // ASCII 2-digit hex
        char buf[] = {escape[2], escape[3], 0};
        char c = (char)strtol(buf, NULL, 16);
        *endpos = escape + 4;
        return GC_strndup(&c, 1);
    } else if ('0' <= escape[1] && escape[1] <= '7' && '0' <= escape[2] && escape[2] <= '7' && '0' <= escape[3] && escape[3] <= '7') {
        char buf[] = {escape[1], escape[2], escape[3], 0};
        char c = (char)strtol(buf, NULL, 8);
        *endpos = escape + 4;
        return GC_strndup(&c, 1);
    } else {
        *endpos = escape + 2;
        return GC_strndup(escape+1, 1);
    }
}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

// Indent is in number of spaces (assuming that \t is 4 spaces)
PUREFUNC static INLINE int64_t get_indent(parse_ctx_t *ctx, const char *pos)
{
    int64_t line_num = get_line_number(ctx->file, pos);
    const char *line = get_line(ctx->file, line_num);
    if (line == NULL) {
        return 0;
    } else if (*line == ' ') {
        int64_t spaces = (int64_t)strspn(line, " ");
        if (line[spaces] == '\t')
            parser_err(ctx, line + spaces, line + spaces + 1, "This is a tab following spaces, and you can't mix tabs and spaces");
        return spaces;
    } else if (*line == '\t') {
        int64_t indent = (int64_t)strspn(line, "\t");
        if (line[indent] == ' ')
            parser_err(ctx, line + indent, line + indent + 1, "This is a space following tabs, and you can't mix tabs and spaces");
        return indent * SPACES_PER_INDENT;
    } else {
        return 0;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////     Text-based parsing primitives     ///////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////
size_t some_of(const char **pos, const char *allow) {
    size_t len = strspn(*pos, allow);
    *pos += len;
    return len;
}

size_t some_not(const char **pos, const char *forbid) {
    size_t len = strcspn(*pos, forbid);
    *pos += len;
    return len;
}

size_t spaces(const char **pos) {
    return some_of(pos, " \t");
}

void whitespace(const char **pos) {
    while (some_of(pos, " \t\r\n") || comment(pos))
        continue;
}

size_t match(const char **pos, const char *target) {
    size_t len = strlen(target);
    if (strncmp(*pos, target, len) != 0)
        return 0;
    *pos += len;
    return len;
}

static INLINE bool is_xid_continue_next(const char *pos) {
    ucs4_t point = 0;
    u8_next(&point, (const uint8_t*)pos);
    return uc_is_property_xid_continue(point);
}

size_t match_word(const char **out, const char *word) {
    const char *pos = *out;
    spaces(&pos);
    if (!match(&pos, word) || is_xid_continue_next(pos))
        return 0;

    *out = pos;
    return strlen(word);
}

const char *get_word(const char **inout) {
    const char *word = *inout;
    spaces(&word);
    const uint8_t *pos = (const uint8_t*)word;
    ucs4_t point;
    pos = u8_next(&point, pos);
    if (!uc_is_property_xid_start(point) && point != '_')
        return NULL;

    for (const uint8_t *next; (next = u8_next(&point, pos)); pos = next) {
        if (!uc_is_property_xid_continue(point))
            break;
    }
    *inout = (const char*)pos;
    return GC_strndup(word, (size_t)((const char*)pos - word));
}

static CONSTFUNC bool is_keyword(const char *word) {
    int64_t lo = 0, hi = sizeof(keywords)/sizeof(keywords[0])-1;
    while (lo <= hi) {
        int64_t mid = (lo + hi) / 2;
        int32_t cmp = strcmp(word, keywords[mid]);
        if (cmp == 0)
            return true;
        else if (cmp > 0)
            lo = mid + 1;
        else if (cmp < 0)
            hi = mid - 1;
    }
    return false;
}

const char *get_id(const char **inout) {
    const char *pos = *inout;
    const char *word = get_word(&pos);
    if (!word || is_keyword(word))
        return NULL;
    *inout = pos;
    return word;
}

static const char *eol(const char *str) {
    return str + strcspn(str, "\r\n");
}

bool comment(const char **pos) {
    if ((*pos)[0] == '#') {
        *pos += strcspn(*pos, "\r\n");
        return true;
    } else {
        return false;
    }
}

bool indent(parse_ctx_t *ctx, const char **out) {
    const char *pos = *out;
    int64_t starting_indent = get_indent(ctx, pos);
    whitespace(&pos);
    const char *next_line = get_line(ctx->file, get_line_number(ctx->file, pos));
    if (next_line <= *out)
        return false;

    if (get_indent(ctx, next_line) != starting_indent + SPACES_PER_INDENT)
        return false;

    *out = next_line + strspn(next_line, " \t");
    return true;
}

bool newline_with_indentation(const char **out, int64_t target) {
    const char *pos = *out;
    if (*pos == '\r') ++pos;
    if (*pos != '\n') return false;
    ++pos;
    if (*pos == '\r' || *pos == '\n' || *pos == '\0') {
        // Empty line
        *out = pos;
        return true;
    }

    if (*pos == ' ') {
        if ((int64_t)strspn(pos, " ") >= target) {
            *out = pos + target;
            return true;
        }
    } else if ((int64_t)strspn(pos, "\t") * SPACES_PER_INDENT >= target) {
        *out = pos + target / SPACES_PER_INDENT;
        return true;
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////     AST-based parsers    /////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////

PARSER(parse_parens) {
    const char *start = pos;
    spaces(&pos);
    if (!match(&pos, "(")) return NULL;
    whitespace(&pos);
    ast_t *expr = optional(ctx, &pos, parse_extended_expr);
    if (!expr) return NULL;

    ast_t *comprehension = parse_comprehension_suffix(ctx, expr);
    while (comprehension) {
        expr = comprehension;
        pos = comprehension->end;
        comprehension = parse_comprehension_suffix(ctx, expr);
    }

    whitespace(&pos);
    expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this expression");

    // Update the span to include the parens:
    return new(ast_t, .file=(ctx)->file, .start=start, .end=pos,
               .tag=expr->tag, .__data=expr->__data);
}

PARSER(parse_int) {
    const char *start = pos;
    (void)match(&pos, "-");
    if (!isdigit(*pos)) return NULL;
    if (match(&pos, "0x")) { // Hex
        pos += strspn(pos, "0123456789abcdefABCDEF_");
    } else if (match(&pos, "0b")) { // Binary
        pos += strspn(pos, "01_");
    } else if (match(&pos, "0o")) { // Octal
        pos += strspn(pos, "01234567_");
    } else { // Decimal
        pos += strspn(pos, "0123456789_");
    }
    char *str = GC_MALLOC_ATOMIC((size_t)(pos - start) + 1);
    memset(str, 0, (size_t)(pos - start) + 1);
    for (char *src = (char*)start, *dest = str; src < pos; ++src) {
        if (*src != '_') *(dest++) = *src;
    }

    if (match(&pos, "e") || match(&pos, "f")) // floating point literal
        return NULL;

    if (match(&pos, "%")) {
        double n = strtod(str, NULL) / 100.;
        return NewAST(ctx->file, start, pos, Num, .n=n);
    } else if (match(&pos, "deg")) {
        double n = strtod(str, NULL) * RADIANS_PER_DEGREE;
        return NewAST(ctx->file, start, pos, Num, .n=n);
    }

    return NewAST(ctx->file, start, pos, Int, .str=str);
}

type_ast_t *parse_table_type(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match(&pos, "{")) return NULL;
    whitespace(&pos);
    type_ast_t *key_type = parse_type(ctx, pos);
    if (!key_type) return NULL;
    pos = key_type->end;
    whitespace(&pos);
    type_ast_t *value_type = NULL;
    if (match(&pos, "=")) {
        value_type = expect(ctx, start, &pos, parse_type, "I couldn't parse the rest of this table type");
    } else {
        return NULL;
    }
    spaces(&pos);
    ast_t *default_value = NULL;
    if (match(&pos, ";") && match_word(&pos, "default")) {
        expect_str(ctx, pos, &pos, "=", "I expected an '=' here");
        default_value = expect(ctx, start, &pos, parse_extended_expr, "I couldn't parse the default value for this table");
    }
    whitespace(&pos);
    expect_closing(ctx, &pos, "}", "I wasn't able to parse the rest of this table type");
    return NewTypeAST(ctx->file, start, pos, TableTypeAST, .key=key_type, .value=value_type, .default_value=default_value);
}

type_ast_t *parse_set_type(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match(&pos, "|")) return NULL;
    whitespace(&pos);
    type_ast_t *item_type = parse_type(ctx, pos);
    if (!item_type) return NULL;
    pos = item_type->end;
    whitespace(&pos);
    expect_closing(ctx, &pos, "|", "I wasn't able to parse the rest of this set type");
    return NewTypeAST(ctx->file, start, pos, SetTypeAST, .item=item_type);
}

type_ast_t *parse_func_type(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match_word(&pos, "func")) return NULL;
    spaces(&pos);
    expect_str(ctx, start, &pos, "(", "I expected a parenthesis here");
    arg_ast_t *args = parse_args(ctx, &pos);
    spaces(&pos);
    type_ast_t *ret = match(&pos, "->") ? optional(ctx, &pos, parse_type) : NULL;
    expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this function type");
    return NewTypeAST(ctx->file, start, pos, FunctionTypeAST, .args=args, .ret=ret);
}

type_ast_t *parse_list_type(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match(&pos, "[")) return NULL;
    type_ast_t *type = expect(ctx, start, &pos, parse_type,
                             "I couldn't parse a list item type after this point");
    expect_closing(ctx, &pos, "]", "I wasn't able to parse the rest of this list type");
    return NewTypeAST(ctx->file, start, pos, ListTypeAST, .item=type);
}

type_ast_t *parse_pointer_type(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    bool is_stack;
    if (match(&pos, "@"))
        is_stack = false;
    else if (match(&pos, "&"))
        is_stack = true;
    else
        return NULL;

    spaces(&pos);
    type_ast_t *type = expect(ctx, start, &pos, parse_non_optional_type,
                              "I couldn't parse a pointer type after this point");
    type_ast_t *ptr_type = NewTypeAST(ctx->file, start, pos, PointerTypeAST, .pointed=type, .is_stack=is_stack);
    spaces(&pos);
    while (match(&pos, "?"))
        ptr_type = NewTypeAST(ctx->file, start, pos, OptionalTypeAST, .type=ptr_type);
    return ptr_type;
}

type_ast_t *parse_type_name(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    const char *id = get_id(&pos);
    if (!id) return NULL;
    for (;;) {
        const char *next = pos;
        spaces(&next);
        if (!match(&next, ".")) break;
        const char *next_id = get_id(&next);
        if (!next_id) break;
        id = String(id, ".", next_id);
        pos = next;
    }
    return NewTypeAST(ctx->file, start, pos, VarTypeAST, .name=id);
}

type_ast_t *parse_non_optional_type(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    type_ast_t *type = NULL;
    bool success = (false
        || (type=parse_pointer_type(ctx, pos))
        || (type=parse_list_type(ctx, pos))
        || (type=parse_table_type(ctx, pos))
        || (type=parse_set_type(ctx, pos))
        || (type=parse_type_name(ctx, pos))
        || (type=parse_func_type(ctx, pos))
    );
    if (!success && match(&pos, "(")) {
        whitespace(&pos);
        type = optional(ctx, &pos, parse_type);
        if (!type) return NULL;
        whitespace(&pos);
        expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this type");
        type->start = start;
        type->end = pos;
    }

    return type;
}

type_ast_t *parse_type(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    type_ast_t *type = parse_non_optional_type(ctx, pos);
    if (!type) return NULL;
    pos = type->end;
    spaces(&pos);
    while (match(&pos, "?"))
        type = NewTypeAST(ctx->file, start, pos, OptionalTypeAST, .type=type);
    return type;
}

PARSER(parse_num) {
    const char *start = pos;
    bool negative = match(&pos, "-");
    if (!isdigit(*pos) && *pos != '.') return NULL;
    else if (*pos == '.' && !isdigit(pos[1])) return NULL;

    size_t len = strspn(pos, "0123456789_");
    if (strncmp(pos+len, "..", 2) == 0)
        return NULL;
    else if (pos[len] == '.')
        len += 1 + strspn(pos + len + 1, "0123456789");
    else if (pos[len] != 'e' && pos[len] != 'f' && pos[len] != '%')
        return NULL;
    if (pos[len] == 'e') {
        len += 1;
        if (pos[len] == '-')
            len += 1;
        len += strspn(pos + len, "0123456789_");
    }
    char *buf = GC_MALLOC_ATOMIC(len+1);
    memset(buf, 0, len+1);
    for (char *src = (char*)pos, *dest = buf; src < pos+len; ++src) {
        if (*src != '_') *(dest++) = *src;
    }
    double d = strtod(buf, NULL);
    pos += len;

    if (negative) d *= -1;

    if (match(&pos, "%"))
        d /= 100.;
    else if (match(&pos, "deg"))
        d *= RADIANS_PER_DEGREE;

    return NewAST(ctx->file, start, pos, Num, .n=d);
}

static INLINE bool match_separator(const char **pos) { // Either comma or newline
    const char *p = *pos;
    int separators = 0;
    for (;;) {
        if (some_of(&p, "\r\n,"))
            ++separators;
        else if (!comment(&p) && !some_of(&p, " \t"))
            break;
    }
    if (separators > 0) {
        *pos = p;
        return true;
    } else {
        return false;
    }
}

PARSER(parse_list) {
    const char *start = pos;
    if (!match(&pos, "[")) return NULL;

    whitespace(&pos);

    ast_list_t *items = NULL;
    for (;;) {
        ast_t *item = optional(ctx, &pos, parse_extended_expr);
        if (!item) break;
        ast_t *suffixed = parse_comprehension_suffix(ctx, item);
        while (suffixed) {
            item = suffixed;
            pos = suffixed->end;
            suffixed = parse_comprehension_suffix(ctx, item);
        }
        items = new(ast_list_t, .ast=item, .next=items);
        if (!match_separator(&pos))
            break;
    }
    whitespace(&pos);
    expect_closing(ctx, &pos, "]", "I wasn't able to parse the rest of this list");

    REVERSE_LIST(items);
    return NewAST(ctx->file, start, pos, List, .items=items);
}

PARSER(parse_table) {
    const char *start = pos;
    if (!match(&pos, "{")) return NULL;

    whitespace(&pos);

    ast_list_t *entries = NULL;
    for (;;) {
        const char *entry_start = pos;
        ast_t *key = optional(ctx, &pos, parse_extended_expr);
        if (!key) break;
        whitespace(&pos);
        if (!match(&pos, "=")) return NULL;
        ast_t *value = expect(ctx, pos-1, &pos, parse_expr, "I couldn't parse the value for this table entry");
        ast_t *entry = NewAST(ctx->file, entry_start, pos, TableEntry, .key=key, .value=value);
        ast_t *suffixed = parse_comprehension_suffix(ctx, entry);
        while (suffixed) {
            entry = suffixed;
            pos = suffixed->end;
            suffixed = parse_comprehension_suffix(ctx, entry);
        }
        entries = new(ast_list_t, .ast=entry, .next=entries);
        if (!match_separator(&pos))
            break;
    }

    REVERSE_LIST(entries);

    whitespace(&pos);

    ast_t *fallback = NULL, *default_value = NULL;
    if (match(&pos, ";")) {
        for (;;) {
            whitespace(&pos);
            const char *attr_start = pos;
            if (match_word(&pos, "fallback")) {
                whitespace(&pos);
                if (!match(&pos, "=")) parser_err(ctx, attr_start, pos, "I expected an '=' after 'fallback'");
                if (fallback)
                    parser_err(ctx, attr_start, pos, "This table already has a fallback");
                fallback = expect(ctx, attr_start, &pos, parse_expr, "I expected a fallback table");
            } else if (match_word(&pos, "default")) {
                whitespace(&pos);
                if (!match(&pos, "=")) parser_err(ctx, attr_start, pos, "I expected an '=' after 'default'");
                if (default_value)
                    parser_err(ctx, attr_start, pos, "This table already has a default");
                default_value = expect(ctx, attr_start, &pos, parse_expr, "I expected a default value");
            } else {
                break;
            }
            whitespace(&pos);
            if (!match(&pos, ",")) break;
        }
    }

    whitespace(&pos);
    expect_closing(ctx, &pos, "}", "I wasn't able to parse the rest of this table");

    return NewAST(ctx->file, start, pos, Table, .default_value=default_value, .entries=entries, .fallback=fallback);
}

PARSER(parse_set) {
    const char *start = pos;
    if (match(&pos, "||"))
        return NewAST(ctx->file, start, pos, Set);

    if (!match(&pos, "|")) return NULL;

    whitespace(&pos);

    ast_list_t *items = NULL;
    for (;;) {
        ast_t *item = optional(ctx, &pos, parse_extended_expr);
        if (!item) break;
        whitespace(&pos);
        ast_t *suffixed = parse_comprehension_suffix(ctx, item);
        while (suffixed) {
            item = suffixed;
            pos = suffixed->end;
            suffixed = parse_comprehension_suffix(ctx, item);
        }
        items = new(ast_list_t, .ast=item, .next=items);
        if (!match_separator(&pos))
            break;
    }

    REVERSE_LIST(items);

    whitespace(&pos);
    expect_closing(ctx, &pos, "|", "I wasn't able to parse the rest of this set");

    return NewAST(ctx->file, start, pos, Set, .items=items);
}

ast_t *parse_field_suffix(parse_ctx_t *ctx, ast_t *lhs) {
    if (!lhs) return NULL;
    const char *pos = lhs->end;
    whitespace(&pos);
    if (!match(&pos, ".")) return NULL;
    if (*pos == '.') return NULL;
    whitespace(&pos);
    bool dollar = match(&pos, "$");
    const char* field = get_id(&pos);
    if (!field) return NULL;
    if (dollar) field = String("$", field);
    return NewAST(ctx->file, lhs->start, pos, FieldAccess, .fielded=lhs, .field=field);
}

ast_t *parse_optional_suffix(parse_ctx_t *ctx, ast_t *lhs) {
    if (!lhs) return NULL;
    const char *pos = lhs->end;
    if (match(&pos, "?"))
        return NewAST(ctx->file, lhs->start, pos, Optional, .value=lhs);
    else
        return NULL;
}

ast_t *parse_non_optional_suffix(parse_ctx_t *ctx, ast_t *lhs) {
    if (!lhs) return NULL;
    const char *pos = lhs->end;
    if (match(&pos, "!"))
        return NewAST(ctx->file, lhs->start, pos, NonOptional, .value=lhs);
    else
        return NULL;
}

PARSER(parse_reduction) {
    const char *start = pos;
    if (!match(&pos, "(")) return NULL;
    
    whitespace(&pos);
    ast_e op = match_binary_operator(&pos);
    if (op == Unknown) return NULL;

    ast_t *key = NewAST(ctx->file, pos, pos, Var, .name="$");
    for (bool progress = true; progress; ) {
        ast_t *new_term;
        progress = (false
            || (new_term=parse_index_suffix(ctx, key))
            || (new_term=parse_method_call_suffix(ctx, key))
            || (new_term=parse_field_suffix(ctx, key))
            || (new_term=parse_fncall_suffix(ctx, key))
            || (new_term=parse_optional_suffix(ctx, key))
            || (new_term=parse_non_optional_suffix(ctx, key))
            );
        if (progress) key = new_term;
    }
    if (key && key->tag == Var) key = NULL;
    else if (key) pos = key->end;

    whitespace(&pos);
    if (!match(&pos, ":")) return NULL;

    ast_t *iter = optional(ctx, &pos, parse_extended_expr);
    if (!iter) return NULL;
    ast_t *suffixed = parse_comprehension_suffix(ctx, iter);
    while (suffixed) {
        iter = suffixed;
        pos = suffixed->end;
        suffixed = parse_comprehension_suffix(ctx, iter);
    }

    whitespace(&pos);
    expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this reduction");

    return NewAST(ctx->file, start, pos, Reduction, .iter=iter, .op=op, .key=key);
}

ast_t *parse_index_suffix(parse_ctx_t *ctx, ast_t *lhs) {
    if (!lhs) return NULL;
    const char *start = lhs->start;
    const char *pos = lhs->end;
    if (!match(&pos, "[")) return NULL;
    whitespace(&pos);
    ast_t *index = optional(ctx, &pos, parse_extended_expr);
    whitespace(&pos);
    bool unchecked = match(&pos, ";") && (spaces(&pos), match_word(&pos, "unchecked") != 0);
    expect_closing(ctx, &pos, "]", "I wasn't able to parse the rest of this index");
    return NewAST(ctx->file, start, pos, Index, .indexed=lhs, .index=index, .unchecked=unchecked);
}

ast_t *parse_comprehension_suffix(parse_ctx_t *ctx, ast_t *expr) {
    // <expr> "for" [<index>,]<var> "in" <iter> ["if" <condition> | "unless" <condition>]
    if (!expr) return NULL;
    const char *start = expr->start;
    const char *pos = expr->end;
    whitespace(&pos);
    if (!match_word(&pos, "for")) return NULL;

    ast_list_t *vars = NULL;
    for (;;) {
        ast_t *var = optional(ctx, &pos, parse_var);
        if (var)
            vars = new(ast_list_t, .ast=var, .next=vars);

        spaces(&pos);
        if (!match(&pos, ","))
            break;
    }
    REVERSE_LIST(vars);

    expect_str(ctx, start, &pos, "in", "I expected an 'in' for this 'for'");
    ast_t *iter = expect(ctx, start, &pos, parse_expr, "I expected an iterable value for this 'for'");
    const char *next_pos = pos;
    whitespace(&next_pos);
    ast_t *filter = NULL;
    if (match_word(&next_pos, "if")) {
        pos = next_pos;
        filter = expect(ctx, pos-2, &pos, parse_expr, "I expected a condition for this 'if'");
    } else if (match_word(&next_pos, "unless")) {
        pos = next_pos;
        filter = expect(ctx, pos-2, &pos, parse_expr, "I expected a condition for this 'unless'");
        filter = WrapAST(filter, Not, filter);
    }
    return NewAST(ctx->file, start, pos, Comprehension, .expr=expr, .vars=vars, .iter=iter, .filter=filter);
}

ast_t *parse_optional_conditional_suffix(parse_ctx_t *ctx, ast_t *stmt) {
    // <statement> "if" <condition> | <statement> "unless" <condition>
    if (!stmt) return stmt;
    const char *start = stmt->start;
    const char *pos = stmt->end;
    if (match_word(&pos, "if")) {
        ast_t *condition = expect(ctx, pos-2, &pos, parse_expr, "I expected a condition for this 'if'");
        return NewAST(ctx->file, start, pos, If, .condition=condition, .body=stmt);
    } else if (match_word(&pos, "unless")) {
        ast_t *condition = expect(ctx, pos-2, &pos, parse_expr, "I expected a condition for this 'unless'");
        condition = WrapAST(condition, Not, condition);
        return NewAST(ctx->file, start, pos, If, .condition=condition, .body=stmt);
    } else {
        return stmt;
    }
}

PARSER(parse_if) {
    // "if" <condition> ["then"] <body> ["else" <body>] | "unless" <condition> <body> ["else" <body>]
    const char *start = pos;
    int64_t starting_indent = get_indent(ctx, pos);

    bool unless;
    if (match_word(&pos, "if"))
        unless = false;
    else if (match_word(&pos, "unless"))
        unless = true;
    else
        return NULL;

    ast_t *condition = unless ? NULL : optional(ctx, &pos, parse_declaration);
    if (!condition)
        condition = expect(ctx, start, &pos, parse_expr, "I expected to find a condition for this 'if'");

    if (unless)
        condition = WrapAST(condition, Not, condition);

    (void)match_word(&pos, "then"); // Optional 'then'
    ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a body for this 'if' statement"); 

    const char *tmp = pos;
    whitespace(&tmp);
    ast_t *else_body = NULL;
    const char *else_start = pos;
    if (get_indent(ctx, tmp) == starting_indent && match_word(&tmp, "else")) {
        pos = tmp;
        spaces(&pos);
        else_body = optional(ctx, &pos, parse_if);
        if (!else_body)
            else_body = expect(ctx, else_start, &pos, parse_block, "I expected a body for this 'else'"); 
    }
    return NewAST(ctx->file, start, pos, If, .condition=condition, .body=body, .else_body=else_body);
}

PARSER(parse_when) {
    // when <expr> (is var : Tag <body>)* [else <body>]
    const char *start = pos;
    int64_t starting_indent = get_indent(ctx, pos);

    if (!match_word(&pos, "when"))
        return NULL;

    ast_t *subject = optional(ctx, &pos, parse_declaration);
    if (!subject) subject = expect(ctx, start, &pos, parse_expr,
                                   "I expected to find an expression for this 'when'");

    when_clause_t *clauses = NULL;
    const char *tmp = pos;
    whitespace(&tmp);
    while (get_indent(ctx, tmp) == starting_indent && match_word(&tmp, "is")) {
        pos = tmp;
        spaces(&pos);
        ast_t *pattern = expect(ctx, start, &pos, parse_expr, "I expected a pattern to match here");
        spaces(&pos);
        when_clause_t *new_clauses = new(when_clause_t, .pattern=pattern, .next=clauses);
        while (match(&pos, ",")) {
            pattern = expect(ctx, start, &pos, parse_expr, "I expected a pattern to match here");
            new_clauses = new(when_clause_t, .pattern=pattern, .next=new_clauses);
            spaces(&pos);
        }
        (void)match_word(&pos, "then"); // Optional 'then'
        ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a body for this 'when' clause"); 
        for (when_clause_t *c = new_clauses; c && c != clauses; c = c->next) {
            c->body = body;
        }
        clauses = new_clauses;
        tmp = pos;
        whitespace(&tmp);
    }
    REVERSE_LIST(clauses);

    ast_t *else_body = NULL;
    const char *else_start = pos;
    if (get_indent(ctx, tmp) == starting_indent && match_word(&tmp, "else")) {
        pos = tmp;
        else_body = expect(ctx, else_start, &pos, parse_block, "I expected a body for this 'else'"); 
    }
    return NewAST(ctx->file, start, pos, When, .subject=subject, .clauses=clauses, .else_body=else_body);
}

PARSER(parse_for) {
    // for [k,] v in iter [<indent>] body
    const char *start = pos;
    if (!match_word(&pos, "for")) return NULL;
    int64_t starting_indent = get_indent(ctx, pos);
    spaces(&pos);
    ast_list_t *vars = NULL;
    for (;;) {
        ast_t *var = optional(ctx, &pos, parse_var);
        if (var)
            vars = new(ast_list_t, .ast=var, .next=vars);

        spaces(&pos);
        if (!match(&pos, ","))
            break;
    }

    spaces(&pos);
    expect_str(ctx, start, &pos, "in", "I expected an 'in' for this 'for'");

    ast_t *iter = expect(ctx, start, &pos, parse_expr, "I expected an iterable value for this 'for'");

    (void)match_word(&pos, "do"); // Optional 'do'

    ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a body for this 'for'"); 

    const char *else_start = pos;
    whitespace(&else_start);
    ast_t *empty = NULL;
    if (match_word(&else_start, "else") && get_indent(ctx, else_start) == starting_indent) {
        pos = else_start;
        empty = expect(ctx, pos, &pos, parse_block, "I expected a body for this 'else'");
    }
    REVERSE_LIST(vars);
    return NewAST(ctx->file, start, pos, For, .vars=vars, .iter=iter, .body=body, .empty=empty);
}

PARSER(parse_do) {
    // do [<indent>] body
    const char *start = pos;
    if (!match_word(&pos, "do")) return NULL;
    ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a body for this 'do'"); 
    return NewAST(ctx->file, start, pos, Block, .statements=Match(body, Block)->statements);
}

PARSER(parse_while) {
    // while condition ["do"] [<indent>] body
    const char *start = pos;
    if (!match_word(&pos, "while")) return NULL;

    const char *tmp = pos;
    // Shorthand form: `while when ...`
    if (match_word(&tmp, "when")) {
        ast_t *when = expect(ctx, start, &pos, parse_when, "I expected a 'when' block after this");
        if (!when->__data.When.else_body) when->__data.When.else_body = NewAST(ctx->file, pos, pos, Stop);
        return NewAST(ctx->file, start, pos, While, .body=when);
    }

    (void)match_word(&pos, "do"); // Optional 'do'

    ast_t *condition = expect(ctx, start, &pos, parse_expr, "I don't see a viable condition for this 'while'");
    ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a body for this 'while'"); 
    return NewAST(ctx->file, start, pos, While, .condition=condition, .body=body);
}

PARSER(parse_repeat) {
    // repeat [<indent>] body
    const char *start = pos;
    if (!match_word(&pos, "repeat")) return NULL;
    ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a body for this 'repeat'"); 
    return NewAST(ctx->file, start, pos, Repeat, .body=body);
}

PARSER(parse_heap_alloc) {
    const char *start = pos;
    if (!match(&pos, "@")) return NULL;
    spaces(&pos);
    ast_t *val = expect(ctx, start, &pos, parse_term_no_suffix, "I expected an expression for this '@'");

    for (;;) {
        ast_t *new_term;
        if ((new_term=parse_index_suffix(ctx, val))
            || (new_term=parse_fncall_suffix(ctx, val))
            || (new_term=parse_method_call_suffix(ctx, val))
            || (new_term=parse_field_suffix(ctx, val))) {
            val = new_term;
        } else break;
    }
    pos = val->end;

    ast_t *ast = NewAST(ctx->file, start, pos, HeapAllocate, .value=val);
    for (;;) {
        ast_t *next = parse_optional_suffix(ctx, ast);
        if (!next) next = parse_non_optional_suffix(ctx, ast);
        if (!next) break;
        ast = next;
    }
    return ast;
}

PARSER(parse_stack_reference) {
    const char *start = pos;
    if (!match(&pos, "&")) return NULL;
    spaces(&pos);
    ast_t *val = expect(ctx, start, &pos, parse_term_no_suffix, "I expected an expression for this '&'");

    for (;;) {
        ast_t *new_term;
        if ((new_term=parse_index_suffix(ctx, val))
            || (new_term=parse_fncall_suffix(ctx, val))
            || (new_term=parse_method_call_suffix(ctx, val))
            || (new_term=parse_field_suffix(ctx, val))) {
            val = new_term;
        } else break;
    }
    pos = val->end;

    ast_t *ast = NewAST(ctx->file, start, pos, StackReference, .value=val);
    for (;;) {
        ast_t *next = parse_optional_suffix(ctx, ast);
        if (!next) next = parse_non_optional_suffix(ctx, ast);
        if (!next) break;
        ast = next;
    }
    return ast;
}

PARSER(parse_not) {
    const char *start = pos;
    if (!match_word(&pos, "not")) return NULL;
    spaces(&pos);
    ast_t *val = expect(ctx, start, &pos, parse_term, "I expected an expression for this 'not'");
    return NewAST(ctx->file, start, pos, Not, .value=val);
}

PARSER(parse_negative) {
    const char *start = pos;
    if (!match(&pos, "-")) return NULL;
    spaces(&pos);
    ast_t *val = expect(ctx, start, &pos, parse_term, "I expected an expression for this '-'");
    return NewAST(ctx->file, start, pos, Negative, .value=val);
}

PARSER(parse_bool) {
    const char *start = pos;
    if (match_word(&pos, "yes"))
        return NewAST(ctx->file, start, pos, Bool, .b=true);
    else if (match_word(&pos, "no"))
        return NewAST(ctx->file, start, pos, Bool, .b=false);
    else
        return NULL;
}

ast_list_t *_parse_text_helper(parse_ctx_t *ctx, const char **out_pos, char open_quote, char close_quote, char open_interp, bool allow_escapes)
{
    const char *pos = *out_pos;
    int64_t starting_indent = get_indent(ctx, pos);
    int64_t string_indent = starting_indent + SPACES_PER_INDENT;
    ast_list_t *chunks = NULL;
    CORD chunk = CORD_EMPTY;
    const char *chunk_start = pos;
    int depth = 1;
    bool leading_newline = false;
    for (; pos < ctx->file->text + ctx->file->len && depth > 0; ) {
        if (*pos == open_interp) { // Interpolation
            const char *interp_start = pos;
            if (chunk) {
                ast_t *literal = NewAST(ctx->file, chunk_start, pos, TextLiteral, .cord=chunk);
                chunks = new(ast_list_t, .ast=literal, .next=chunks);
                chunk = CORD_EMPTY;
            }
            ++pos;
            ast_t *interp;
            if (*pos == ' ' || *pos == '\t')
                parser_err(ctx, pos, pos+1, "Whitespace is not allowed before an interpolation here");
            interp = expect(ctx, interp_start, &pos, parse_term_no_suffix, "I expected an interpolation term here");
            chunks = new(ast_list_t, .ast=interp, .next=chunks);
            chunk_start = pos;
        } else if (allow_escapes && *pos == '\\') {
            const char *c = unescape(ctx, &pos);
            chunk = CORD_cat(chunk, c);
        } else if (!leading_newline && *pos == open_quote && closing[(int)open_quote]) { // Nested pair begin
            if (get_indent(ctx, pos) == starting_indent) {
                ++depth;
            }
            chunk = CORD_cat_char(chunk, *pos);
            ++pos;
        } else if (!leading_newline && *pos == close_quote) { // Nested pair end
            if (get_indent(ctx, pos) == starting_indent) {
                --depth;
                if (depth == 0)
                    break;
            }
            chunk = CORD_cat_char(chunk, *pos);
            ++pos;
        } else if (newline_with_indentation(&pos, string_indent)) { // Newline
            if (!leading_newline && !(chunk || chunks)) {
                leading_newline = true;
            } else {
                chunk = CORD_cat_char(chunk, '\n');
            }
        } else if (newline_with_indentation(&pos, starting_indent)) { // Line continuation (..)
            if (*pos == close_quote) {
                break;
            } else if (some_of(&pos, ".") >= 2) {
                // Multi-line split
                continue;
            } else {
                parser_err(ctx, pos, eol(pos), "This multi-line string should be either indented or have '..' at the front");
            }
        } else { // Plain character
            chunk = CORD_cat_char(chunk, *pos);
            ++pos;
        }
    }

    if (chunk) {
        ast_t *literal = NewAST(ctx->file, chunk_start, pos, TextLiteral, .cord=chunk);
        chunks = new(ast_list_t, .ast=literal, .next=chunks);
        chunk = NULL;
    }

    REVERSE_LIST(chunks);
    char close_str[2] = {close_quote, 0};
    expect_closing(ctx, &pos, close_str, "I was expecting a ", close_quote, " to finish this string");
    *out_pos = pos;
    return chunks;
}

PARSER(parse_text) {
    // ('"' ... '"' / "'" ... "'" / "`" ... "`")
    // "$" [name] [interp-char] quote-char ... close-quote
    const char *start = pos;
    const char *lang = NULL;

    char open_quote, close_quote, open_interp = '$';
    if (match(&pos, "\"")) { // Double quote
        open_quote = '"', close_quote = '"', open_interp = '$';
    } else if (match(&pos, "`")) { // Backtick
        open_quote = '`', close_quote = '`', open_interp = '$';
    } else if (match(&pos, "'")) { // Single quote
        open_quote = '\'', close_quote = '\'', open_interp = '$';
    } else if (match(&pos, "$")) { // Customized strings
        lang = get_id(&pos);
        // $"..." or $@"...."
        static const char *interp_chars = "~!@#$%^&*+=\\?";
        if (match(&pos, "$")) { // Disable interpolation with $$
            open_interp = '\x03';
        } else if (strchr(interp_chars, *pos)) {
            open_interp = *pos;
            ++pos;
        }
        static const char *quote_chars = "\"'`|/;([{<";
        if (!strchr(quote_chars, *pos))
            parser_err(ctx, pos, pos+1, "This is not a valid string quotation character. Valid characters are: \"'`|/;([{<");
        open_quote = *pos;
        ++pos;
        close_quote = closing[(int)open_quote] ? closing[(int)open_quote] : open_quote;
    } else {
        return NULL;
    }

    bool allow_escapes = (open_quote != '`');
    ast_list_t *chunks = _parse_text_helper(ctx, &pos, open_quote, close_quote, open_interp, allow_escapes);
    bool colorize = match(&pos, "~") && match_word(&pos, "colorized");
    return NewAST(ctx->file, start, pos, TextJoin, .lang=lang, .children=chunks, .colorize=colorize);
}

PARSER(parse_path) {
    // "(" ("~/" / "./" / "../" / "/") ... ")"
    const char *start = pos;

    if (!match(&pos, "("))
        return NULL;

    if (!(*pos == '~' || *pos == '.' || *pos == '/'))
        return NULL;

    const char *path_start = pos;
    size_t len = 1;
    int paren_depth = 1;
    while (pos + len < ctx->file->text + ctx->file->len - 1) {
        if (pos[len] == '\\') {
            len += 2;
            continue;
        } else if (pos[len] == '(') {
            paren_depth += 1;
        } else if (pos[len] == ')') {
            paren_depth -= 1;
            if (paren_depth <= 0) break;
        } else if (pos[len] == '\r' || pos[len] == '\n') {
            parser_err(ctx, path_start, &pos[len-1], "This path was not closed");
        }
        len += 1;
    }
    pos += len + 1;
    char *path = String(string_slice(path_start, .length=len));
    for (char *src = path, *dest = path; ; ) {
        if (src[0] == '\\') {
            *(dest++) = src[1];
            src += 2;
        } else if (*src) {
            *(dest++) = *(src++);
        } else {
            *(dest++) = '\0';
            break;
        }
    }
    return NewAST(ctx->file, start, pos, Path, .path=path);
}

PARSER(parse_pass) {
    const char *start = pos;
    return match_word(&pos, "pass") ? NewAST(ctx->file, start, pos, Pass) : NULL;
}

PARSER(parse_defer) {
    const char *start = pos;
    if (!match_word(&pos, "defer")) return NULL;
    ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a block to be deferred here");
    return NewAST(ctx->file, start, pos, Defer, .body=body);
}

PARSER(parse_skip) {
    const char *start = pos;
    if (!match_word(&pos, "continue") && !match_word(&pos, "skip")) return NULL;
    const char *target;
    if (match_word(&pos, "for")) target = "for";
    else if (match_word(&pos, "while")) target = "while";
    else target = get_id(&pos);
    ast_t *skip = NewAST(ctx->file, start, pos, Skip, .target=target);
    skip = parse_optional_conditional_suffix(ctx, skip);
    return skip;
}

PARSER(parse_stop) {
    const char *start = pos;
    if (!match_word(&pos, "stop") && !match_word(&pos, "break")) return NULL;
    const char *target;
    if (match_word(&pos, "for")) target = "for";
    else if (match_word(&pos, "while")) target = "while";
    else target = get_id(&pos);
    ast_t *stop = NewAST(ctx->file, start, pos, Stop, .target=target);
    stop = parse_optional_conditional_suffix(ctx, stop);
    return stop;
}

PARSER(parse_return) {
    const char *start = pos;
    if (!match_word(&pos, "return")) return NULL;
    ast_t *value = optional(ctx, &pos, parse_expr);
    ast_t *ret = NewAST(ctx->file, start, pos, Return, .value=value);
    ret = parse_optional_conditional_suffix(ctx, ret);
    return ret;
}

PARSER(parse_lambda) {
    const char *start = pos;
    if (!match_word(&pos, "func"))
        return NULL;
    spaces(&pos);
    if (!match(&pos, "("))
        return NULL;
    arg_ast_t *args = parse_args(ctx, &pos);
    spaces(&pos);
    type_ast_t *ret = match(&pos, "->") ? optional(ctx, &pos, parse_type) : NULL;
    spaces(&pos);
    expect_closing(ctx, &pos, ")", "I was expecting a ')' to finish this anonymous function's arguments");
    ast_t *body = optional(ctx, &pos, parse_block);
    if (!body) body = NewAST(ctx->file, pos, pos, Block, .statements=NULL);
    return NewAST(ctx->file, start, pos, Lambda, .id=ctx->next_lambda_id++, .args=args, .ret_type=ret, .body=body);
}

PARSER(parse_none) {
    const char *start = pos;
    if (!match_word(&pos, "none"))
        return NULL;
    return NewAST(ctx->file, start, pos, None);
}

PARSER(parse_deserialize) {
    const char *start = pos;
    if (!match_word(&pos, "deserialize"))
        return NULL;

    spaces(&pos);
    expect_str(ctx, start, &pos, "(", "I expected arguments for this `deserialize` call");
    whitespace(&pos);
    ast_t *value = expect(ctx, start, &pos, parse_extended_expr, "I expected an expression here");
    whitespace(&pos);
    expect_str(ctx, start, &pos, "->", "I expected a `-> Type` for this `deserialize` call so I know what it deserializes to");
    whitespace(&pos);
    type_ast_t *type = expect(ctx, start, &pos, parse_type, "I couldn't parse the type for this deserialization");
    whitespace(&pos);
    expect_closing(ctx, &pos, ")", "I expected a closing ')' for this `deserialize` call");
    return NewAST(ctx->file, start, pos, Deserialize, .value=value, .type=type);
}

PARSER(parse_var) {
    const char *start = pos;
    const char* name = get_id(&pos);
    if (!name) return NULL;
    return NewAST(ctx->file, start, pos, Var, .name=name);
}

PARSER(parse_term_no_suffix) {
    spaces(&pos);
    ast_t *term = NULL;
    (void)(
        false
        || (term=parse_none(ctx, pos))
        || (term=parse_num(ctx, pos)) // Must come before int
        || (term=parse_int(ctx, pos))
        || (term=parse_negative(ctx, pos)) // Must come after num/int
        || (term=parse_heap_alloc(ctx, pos))
        || (term=parse_stack_reference(ctx, pos))
        || (term=parse_bool(ctx, pos))
        || (term=parse_text(ctx, pos))
        || (term=parse_path(ctx, pos))
        || (term=parse_lambda(ctx, pos))
        || (term=parse_parens(ctx, pos))
        || (term=parse_table(ctx, pos))
        || (term=parse_set(ctx, pos))
        || (term=parse_deserialize(ctx, pos))
        || (term=parse_var(ctx, pos))
        || (term=parse_list(ctx, pos))
        || (term=parse_reduction(ctx, pos))
        || (term=parse_pass(ctx, pos))
        || (term=parse_defer(ctx, pos))
        || (term=parse_skip(ctx, pos))
        || (term=parse_stop(ctx, pos))
        || (term=parse_return(ctx, pos))
        || (term=parse_not(ctx, pos))
        || (term=parse_extern(ctx, pos))
        || (term=parse_inline_c(ctx, pos))
        );
    return term;
}

PARSER(parse_term) {
    const char *start = pos;
    if (match(&pos, "???"))
        parser_err(ctx, start, pos, "This value needs to be filled in!");

    ast_t *term = parse_term_no_suffix(ctx, pos);
    if (!term) return NULL;

    for (bool progress = true; progress; ) {
        ast_t *new_term;
        progress = (false
            || (new_term=parse_index_suffix(ctx, term))
            || (new_term=parse_method_call_suffix(ctx, term))
            || (new_term=parse_field_suffix(ctx, term))
            || (new_term=parse_fncall_suffix(ctx, term))
            || (new_term=parse_optional_suffix(ctx, term))
            || (new_term=parse_non_optional_suffix(ctx, term))
            );
        if (progress) term = new_term;
    }
    return term;
}

ast_t *parse_method_call_suffix(parse_ctx_t *ctx, ast_t *self) {
    if (!self) return NULL;

    const char *start = self->start;
    const char *pos = self->end;

    if (!match(&pos, ".")) return NULL;
    if (*pos == ' ') return NULL;
    const char *fn = get_id(&pos);
    if (!fn) return NULL;
    spaces(&pos);
    if (!match(&pos, "(")) return NULL;
    whitespace(&pos);

    arg_ast_t *args = NULL;
    for (;;) {
        const char *arg_start = pos;
        const char *name = get_id(&pos);
        whitespace(&pos);
        if (!name || !match(&pos, "=")) {
            name = NULL;
            pos = arg_start;
        }

        ast_t *arg = optional(ctx, &pos, parse_expr);
        if (!arg) {
            if (name) parser_err(ctx, arg_start, pos, "I expected an argument here");
            break;
        }
        args = new(arg_ast_t, .name=name, .value=arg, .next=args);
        if (!match_separator(&pos))
            break;
    }
    REVERSE_LIST(args);

    whitespace(&pos);

    if (!match(&pos, ")"))
        parser_err(ctx, start, pos, "This parenthesis is unclosed");

    return NewAST(ctx->file, start, pos, MethodCall, .self=self, .name=fn, .args=args);
}

ast_t *parse_fncall_suffix(parse_ctx_t *ctx, ast_t *fn) {
    if (!fn) return NULL;

    const char *start = fn->start;
    const char *pos = fn->end;

    if (!match(&pos, "(")) return NULL;

    whitespace(&pos);

    arg_ast_t *args = NULL;
    for (;;) {
        const char *arg_start = pos;
        const char *name = get_id(&pos);
        whitespace(&pos);
        if (!name || !match(&pos, "=")) {
            name = NULL;
            pos = arg_start;
        }

        ast_t *arg = optional(ctx, &pos, parse_expr);
        if (!arg) {
            if (name)
                parser_err(ctx, arg_start, pos, "I expected an argument here");
            break;
        }
        args = new(arg_ast_t, .name=name, .value=arg, .next=args);
        if (!match_separator(&pos))
            break;
    }

    whitespace(&pos);

    if (!match(&pos, ")"))
        parser_err(ctx, start, pos, "This parenthesis is unclosed");

    REVERSE_LIST(args);
    return NewAST(ctx->file, start, pos, FunctionCall, .fn=fn, .args=args);
}

ast_e match_binary_operator(const char **pos)
{
    switch (**pos) {
    case '+': {
        *pos += 1;
        return match(pos, "+") ? Concat : Plus;
    }
    case '-': {
        *pos += 1;
        if ((*pos)[0] != ' ' && (*pos)[-2] == ' ') // looks like `fn -5`
            return Unknown;
        return Minus;
    }
    case '*': *pos += 1; return Multiply;
    case '/': *pos += 1; return Divide;
    case '^': *pos += 1; return Power;
    case '<': {
        *pos += 1;
        if (match(pos, "=")) return LessThanOrEquals; // "<="
        else if (match(pos, ">")) return Compare; // "<>"
        else if (match(pos, "<")) {
            if (match(pos, "<"))
                return UnsignedLeftShift; // "<<<"
            return LeftShift; // "<<"
        } else return LessThan;
    }
    case '>': {
        *pos += 1;
        if (match(pos, "=")) return GreaterThanOrEquals; // ">="
        if (match(pos, ">")) {
            if (match(pos, ">"))
                return UnsignedRightShift; // ">>>"
            return RightShift; // ">>"
        }
        return GreaterThan;
    }
    default: {
        if (match(pos, "!=")) return NotEquals;
        else if (match(pos, "==") && **pos != '=') return Equals;
        else if (match_word(pos, "and")) return And;
        else if (match_word(pos, "or")) return Or;
        else if (match_word(pos, "xor")) return Xor;
        else if (match_word(pos, "mod1")) return Mod1;
        else if (match_word(pos, "mod")) return Mod;
        else if (match_word(pos, "_min_")) return Min;
        else if (match_word(pos, "_max_")) return Max;
        else return Unknown;
    }
    }
}

static ast_t *parse_infix_expr(parse_ctx_t *ctx, const char *pos, int min_tightness) {
    ast_t *lhs = optional(ctx, &pos, parse_term);
    if (!lhs) return NULL;

    int64_t starting_line = get_line_number(ctx->file, pos);
    int64_t starting_indent = get_indent(ctx, pos);
    spaces(&pos);
    for (ast_e op; (op=match_binary_operator(&pos)) != Unknown && op_tightness[op] >= min_tightness; spaces(&pos)) {
        ast_t *key = NULL;
        if (op == Min || op == Max) {
            key = NewAST(ctx->file, pos, pos, Var, .name="$");
            for (bool progress = true; progress; ) {
                ast_t *new_term;
                progress = (false
                    || (new_term=parse_index_suffix(ctx, key))
                    || (new_term=parse_method_call_suffix(ctx, key))
                    || (new_term=parse_field_suffix(ctx, key))
                    || (new_term=parse_fncall_suffix(ctx, key))
                    || (new_term=parse_optional_suffix(ctx, key))
                    || (new_term=parse_non_optional_suffix(ctx, key))
                    );
                if (progress) key = new_term;
            }
            if (key && key->tag == Var) key = NULL;
            else if (key) pos = key->end;
        }

        whitespace(&pos);
        if (get_line_number(ctx->file, pos) != starting_line && get_indent(ctx, pos) < starting_indent)
            parser_err(ctx, pos, eol(pos), "I expected this line to be at least as indented than the line above it");

        ast_t *rhs = parse_infix_expr(ctx, pos, op_tightness[op] + 1);
        if (!rhs) break;
        pos = rhs->end;
        
        if (op == Min) {
            return NewAST(ctx->file, lhs->start, rhs->end, Min, .lhs=lhs, .rhs=rhs, .key=key);
        } else if (op == Max) {
            return NewAST(ctx->file, lhs->start, rhs->end, Max, .lhs=lhs, .rhs=rhs, .key=key);
        } else {
            lhs = new(ast_t, .file=ctx->file, .start=lhs->start, .end=rhs->end, .tag=op, .__data.Plus.lhs=lhs, .__data.Plus.rhs=rhs);
        }
    }
    return lhs;
}

PARSER(parse_expr) {
    return parse_infix_expr(ctx, pos, 0);
}

PARSER(parse_declaration) {
    const char *start = pos;
    ast_t *var = parse_var(ctx, pos);
    if (!var) return NULL;
    pos = var->end;
    spaces(&pos);
    if (!match(&pos, ":")) return NULL;
    spaces(&pos);
    type_ast_t *type = optional(ctx, &pos, parse_type);
    spaces(&pos);
    ast_t *val = NULL;
    if (match(&pos, "=")) {
        val = optional(ctx, &pos, parse_extended_expr);
        if (!val) {
            if (optional(ctx, &pos, parse_use))
                parser_err(ctx, start, pos, "'use' statements are only allowed at the top level of a file");
            else
                parser_err(ctx, pos, eol(pos), "This is not a valid expression");
        }
    }
    return NewAST(ctx->file, start, pos, Declare, .var=var, .type=type, .value=val);
}

PARSER(parse_top_declaration) {
    ast_t *declaration = parse_declaration(ctx, pos);
    if (declaration)
        declaration->__data.Declare.top_level = true;
    return declaration;
}

PARSER(parse_update) {
    const char *start = pos;
    ast_t *lhs = optional(ctx, &pos, parse_expr);
    if (!lhs) return NULL;
    spaces(&pos);
    ast_e op;
    if (match(&pos, "+=")) op = PlusUpdate;
    else if (match(&pos, "++=")) op = ConcatUpdate;
    else if (match(&pos, "-=")) op = MinusUpdate;
    else if (match(&pos, "*=")) op = MultiplyUpdate;
    else if (match(&pos, "/=")) op = DivideUpdate;
    else if (match(&pos, "^=")) op = PowerUpdate;
    else if (match(&pos, "<<=")) op = LeftShiftUpdate;
    else if (match(&pos, "<<<=")) op = UnsignedLeftShiftUpdate;
    else if (match(&pos, ">>=")) op = RightShiftUpdate;
    else if (match(&pos, ">>>=")) op = UnsignedRightShiftUpdate;
    else if (match(&pos, "and=")) op = AndUpdate;
    else if (match(&pos, "or=")) op = OrUpdate;
    else if (match(&pos, "xor=")) op = XorUpdate;
    else return NULL;
    ast_t *rhs = expect(ctx, start, &pos, parse_extended_expr, "I expected an expression here");
    return new(ast_t, .file=ctx->file, .start=start, .end=pos, .tag=op, .__data.PlusUpdate.lhs=lhs, .__data.PlusUpdate.rhs=rhs);
}

PARSER(parse_assignment) {
    const char *start = pos;
    ast_list_t *targets = NULL;
    for (;;) {
        ast_t *lhs = optional(ctx, &pos, parse_term);
        if (!lhs) break;
        targets = new(ast_list_t, .ast=lhs, .next=targets);
        spaces(&pos);
        if (!match(&pos, ",")) break;
        whitespace(&pos);
    }

    if (!targets) return NULL;

    spaces(&pos);
    if (!match(&pos, "=")) return NULL;
    if (match(&pos, "=")) return NULL; // == comparison

    ast_list_t *values = NULL;
    for (;;) {
        ast_t *rhs = optional(ctx, &pos, parse_extended_expr);
        if (!rhs) break;
        values = new(ast_list_t, .ast=rhs, .next=values);
        spaces(&pos);
        if (!match(&pos, ",")) break;
        whitespace(&pos);
    }

    REVERSE_LIST(targets);
    REVERSE_LIST(values);

    return NewAST(ctx->file, start, pos, Assign, .targets=targets, .values=values);
}

PARSER(parse_statement) {
    ast_t *stmt = NULL;
    if ((stmt=parse_declaration(ctx, pos))
        || (stmt=parse_doctest(ctx, pos))
        || (stmt=parse_assert(ctx, pos)))
        return stmt;

    if (!(false 
        || (stmt=parse_update(ctx, pos))
        || (stmt=parse_assignment(ctx, pos))
    ))
        stmt = parse_extended_expr(ctx, pos);
    
    for (bool progress = (stmt != NULL); progress; ) {
        ast_t *new_stmt;
        progress = false;
        if (stmt->tag == Var) {
            progress = (false
                || (new_stmt=parse_method_call_suffix(ctx, stmt))
                || (new_stmt=parse_fncall_suffix(ctx, stmt))
            );
        } else if (stmt->tag == FunctionCall) {
            new_stmt = parse_optional_conditional_suffix(ctx, stmt);
            progress = (new_stmt != stmt);
        }

        if (progress) stmt = new_stmt;
    }
    return stmt;

}

PARSER(parse_extended_expr) {
    ast_t *expr = NULL;

    if (false
        || (expr=optional(ctx, &pos, parse_for))
        || (expr=optional(ctx, &pos, parse_while))
        || (expr=optional(ctx, &pos, parse_if))
        || (expr=optional(ctx, &pos, parse_when))
        || (expr=optional(ctx, &pos, parse_repeat))
        || (expr=optional(ctx, &pos, parse_do))
        )
        return expr;

    return parse_expr(ctx, pos);
}

PARSER(parse_block) {
    const char *start = pos;
    spaces(&pos);

    ast_list_t *statements = NULL;
    if (!indent(ctx, &pos)) {
        // Inline block
        spaces(&pos);
        while (*pos) {
            spaces(&pos);
            ast_t *stmt = optional(ctx, &pos, parse_statement);
            if (!stmt) break;
            statements = new(ast_list_t, .ast=stmt, .next=statements);
            spaces(&pos);
            if (!match(&pos, ";")) break;
        }
    } else {
        goto indented;
    }

    if (indent(ctx, &pos)) {
      indented:;
        int64_t block_indent = get_indent(ctx, pos);
        whitespace(&pos);
        while (*pos) {
            ast_t *stmt = optional(ctx, &pos, parse_statement);
            if (!stmt) {
                const char *line_start = pos;
                if (match_word(&pos, "struct"))
                    parser_err(ctx, line_start, eol(pos), "Struct definitions are only allowed at the top level");
                else if (match_word(&pos, "enum"))
                    parser_err(ctx, line_start, eol(pos), "Enum definitions are only allowed at the top level");
                else if (match_word(&pos, "func"))
                    parser_err(ctx, line_start, eol(pos), "Function definitions are only allowed at the top level");
                else if (match_word(&pos, "use"))
                    parser_err(ctx, line_start, eol(pos), "'use' statements are only allowed at the top level");

                spaces(&pos);
                if (*pos && *pos != '\r' && *pos != '\n')
                    parser_err(ctx, pos, eol(pos), "I couldn't parse this line");
                break;
            }
            statements = new(ast_list_t, .ast=stmt, .next=statements);
            whitespace(&pos);

            // Guard against having two valid statements on the same line, separated by spaces (but no newlines):
            if (!memchr(stmt->end, '\n', (size_t)(pos - stmt->end))) {
                if (*pos)
                    parser_err(ctx, pos, eol(pos), "I don't know how to parse the rest of this line");
                pos = stmt->end;
                break;
            }

            if (get_indent(ctx, pos) != block_indent) {
                pos = stmt->end; // backtrack
                break;
            }
        }
    }
    REVERSE_LIST(statements);
    return NewAST(ctx->file, start, pos, Block, .statements=statements);
}

PARSER(parse_namespace) {
    const char *start = pos;
    whitespace(&pos);
    int64_t indent = get_indent(ctx, pos);
    ast_list_t *statements = NULL;
    for (;;) {
        const char *next = pos;
        whitespace(&next);
        if (get_indent(ctx, next) != indent) break;
        ast_t *stmt;
        if ((stmt=optional(ctx, &pos, parse_struct_def))
            ||(stmt=optional(ctx, &pos, parse_func_def))
            ||(stmt=optional(ctx, &pos, parse_enum_def))
            ||(stmt=optional(ctx, &pos, parse_lang_def))
            ||(stmt=optional(ctx, &pos, parse_extend))
            ||(stmt=optional(ctx, &pos, parse_convert_def))
            ||(stmt=optional(ctx, &pos, parse_use))
            ||(stmt=optional(ctx, &pos, parse_extern))
            ||(stmt=optional(ctx, &pos, parse_inline_c))
            ||(stmt=optional(ctx, &pos, parse_declaration)))
        {
            statements = new(ast_list_t, .ast=stmt, .next=statements);
            pos = stmt->end;
            whitespace(&pos); // TODO: check for newline
            // if (!(space_types & WHITESPACE_NEWLINES)) {
            //     pos = stmt->end;
            //     break;
            // }
        } else {
            if (get_indent(ctx, next) > indent && next < eol(next))
                parser_err(ctx, next, eol(next), "I couldn't parse this namespace declaration");
            break;
        }
    }
    REVERSE_LIST(statements);
    return NewAST(ctx->file, start, pos, Block, .statements=statements);
}

PARSER(parse_file_body) {
    const char *start = pos;
    whitespace(&pos);
    ast_list_t *statements = NULL;
    for (;;) {
        const char *next = pos;
        whitespace(&next);
        if (get_indent(ctx, next) != 0) break;
        ast_t *stmt;
        if ((stmt=optional(ctx, &pos, parse_struct_def))
            ||(stmt=optional(ctx, &pos, parse_func_def))
            ||(stmt=optional(ctx, &pos, parse_enum_def))
            ||(stmt=optional(ctx, &pos, parse_lang_def))
            ||(stmt=optional(ctx, &pos, parse_extend))
            ||(stmt=optional(ctx, &pos, parse_convert_def))
            ||(stmt=optional(ctx, &pos, parse_use))
            ||(stmt=optional(ctx, &pos, parse_extern))
            ||(stmt=optional(ctx, &pos, parse_inline_c))
            ||(stmt=optional(ctx, &pos, parse_top_declaration)))
        {
            statements = new(ast_list_t, .ast=stmt, .next=statements);
            pos = stmt->end;
            whitespace(&pos); // TODO: check for newline
        } else {
            break;
        }
    }
    whitespace(&pos);
    if (pos < ctx->file->text + ctx->file->len && *pos != '\0') {
        parser_err(ctx, pos, eol(pos), "I expect all top-level statements to be declarations of some kind");
    }
    REVERSE_LIST(statements);
    return NewAST(ctx->file, start, pos, Block, .statements=statements);
}

PARSER(parse_struct_def) {
    // struct Foo(...) [: \n body]
    const char *start = pos;
    if (!match_word(&pos, "struct")) return NULL;

    int64_t starting_indent = get_indent(ctx, pos);

    spaces(&pos);
    const char *name = get_id(&pos);
    if (!name) parser_err(ctx, start, pos, "I expected a name for this struct");
    spaces(&pos);

    if (!match(&pos, "("))
        parser_err(ctx, pos, pos, "I expected a '(' and a list of fields here");

    arg_ast_t *fields = parse_args(ctx, &pos);

    whitespace(&pos);
    bool secret = false, external = false, opaque = false;
    if (match(&pos, ";")) { // Extra flags
        whitespace(&pos);
        for (;;) {
            if (match_word(&pos, "secret")) {
                secret = true;
            } else if (match_word(&pos, "extern")) {
                external = true;
            } else if (match_word(&pos, "opaque")) {
                if (fields)
                    parser_err(ctx, pos-strlen("opaque"), pos, "A struct can't be opaque if it has fields defined");
                opaque = true;
            } else {
                break;
            }

            if (!match_separator(&pos))
                break;
        }
    }


    expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this struct");

    ast_t *namespace = NULL;
    const char *ns_pos = pos;
    whitespace(&ns_pos);
    int64_t ns_indent = get_indent(ctx, ns_pos);
    if (ns_indent > starting_indent) {
        pos = ns_pos;
        namespace = optional(ctx, &pos, parse_namespace);
    }
    if (!namespace)
        namespace = NewAST(ctx->file, pos, pos, Block, .statements=NULL);
    return NewAST(ctx->file, start, pos, StructDef, .name=name, .fields=fields, .namespace=namespace,
                  .secret=secret, .external=external, .opaque=opaque);
}

PARSER(parse_enum_def) {
    // tagged union: enum Foo(a, b(x:Int,y:Int)=5, ...) [: \n namespace]
    const char *start = pos;
    if (!match_word(&pos, "enum")) return NULL;
    int64_t starting_indent = get_indent(ctx, pos);
    spaces(&pos);
    const char *name = get_id(&pos);
    if (!name)
        parser_err(ctx, start, pos, "I expected a name for this enum");
    spaces(&pos);
    if (!match(&pos, "(")) return NULL;

    tag_ast_t *tags = NULL;
    whitespace(&pos);
    for (;;) {
        spaces(&pos);
        const char *tag_name = get_id(&pos);
        if (!tag_name) break;

        spaces(&pos);
        arg_ast_t *fields;
        bool secret = false;
        if (match(&pos, "(")) {
            whitespace(&pos);
            fields = parse_args(ctx, &pos);
            whitespace(&pos);
            if (match(&pos, ";")) { // Extra flags
                whitespace(&pos);
                secret = match_word(&pos, "secret");
                whitespace(&pos);
            }
            expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this tagged union member");
        } else {
            fields = NULL;
        }

        tags = new(tag_ast_t, .name=tag_name, .fields=fields, .secret=secret, .next=tags);

        if (!match_separator(&pos))
            break;
    }

    whitespace(&pos);
    expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this enum definition");

    REVERSE_LIST(tags);

    if (tags == NULL)
        parser_err(ctx, start, pos, "This enum does not have any tags!");

    ast_t *namespace = NULL;
    const char *ns_pos = pos;
    whitespace(&ns_pos);
    int64_t ns_indent = get_indent(ctx, ns_pos);
    if (ns_indent > starting_indent) {
        pos = ns_pos;
        namespace = optional(ctx, &pos, parse_namespace);
    }
    if (!namespace)
        namespace = NewAST(ctx->file, pos, pos, Block, .statements=NULL);

    return NewAST(ctx->file, start, pos, EnumDef, .name=name, .tags=tags, .namespace=namespace);
}

PARSER(parse_lang_def) {
    const char *start = pos;
    // lang Name: [namespace...]
    if (!match_word(&pos, "lang")) return NULL;
    int64_t starting_indent = get_indent(ctx, pos);
    spaces(&pos);
    const char *name = get_id(&pos);
    if (!name)
        parser_err(ctx, start, pos, "I expected a name for this lang");
    spaces(&pos);

    ast_t *namespace = NULL;
    const char *ns_pos = pos;
    whitespace(&ns_pos);
    int64_t ns_indent = get_indent(ctx, ns_pos);
    if (ns_indent > starting_indent) {
        pos = ns_pos;
        namespace = optional(ctx, &pos, parse_namespace);
    }
    if (!namespace)
        namespace = NewAST(ctx->file, pos, pos, Block, .statements=NULL);

    return NewAST(ctx->file, start, pos, LangDef, .name=name, .namespace=namespace);
}

PARSER(parse_extend) {
    const char *start = pos;
    // extend Name: body...
    if (!match_word(&pos, "extend")) return NULL;
    int64_t starting_indent = get_indent(ctx, pos);
    spaces(&pos);
    const char *name = get_id(&pos);
    if (!name)
        parser_err(ctx, start, pos, "I expected a name for this lang");

    ast_t *body = NULL;
    const char *ns_pos = pos;
    whitespace(&ns_pos);
    int64_t ns_indent = get_indent(ctx, ns_pos);
    if (ns_indent > starting_indent) {
        pos = ns_pos;
        body = optional(ctx, &pos, parse_namespace);
    }
    if (!body)
        body = NewAST(ctx->file, pos, pos, Block, .statements=NULL);

    return NewAST(ctx->file, start, pos, Extend, .name=name, .body=body);
}

arg_ast_t *parse_args(parse_ctx_t *ctx, const char **pos)
{
    arg_ast_t *args = NULL;
    for (;;) {
        const char *batch_start = *pos;
        ast_t *default_val = NULL;
        type_ast_t *type = NULL;

        typedef struct name_list_s {
            const char *name;
            struct name_list_s *next;
        } name_list_t;

        name_list_t *names = NULL;
        for (;;) {
            whitespace(pos);
            const char *name = get_id(pos);
            if (!name) break;
            whitespace(pos);

            if (match(pos, ":")) {
                type = expect(ctx, *pos-1, pos, parse_type, "I expected a type here");
                names = new(name_list_t, .name=name, .next=names);
                whitespace(pos);
                if (match(pos, "="))
                    default_val = expect(ctx, *pos-1, pos, parse_term, "I expected a value after this '='");
                break;
            } else if (strncmp(*pos, "==", 2) != 0 && match(pos, "=")) {
                default_val = expect(ctx, *pos-1, pos, parse_term, "I expected a value after this '='");
                names = new(name_list_t, .name=name, .next=names);
                break;
            } else if (name) {
                names = new(name_list_t, .name=name, .next=names);
                spaces(pos);
                if (!match(pos, ",")) break;
            } else {
                break;
            }
        }
        if (!names) break;
        if (!default_val && !type)
            parser_err(ctx, batch_start, *pos, "I expected a ':' and type, or '=' and a default value after this parameter (", names->name, ")");

        REVERSE_LIST(names);
        for (; names; names = names->next)
            args = new(arg_ast_t, .name=names->name, .type=type, .value=default_val, .next=args);

        if (!match_separator(pos))
            break;
    }

    REVERSE_LIST(args);
    return args;
}

PARSER(parse_func_def) {
    const char *start = pos;
    if (!match_word(&pos, "func")) return NULL;

    ast_t *name = optional(ctx, &pos, parse_var);
    if (!name) return NULL;

    spaces(&pos);

    expect_str(ctx, start, &pos, "(", "I expected a parenthesis for this function's arguments");

    arg_ast_t *args = parse_args(ctx, &pos);
    spaces(&pos);
    type_ast_t *ret_type = match(&pos, "->") ? optional(ctx, &pos, parse_type) : NULL;
    whitespace(&pos);
    bool is_inline = false;
    ast_t *cache_ast = NULL;
    for (bool specials = match(&pos, ";"); specials; specials = match_separator(&pos)) {
        const char *flag_start = pos;
        if (match_word(&pos, "inline")) {
            is_inline = true;
        } else if (match_word(&pos, "cached")) {
            if (!cache_ast) cache_ast = NewAST(ctx->file, pos, pos, Int, .str="-1");
        } else if (match_word(&pos, "cache_size")) {
            whitespace(&pos);
            if (!match(&pos, "="))
                parser_err(ctx, flag_start, pos, "I expected a value for 'cache_size'");
            whitespace(&pos);
            cache_ast = expect(ctx, start, &pos, parse_expr, "I expected a maximum size for the cache");
        }
    }
    expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this function definition");

    ast_t *body = expect(ctx, start, &pos, parse_block,
                             "This function needs a body block");
    return NewAST(ctx->file, start, pos, FunctionDef,
                  .name=name, .args=args, .ret_type=ret_type, .body=body, .cache=cache_ast,
                  .is_inline=is_inline);
}

PARSER(parse_convert_def) {
    const char *start = pos;
    if (!match_word(&pos, "convert")) return NULL;

    spaces(&pos);

    if (!match(&pos, "(")) return NULL;

    arg_ast_t *args = parse_args(ctx, &pos);
    spaces(&pos);
    type_ast_t *ret_type = match(&pos, "->") ? optional(ctx, &pos, parse_type) : NULL;
    whitespace(&pos);
    bool is_inline = false;
    ast_t *cache_ast = NULL;
    for (bool specials = match(&pos, ";"); specials; specials = match_separator(&pos)) {
        const char *flag_start = pos;
        if (match_word(&pos, "inline")) {
            is_inline = true;
        } else if (match_word(&pos, "cached")) {
            if (!cache_ast) cache_ast = NewAST(ctx->file, pos, pos, Int, .str="-1");
        } else if (match_word(&pos, "cache_size")) {
            whitespace(&pos);
            if (!match(&pos, "="))
                parser_err(ctx, flag_start, pos, "I expected a value for 'cache_size'");
            whitespace(&pos);
            cache_ast = expect(ctx, start, &pos, parse_expr, "I expected a maximum size for the cache");
        }
    }
    expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this function definition");

    ast_t *body = expect(ctx, start, &pos, parse_block,
                             "This function needs a body block");
    return NewAST(ctx->file, start, pos, ConvertDef,
                  .args=args, .ret_type=ret_type, .body=body, .cache=cache_ast, .is_inline=is_inline);
}

PARSER(parse_extern) {
    const char *start = pos;
    if (!match_word(&pos, "extern")) return NULL;
    spaces(&pos);
    const char* name = get_id(&pos);
    spaces(&pos);
    if (!match(&pos, ":"))
        parser_err(ctx, start, pos, "I couldn't get a type for this extern");
    type_ast_t *type = expect(ctx, start, &pos, parse_type, "I couldn't parse the type for this extern");
    return NewAST(ctx->file, start, pos, Extern, .name=name, .type=type);
}

PARSER(parse_inline_c) {
    const char *start = pos;
    if (!match_word(&pos, "C_code")) return NULL;

    spaces(&pos);
    type_ast_t *type = NULL;
    ast_list_t *chunks;
    if (match(&pos, ":")) {
        type = expect(ctx, start, &pos, parse_type, "I couldn't parse the type for this C_code code");
        spaces(&pos);
        if (!match(&pos, "("))
            parser_err(ctx, start, pos, "I expected a '(' here");
        chunks = new(ast_list_t, .ast=NewAST(ctx->file, pos, pos, TextLiteral, "({"),
                     .next=_parse_text_helper(ctx, &pos, '(', ')', '@', false));
        if (type) {
            REVERSE_LIST(chunks);
            chunks = new(ast_list_t, .ast=NewAST(ctx->file, pos, pos, TextLiteral, "; })"), .next=chunks);
            REVERSE_LIST(chunks);
        }
    } else {
        if (!match(&pos, "{"))
            parser_err(ctx, start, pos, "I expected a '{' here");
        chunks = _parse_text_helper(ctx, &pos, '{', '}', '@', false);
    }

    return NewAST(ctx->file, start, pos, InlineCCode, .chunks=chunks, .type_ast=type);
}

PARSER(parse_doctest) {
    const char *start = pos;
    if (!match(&pos, ">>")) return NULL;
    spaces(&pos);
    ast_t *expr = expect(ctx, start, &pos, parse_statement, "I couldn't parse the expression for this doctest");
    whitespace(&pos);
    ast_t *expected = NULL;
    if (match(&pos, "=")) {
        spaces(&pos);
        expected = expect(ctx, start, &pos, parse_extended_expr, "I couldn't parse the expected expression here");
    } else {
        pos = expr->end;
    }
    return NewAST(ctx->file, start, pos, DocTest, .expr=expr, .expected=expected);
}

PARSER(parse_assert) {
    const char *start = pos;
    if (!match_word(&pos, "assert")) return NULL;
    spaces(&pos);
    ast_t *expr = expect(ctx, start, &pos, parse_extended_expr, "I couldn't parse the expression for this assert");
    spaces(&pos);
    ast_t *message = NULL;
    if (match(&pos, ",")) {
        whitespace(&pos);
        message = expect(ctx, start, &pos, parse_extended_expr, "I couldn't parse the error message for this assert");
    } else {
        pos = expr->end;
    }
    return NewAST(ctx->file, start, pos, Assert, .expr=expr, .message=message);
}

PARSER(parse_use) {
    const char *start = pos;

    ast_t *var = parse_var(ctx, pos);
    if (var) {
        pos = var->end;
        spaces(&pos);
        if (!match(&pos, ":=")) return NULL;
        spaces(&pos);
    }

    if (!match_word(&pos, "use")) return NULL;
    spaces(&pos);
    size_t name_len = strcspn(pos, " \t\r\n;");
    if (name_len < 1)
        parser_err(ctx, start, pos, "There is no module name here to use");
    char *name = GC_strndup(pos, name_len);
    pos += name_len;
    while (match(&pos, ";")) continue;
    int what; 
    if (name[0] == '<' || ends_with(name, ".h")) {
        what = USE_HEADER;
    } else if (starts_with(name, "-l")) {
        what = USE_SHARED_OBJECT;
    } else if (ends_with(name, ".c")) {
        what = USE_C_CODE;
    } else if (ends_with(name, ".S") || ends_with(name, ".s")) {
        what = USE_ASM;
    } else if (starts_with(name, "./") || starts_with(name, "/") || starts_with(name, "../") || starts_with(name, "~/")) {
        what = USE_LOCAL;
    } else {
        what = USE_MODULE;
    }
    return NewAST(ctx->file, start, pos, Use, .var=var, .path=name, .what=what);
}

ast_t *parse_file(const char *path, jmp_buf *on_err) {
    if (path[0] != '<' && path[0] != '/')
        fail("Path is not fully resolved: ", path);
    // NOTE: this cache leaks a bounded amount of memory. The cache will never
    // hold more than PARSE_CACHE_SIZE entries (see below), but each entry's
    // AST holds onto a reference to the file it came from, so they could
    // potentially be somewhat large.
    static Table_t cached = {};
    ast_t *ast = Table$str_get(cached, path);
    if (ast) return ast;

    file_t *file;
    if (path[0] == '<') {
        const char *endbracket = strchr(path, '>');
        if (!endbracket) return NULL;
        file = spoof_file(GC_strndup(path, (size_t)(endbracket + 1 - path)), endbracket + 1);
    } else {
        file = load_file(path);
        if (!file) return NULL;
    }

    parse_ctx_t ctx = {
        .file=file,
        .on_err=on_err,
    };

    const char *pos = file->text;
    if (match(&pos, "#!")) // shebang
        some_not(&pos, "\r\n");

    whitespace(&pos);
    ast = parse_file_body(&ctx, pos);
    pos = ast->end;
    whitespace(&pos);
    if (pos < file->text + file->len && *pos != '\0') {
        parser_err(&ctx, pos, pos + strlen(pos), "I couldn't parse this part of the file");
    }

    // If cache is getting too big, evict a random entry:
    if (cached.entries.length > PARSE_CACHE_SIZE) {
        // FIXME: this currently evicts the first entry, but it should be more like
        // an LRU cache
        struct {const char *path; ast_t *ast; } *to_remove = Table$entry(cached, 1);
        Table$str_remove(&cached, to_remove->path);
    }

    // Save the AST in the cache:
    Table$str_set(&cached, path, ast);
    return ast;
}

type_ast_t *parse_type_str(const char *str) {
    file_t *file = spoof_file("<type>", str);
    parse_ctx_t ctx = {
        .file=file,
        .on_err=NULL,
    };

    const char *pos = file->text;
    whitespace(&pos);
    type_ast_t *ast = parse_type(&ctx, pos);
    if (!ast) return ast;
    pos = ast->end;
    whitespace(&pos);
    if (strlen(pos) > 0) {
        parser_err(&ctx, pos, pos + strlen(pos), "I couldn't parse this part of the type");
    }
    return ast;
}

ast_t *parse(const char *str) {
    file_t *file = spoof_file("<string>", str);
    parse_ctx_t ctx = {
        .file=file,
        .on_err=NULL,
    };

    const char *pos = file->text;
    whitespace(&pos);
    ast_t *ast = parse_file_body(&ctx, pos);
    pos = ast->end;
    whitespace(&pos);
    if (pos < file->text + file->len && *pos != '\0')
        parser_err(&ctx, pos, pos + strlen(pos), "I couldn't parse this part of the string");
    return ast;
}

ast_t *parse_expression(const char *str) {
    file_t *file = spoof_file("<string>", str);
    parse_ctx_t ctx = {
        .file=file,
        .on_err=NULL,
    };

    const char *pos = file->text;
    whitespace(&pos);
    ast_t *ast = parse_extended_expr(&ctx, pos);
    pos = ast->end;
    whitespace(&pos);
    if (pos < file->text + file->len && *pos != '\0')
        parser_err(&ctx, pos, pos + strlen(pos), "I couldn't parse this part of the string");
    return ast;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
