// Recursive descent parser for parsing code
#include <ctype.h>
#include <gc.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <unistr.h>
#include <unictype.h>
#include <uniname.h>
#include <signal.h>

#include "ast.h"
#include "builtins/functions.h"
#include "builtins/integers.h"
#include "builtins/text.h"
#include "builtins/table.h"
#include "builtins/util.h"

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

typedef ast_t* (parser_t)(parse_ctx_t*,const char*);

extern void builtin_fail(const char *fmt, ...);

#define SPACES_PER_INDENT 4

#define PARSER(name) ast_t *name(parse_ctx_t *ctx, const char *pos)

int op_tightness[] = {
    [BINOP_POWER]=9,
    [BINOP_MULT]=8, [BINOP_DIVIDE]=8, [BINOP_MOD]=8, [BINOP_MOD1]=8,
    [BINOP_PLUS]=7, [BINOP_MINUS]=7,
    [BINOP_CONCAT]=6,
    [BINOP_LSHIFT]=5, [BINOP_RSHIFT]=5,
    [BINOP_MIN]=4, [BINOP_MAX]=4,
    [BINOP_EQ]=3, [BINOP_NE]=3,
    [BINOP_LT]=2, [BINOP_LE]=2, [BINOP_GT]=2, [BINOP_GE]=2,
    [BINOP_CMP]=2,
    [BINOP_AND]=1, [BINOP_OR]=1, [BINOP_XOR]=1,
};

static const char *keywords[] = {
    "yes", "xor", "while", "when", "use", "struct", "stop", "skip", "return",
    "or", "not", "no", "mod1", "mod", "pass", "lang", "inline", "in", "if",
    "func", "for", "extern", "enum", "else", "do", "defer", "and", "_min_", "_max_",
    NULL,
};

enum {NORMAL_FUNCTION=0, EXTERN_FUNCTION=1};
typedef enum {WHITESPACE_NONE=0, WHITESPACE_SPACES=1, WHITESPACE_NEWLINES=2, WHITESPACE_COMMENTS=4} whitespace_types_e;

static inline size_t some_of(const char **pos, const char *allow);
static inline size_t some_not(const char **pos, const char *forbid);
static inline size_t spaces(const char **pos);
static inline whitespace_types_e whitespace(const char **pos);
static inline size_t match(const char **pos, const char *target);
static inline void expect_str(parse_ctx_t *ctx, const char *start, const char **pos, const char *target, const char *fmt, ...);
static inline void expect_closing(parse_ctx_t *ctx, const char **pos, const char *target, const char *fmt, ...);
static inline size_t match_word(const char **pos, const char *word);
static inline const char* get_word(const char **pos);
static inline const char* get_id(const char **pos);
static inline bool comment(const char **pos);
static inline bool indent(parse_ctx_t *ctx, const char **pos);
static inline binop_e match_binary_operator(const char **pos);
static ast_t *parse_fncall_suffix(parse_ctx_t *ctx, ast_t *fn);
static ast_t *parse_method_call_suffix(parse_ctx_t *ctx, ast_t *self);
static ast_t *parse_field_suffix(parse_ctx_t *ctx, ast_t *lhs);
static ast_t *parse_index_suffix(parse_ctx_t *ctx, ast_t *lhs);
static ast_t *parse_comprehension_suffix(parse_ctx_t *ctx, ast_t *lhs);
static ast_t *parse_optional_conditional_suffix(parse_ctx_t *ctx, ast_t *lhs);
static ast_t *parse_optional_suffix(parse_ctx_t *ctx, ast_t *lhs);
static arg_ast_t *parse_args(parse_ctx_t *ctx, const char **pos, bool allow_unnamed);
static type_ast_t *parse_non_optional_type(parse_ctx_t *ctx, const char *pos);
static type_ast_t *parse_type(parse_ctx_t *ctx, const char *pos);
static PARSER(parse_array);
static PARSER(parse_block);
static PARSER(parse_channel);
static PARSER(parse_declaration);
static PARSER(parse_do);
static PARSER(parse_doctest);
static PARSER(parse_enum_def);
static PARSER(parse_expr);
static PARSER(parse_extended_expr);
static PARSER(parse_extern);
static PARSER(parse_file_body);
static PARSER(parse_for);
static PARSER(parse_func_def);
static PARSER(parse_if);
static PARSER(parse_inline_c);
static PARSER(parse_lang_def);
static PARSER(parse_linker);
static PARSER(parse_namespace);
static PARSER(parse_path);
static PARSER(parse_say);
static PARSER(parse_statement);
static PARSER(parse_struct_def);
static PARSER(parse_term);
static PARSER(parse_term_no_suffix);
static PARSER(parse_text);
static PARSER(parse_top_declaration);
static PARSER(parse_use);
static PARSER(parse_var);
static PARSER(parse_when);
static PARSER(parse_while);

//
// Print a parse error and exit (or use the on_err longjmp)
//
__attribute__((noreturn, format(printf, 4, 0)))
static _Noreturn void vparser_err(parse_ctx_t *ctx, const char *start, const char *end, const char *fmt, va_list args) {
    if (isatty(STDERR_FILENO) && !getenv("NO_COLOR"))
        fputs("\x1b[31;1;7m", stderr);
    fprintf(stderr, "%s:%ld.%ld: ", ctx->file->relative_filename, get_line_number(ctx->file, start),
            get_line_column(ctx->file, start));
    vfprintf(stderr, fmt, args);
    if (isatty(STDERR_FILENO) && !getenv("NO_COLOR"))
        fputs(" \x1b[m", stderr);
    fputs("\n\n", stderr);

    highlight_error(ctx->file, start, end, "\x1b[31;1;7m", 2, isatty(STDERR_FILENO) && !getenv("NO_COLOR"));
    fputs("\n", stderr);

    if (getenv("TOMO_STACKTRACE"))
        print_stack_trace(stderr, 1, 3);

    if (ctx->on_err)
        longjmp(*ctx->on_err, 1);

    raise(SIGABRT);
    exit(1);
}

//
// Wrapper for vparser_err
//
__attribute__((noreturn, format(printf, 4, 5)))
static _Noreturn void parser_err(parse_ctx_t *ctx, const char *start, const char *end, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vparser_err(ctx, start, end, fmt, args);
    va_end(args);
}

//
// Convert an escape sequence like \n to a string
//
#pragma GCC diagnostic ignored "-Wstack-protector"
static const char *unescape(parse_ctx_t *ctx, const char **out) {
    const char **endpos = out;
    const char *escape = *out;
    static const char *unescapes[256] = {['a']="\a",['b']="\b",['e']="\x1b",['f']="\f",['n']="\n",['r']="\r",['t']="\t",['v']="\v",['_']=" "};
    assert(*escape == '\\');
    if (unescapes[(int)escape[1]]) {
        *endpos = escape + 2;
        return GC_strdup(unescapes[(int)escape[1]]);
    } else if (escape[1] == 'U' && escape[2] == '[') {
        size_t len = strcspn(&escape[3], "\r\n]");
        if (escape[3+len] != ']')
            parser_err(ctx, escape, escape + 3 + len, "Missing closing ']'");
        char name[len+1] = {};
        memcpy(name, &escape[3], len);
        name[len] = '\0';
        uint32_t codepoint = unicode_name_character(name);
        if (codepoint == UNINAME_INVALID)
            parser_err(ctx, escape, escape + 4 + len,
                       "Invalid unicode codepoint name: \"%s\"", name);
        *endpos = escape + 4 + len;
        char *str = GC_MALLOC_ATOMIC(16);
        size_t u8_len = 16;
        (void)u32_to_u8(&codepoint, 1, (uint8_t*)str, &u8_len);
        str[u8_len] = '\0';
        return str;
    } else if (escape[1] == 'U' && escape[2]) {
        char *endptr = NULL;
        long codepoint = strtol(escape+2, &endptr, 16);
        uint32_t ustr[2] = {codepoint, 0};
        size_t bufsize = 8;
        uint8_t buf[bufsize];
        (void)u32_to_u8(ustr, bufsize, buf, &bufsize);
        *endpos = endptr;
        return GC_strndup((char*)buf, bufsize);
    } else if (escape[1] == 'x' && escape[2] && escape[3]) {
        char *endptr = NULL;
        char c = (char)strtol(escape+2, &endptr, 16);
        *endpos = escape + 4;
        return GC_strndup(&c, 1);
    } else if ('0' <= escape[1] && escape[1] <= '7' && '0' <= escape[2] && escape[2] <= '7' && '0' <= escape[3] && escape[3] <= '7') {
        char *endptr = NULL;
        char c = (char)strtol(escape+1, &endptr, 8);
        *endpos = escape + 4;
        return GC_strndup(&c, 1);
    } else {
        *endpos = escape + 2;
        return GC_strndup(escape+1, 1);
    }
}

PUREFUNC static inline int64_t get_indent(parse_ctx_t *ctx, const char *pos)
{
    int64_t line_num = get_line_number(ctx->file, pos);
    const char *line = get_line(ctx->file, line_num);
    if (*line == ' ') {
        int64_t spaces = (int64_t)strspn(line, " ");
        if (spaces % SPACES_PER_INDENT != 0)
            parser_err(ctx, line + spaces - (spaces % SPACES_PER_INDENT), line + spaces,
                       "Indentation must be a multiple of 4 spaces, not %ld", spaces);
        int64_t indent = spaces / SPACES_PER_INDENT;
        if (line[indent] == '\t')
            parser_err(ctx, line + indent, line + indent + 1, "This is a tab following spaces, and you can't mix tabs and spaces");
        return indent;
    } else if (*line == '\t') {
        int64_t indent = (int64_t)strspn(line, "\t");
        if (line[indent] == ' ')
            parser_err(ctx, line + indent, line + indent + 1, "This is a space following tabs, and you can't mix tabs and spaces");
        return indent;
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

whitespace_types_e whitespace(const char **pos) {
    whitespace_types_e spaces = WHITESPACE_NONE;
    for (;;) {
        if (some_of(pos, " \t")) {
            spaces |= WHITESPACE_SPACES;
        } else if (some_of(pos, "\r\n")) {
            spaces |= WHITESPACE_NEWLINES;
        } else if (comment(pos)) {
            spaces |= WHITESPACE_COMMENTS;
        } else break;
    }
    return spaces;
}

size_t match(const char **pos, const char *target) {
    size_t len = strlen(target);
    if (strncmp(*pos, target, len) != 0)
        return 0;
    *pos += len;
    return len;
}

static inline bool is_xid_continue_next(const char *pos) {
    ucs4_t point = 0;
    u8_next(&point, (const uint8_t*)pos);
    return uc_is_property_xid_continue(point);
}

//
// Expect a string (potentially after whitespace) and emit a parser error if it's not there
//
__attribute__((format(printf, 5, 6)))
static void expect_str(
    parse_ctx_t *ctx, const char *start, const char **pos, const char *target, const char *fmt, ...) {
    spaces(pos);
    if (match(pos, target)) {
        char lastchar = target[strlen(target)-1];
        if (!(isalpha(lastchar) || isdigit(lastchar) || lastchar == '_'))
            return;
        if (!is_xid_continue_next(*pos))
            return;
    }

    if (isatty(STDERR_FILENO) && !getenv("NO_COLOR"))
        fputs("\x1b[31;1;7m", stderr);
    va_list args;
    va_start(args, fmt);
    vparser_err(ctx, start, *pos, fmt, args);
    va_end(args);
}

//
// Helper for matching closing parens with good error messages
//
__attribute__((format(printf, 4, 5)))
static void expect_closing(
    parse_ctx_t *ctx, const char **pos, const char *close_str, const char *fmt, ...) {
    const char *start = *pos;
    spaces(pos);
    if (match(pos, close_str))
        return;

    const char *eol = strchr(*pos, '\n');
    const char *next = strstr(*pos, close_str);

    const char *end = eol < next ? eol : next;

    if (isatty(STDERR_FILENO) && !getenv("NO_COLOR"))
        fputs("\x1b[31;1;7m", stderr);
    va_list args;
    va_start(args, fmt);
    vparser_err(ctx, start, end, fmt, args);
    va_end(args);
}

#define expect(ctx, start, pos, parser, ...) ({ \
    const char **_pos = pos; \
    spaces(_pos); \
    auto _result = parser(ctx, *_pos); \
    if (!_result) { \
        if (isatty(STDERR_FILENO) && !getenv("NO_COLOR")) \
            fputs("\x1b[31;1;7m", stderr); \
        parser_err(ctx, start, *_pos, __VA_ARGS__); \
    } \
    *_pos = _result->end; \
    _result; })

#define optional(ctx, pos, parser) ({ \
    const char **_pos = pos; \
    spaces(_pos); \
    auto _result = parser(ctx, *_pos); \
    if (_result) *_pos = _result->end; \
    _result; })

size_t match_word(const char **out, const char *word) {
    const char *pos = *out;
    spaces(&pos);
    if (!match(&pos, word) || is_xid_continue_next(pos))
        return 0;

    *out = pos;
    return strlen(word);
}

bool match_group(const char **out, char open) {
    static char mirror_delim[256] = {['(']=')', ['{']='}', ['<']='>', ['[']=']'};
    const char *pos = *out;
    if (*pos != open) return 0;
    char close = mirror_delim[(int)open] ? mirror_delim[(int)open] : open;
    int depth = 1;
    for (++pos; *pos && depth > 0; ++pos) {
        if (*pos == close) --depth;
        else if (*pos == open) ++depth;
    }
    if (depth == 0) {
        *out = pos;
        return true;
    } else return false;
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

const char *get_id(const char **inout) {
    const char *pos = *inout;
    const char *word = get_word(&pos);
    if (!word) return word;
    for (int i = 0; keywords[i]; i++)
        if (strcmp(word, keywords[i]) == 0)
            return NULL;
    *inout = pos;
    return word;
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

    if (get_indent(ctx, next_line) != starting_indent + 1)
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
        if ((int64_t)strspn(pos, " ") >= SPACES_PER_INDENT * target) {
            *out = pos + SPACES_PER_INDENT * target;
            return true;
        }
    } else if ((int64_t)strspn(pos, "\t") >= target) {
        *out = pos + target;
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
        return NewAST(ctx->file, start, pos, Num, .n=n, .bits=64);
    } else if (match(&pos, "deg")) {
        double n = strtod(str, NULL) * RADIANS_PER_DEGREE;
        return NewAST(ctx->file, start, pos, Num, .n=n, .bits=64);
    }

    match(&pos, "_");
    auto bits = IBITS_UNSPECIFIED;
    if (match(&pos, "i64")) bits = IBITS64;
    else if (match(&pos, "i32")) bits = IBITS32;
    else if (match(&pos, "i16")) bits = IBITS16;
    else if (match(&pos, "i8")) bits = IBITS8;

    // else if (match(&pos, ".") || match(&pos, "e")) return NULL; // looks like a float

    return NewAST(ctx->file, start, pos, Int, .str=str, .bits=bits);
}

type_ast_t *parse_table_type(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match(&pos, "{")) return NULL;
    whitespace(&pos);
    type_ast_t *key_type = parse_type(ctx, pos);
    if (!key_type) return NULL;
    pos = key_type->end;
    whitespace(&pos);
    if (!match(&pos, ":")) return NULL;
    type_ast_t *value_type = expect(ctx, start, &pos, parse_type, "I couldn't parse the rest of this table type");
    whitespace(&pos);
    expect_closing(ctx, &pos, "}", "I wasn't able to parse the rest of this table type");
    return NewTypeAST(ctx->file, start, pos, TableTypeAST, .key=key_type, .value=value_type);
}

type_ast_t *parse_set_type(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match(&pos, "{")) return NULL;
    whitespace(&pos);
    type_ast_t *item_type = parse_type(ctx, pos);
    if (!item_type) return NULL;
    pos = item_type->end;
    whitespace(&pos);
    if (match(&pos, ":")) return NULL;
    expect_closing(ctx, &pos, "}", "I wasn't able to parse the rest of this set type");
    return NewTypeAST(ctx->file, start, pos, SetTypeAST, .item=item_type);
}

type_ast_t *parse_func_type(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match_word(&pos, "func")) return NULL;
    spaces(&pos);
    if (!match(&pos, "(")) return NULL;
    arg_ast_t *args = parse_args(ctx, &pos, true);
    expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this function type");
    spaces(&pos);
    type_ast_t *ret = match(&pos, "->") ? optional(ctx, &pos, parse_type) : NULL;
    return NewTypeAST(ctx->file, start, pos, FunctionTypeAST, .args=args, .ret=ret);
}

type_ast_t *parse_array_type(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match(&pos, "[")) return NULL;
    type_ast_t *type = expect(ctx, start, &pos, parse_type,
                             "I couldn't parse an array item type after this point");
    expect_closing(ctx, &pos, "]", "I wasn't able to parse the rest of this array type");
    return NewTypeAST(ctx->file, start, pos, ArrayTypeAST, .item=type);
}

type_ast_t *parse_channel_type(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match(&pos, "|")) return NULL;
    type_ast_t *type = expect(ctx, start, &pos, parse_type,
                             "I couldn't parse a channel item type after this point");
    expect_closing(ctx, &pos, "|", "I wasn't able to parse the rest of this channel");
    return NewTypeAST(ctx->file, start, pos, ChannelTypeAST, .item=type);
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
    bool is_readonly = match(&pos, "%");
    spaces(&pos);
    type_ast_t *type = expect(ctx, start, &pos, parse_non_optional_type,
                              "I couldn't parse a pointer type after this point");
    type_ast_t *ptr_type = NewTypeAST(ctx->file, start, pos, PointerTypeAST, .pointed=type, .is_stack=is_stack, .is_readonly=is_readonly);
    spaces(&pos);
    if (match(&pos, "?"))
        return NewTypeAST(ctx->file, start, pos, OptionalTypeAST, .type=ptr_type);
    else
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
        id = heap_strf("%s.%s", id, next_id);
        pos = next;
    }
    return NewTypeAST(ctx->file, start, pos, VarTypeAST, .name=id);
}

type_ast_t *parse_non_optional_type(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    type_ast_t *type = NULL;
    bool success = (false
        || (type=parse_pointer_type(ctx, pos))
        || (type=parse_array_type(ctx, pos))
        || (type=parse_channel_type(ctx, pos))
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
    pos = type->end;
    spaces(&pos);
    if (match(&pos, "?"))
        return NewTypeAST(ctx->file, start, pos, OptionalTypeAST, .type=type);
    else
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
    if (pos[len] == 'e')
        len += 1 + strspn(pos + len + 1, "-0123456789_");
    char *buf = GC_MALLOC_ATOMIC(len+1);
    memset(buf, 0, len+1);
    for (char *src = (char*)pos, *dest = buf; src < pos+len; ++src) {
        if (*src != '_') *(dest++) = *src;
    }
    double d = strtod(buf, NULL);
    pos += len;

    if (negative) d *= -1;

    auto bits = NBITS_UNSPECIFIED;
    match(&pos, "_");
    if (match(&pos, "f64")) bits = NBITS64;
    else if (match(&pos, "f32")) bits = NBITS32;

    if (match(&pos, "%"))
        d /= 100.;
    else if (match(&pos, "deg"))
        d *= RADIANS_PER_DEGREE;

    return NewAST(ctx->file, start, pos, Num, .n=d, .bits=bits);
}

static inline bool match_separator(const char **pos) { // Either comma or newline
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

PARSER(parse_channel) {
    const char *start = pos;
    if (!match(&pos, "|:")) return NULL;
    type_ast_t *item_type = expect(ctx, pos-1, &pos, parse_type, "I couldn't parse a type for this channel");
    ast_t *max_size = NULL;
    if (match(&pos, ";")) {
        whitespace(&pos);
        const char *attr_start = pos;
        if (match_word(&pos, "max_size") && match(&pos, "="))
            max_size = expect(ctx, attr_start, &pos, parse_int, "I expected a maximum size for this channel");
    }
    expect_closing(ctx, &pos, "|", "I wasn't able to parse the rest of this channel");
    return NewAST(ctx->file, start, pos, Channel, .item_type=item_type, .max_size=max_size);
}

PARSER(parse_array) {
    const char *start = pos;
    if (!match(&pos, "[")) return NULL;

    whitespace(&pos);

    ast_list_t *items = NULL;
    type_ast_t *item_type = NULL;
    if (match(&pos, ":")) {
        whitespace(&pos);
        item_type = expect(ctx, pos-1, &pos, parse_type, "I couldn't parse a type for this array");
        whitespace(&pos);
    }

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
    expect_closing(ctx, &pos, "]", "I wasn't able to parse the rest of this array");

    if (!item_type && !items)
        parser_err(ctx, start, pos, "Empty arrays must specify what type they would contain (e.g. [:Int])");

    REVERSE_LIST(items);
    return NewAST(ctx->file, start, pos, Array, .item_type=item_type, .items=items);
}

PARSER(parse_table) {
    const char *start = pos;
    if (!match(&pos, "{")) return NULL;

    whitespace(&pos);

    ast_list_t *entries = NULL;
    type_ast_t *key_type = NULL, *value_type = NULL;
    if (match(&pos, ":")) {
        whitespace(&pos);
        key_type = expect(ctx, pos-1, &pos, parse_type, "I couldn't parse a key type for this table");
        whitespace(&pos);
        if (!match(&pos, ":"))
            return NULL;
        value_type = expect(ctx, pos-1, &pos, parse_type, "I couldn't parse a value type for this table");
        whitespace(&pos);
        match(&pos, ",");
    }

    for (;;) {
        const char *entry_start = pos;
        ast_t *key = optional(ctx, &pos, parse_extended_expr);
        if (!key) break;
        whitespace(&pos);
        if (!match(&pos, ":")) return NULL;
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

    if (!key_type && !value_type && !entries)
        return NULL;

    whitespace(&pos);

    ast_t *fallback = NULL;
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
            } else {
                break;
            }
            whitespace(&pos);
            if (!match(&pos, ";")) break;
        }
    }

    whitespace(&pos);
    expect_closing(ctx, &pos, "}", "I wasn't able to parse the rest of this table");

    return NewAST(ctx->file, start, pos, Table, .key_type=key_type, .value_type=value_type, .entries=entries, .fallback=fallback);
}

PARSER(parse_set) {
    const char *start = pos;
    if (!match(&pos, "{")) return NULL;

    whitespace(&pos);

    ast_list_t *items = NULL;
    type_ast_t *item_type = NULL;
    if (match(&pos, ":")) {
        whitespace(&pos);
        item_type = expect(ctx, pos-1, &pos, parse_type, "I couldn't parse a key type for this set");
        whitespace(&pos);
        if (match(&pos, ":"))
            return NULL;
        whitespace(&pos);
    }

    for (;;) {
        ast_t *item = optional(ctx, &pos, parse_extended_expr);
        if (!item) break;
        whitespace(&pos);
        if (match(&pos, ":")) return NULL;
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

    if (!item_type && !items)
        return NULL;

    whitespace(&pos);
    expect_closing(ctx, &pos, "}", "I wasn't able to parse the rest of this set");

    return NewAST(ctx->file, start, pos, Set, .item_type=item_type, .items=items);
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
    if (dollar) field = heap_strf("$%s", field);
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

PARSER(parse_reduction) {
    const char *start = pos;
    if (!match(&pos, "(")) return NULL;
    
    spaces(&pos);
    const char *combo_start = pos;
    binop_e op = match_binary_operator(&pos);
    if (op == BINOP_UNKNOWN) return NULL;

    ast_t *combination;
    ast_t *lhs = NewAST(ctx->file, pos, pos, Var, .name="$reduction");
    ast_t *rhs = NewAST(ctx->file, pos, pos, Var, .name="$iter_value");
    if (op == BINOP_MIN || op == BINOP_MAX) {
        ast_t *key = NewAST(ctx->file, pos, pos, Var, .name="$");
        for (bool progress = true; progress; ) {
            ast_t *new_term;
            progress = (false
                || (new_term=parse_index_suffix(ctx, key))
                || (new_term=parse_field_suffix(ctx, key))
                || (new_term=parse_method_call_suffix(ctx, key))
                || (new_term=parse_fncall_suffix(ctx, key))
                || (new_term=parse_optional_suffix(ctx, key))
                );
            if (progress) key = new_term;
        }
        if (key->tag == Var) key = NULL;
        else pos = key->end;
        combination = op == BINOP_MIN ?
            NewAST(ctx->file, combo_start, pos, Min, .lhs=lhs, .rhs=rhs, .key=key)
            : NewAST(ctx->file, combo_start, pos, Max, .lhs=lhs, .rhs=rhs, .key=key);
    } else {
        combination = NewAST(ctx->file, combo_start, pos, BinaryOp, .op=op, .lhs=lhs, .rhs=rhs);
    }

    spaces(&pos);
    if (!match(&pos, ")")) return NULL;

    ast_t *iter = optional(ctx, &pos, parse_extended_expr);
    if (!iter) return NULL;
    ast_t *suffixed = parse_comprehension_suffix(ctx, iter);
    while (suffixed) {
        iter = suffixed;
        pos = suffixed->end;
        suffixed = parse_comprehension_suffix(ctx, iter);
    }

    ast_t *fallback = NULL;
    if (match_word(&pos, "else")) {
        expect_str(ctx, start, &pos, ":", "I expected a ':' here");
        fallback = expect(ctx, pos-4, &pos, parse_expr, "I couldn't parse the expression after this 'else'");
    }

    return NewAST(ctx->file, start, pos, Reduction, .iter=iter, .combination=combination, .fallback=fallback);
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
    // <expr> for [<index>,]<var> in <iter> [if <condition>]
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
    }
    return NewAST(ctx->file, start, pos, Comprehension, .expr=expr, .vars=vars, .iter=iter, .filter=filter);
}

ast_t *parse_optional_conditional_suffix(parse_ctx_t *ctx, ast_t *stmt) {
    // <statement> if <condition>
    if (!stmt) return stmt;
    const char *start = stmt->start;
    const char *pos = stmt->end;
    if (!match_word(&pos, "if"))
        return stmt;

    ast_t *condition = expect(ctx, pos-2, &pos, parse_expr, "I expected a condition for this 'if'");
    return NewAST(ctx->file, start, pos, If, .condition=condition, .body=stmt);
}

PARSER(parse_if) {
    // if <condition> <body> [else <body>]
    const char *start = pos;
    int64_t starting_indent = get_indent(ctx, pos);

    if (!match_word(&pos, "if"))
        return NULL;

    ast_t *condition = optional(ctx, &pos, parse_declaration);
    if (!condition)
        condition = expect(ctx, start, &pos, parse_expr, "I expected to find a condition for this 'if'");

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
        ast_t *tag_name;
        ast_list_t *args;
        if (match(&pos, "@")) {
            tag_name = NewAST(ctx->file, pos-1, pos, Var, .name="@");
            spaces(&pos);
            ast_t *arg = optional(ctx, &pos, parse_var);
            args = arg ? new(ast_list_t, .ast=arg) : NULL;
        } else {
            tag_name = expect(ctx, start, &pos, parse_var, "I expected a tag name here");
            spaces(&pos);
            args = NULL;
            if (match(&pos, "(")) {
                for (;;) {
                    whitespace(&pos);
                    ast_t *arg = optional(ctx, &pos, parse_var);
                    if (!arg) break;
                    args = new(ast_list_t, .ast=arg, .next=args);
                    whitespace(&pos);
                    if (!match(&pos, ",")) break;
                }
                whitespace(&pos);
                expect_closing(ctx, &pos, ")", "I was expecting a ')' to finish this pattern's arguments");
                REVERSE_LIST(args);
            }
        }

        tmp = pos;
        if (!match(&tmp, ":"))
            parser_err(ctx, tmp, tmp, "I expected a colon ':' after this clause");
        ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a body for this 'when' clause"); 
        clauses = new(when_clause_t, .tag_name=tag_name, .args=args, .body=body, .next=clauses);
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
    // do: [<indent>] body
    const char *start = pos;
    if (!match_word(&pos, "do")) return NULL;
    ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a body for this 'while'"); 
    return NewAST(ctx->file, start, pos, Block, .statements=Match(body, Block)->statements);
}

PARSER(parse_while) {
    // while condition: [<indent>] body
    const char *start = pos;
    if (!match_word(&pos, "while")) return NULL;

    const char *tmp = pos;
    // Shorthand form: `while when ...`
    if (match_word(&tmp, "when")) {
        ast_t *when = expect(ctx, start, &pos, parse_when, "I expected a 'when' block after this");
        if (!when->__data.When.else_body) when->__data.When.else_body = NewAST(ctx->file, pos, pos, Stop);
        return NewAST(ctx->file, start, pos, While, .body=when);
    }
    ast_t *condition = expect(ctx, start, &pos, parse_expr, "I don't see a viable condition for this 'while'");
    ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a body for this 'while'"); 
    return NewAST(ctx->file, start, pos, While, .condition=condition, .body=body);
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
            || (new_term=parse_field_suffix(ctx, val))) {
            val = new_term;
        } else break;
    }
    pos = val->end;

    ast_t *ast = NewAST(ctx->file, start, pos, HeapAllocate, .value=val);
    ast_t *optional = parse_optional_suffix(ctx, ast);
    if (optional) ast = optional;
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
            || (new_term=parse_field_suffix(ctx, val))) {
            val = new_term;
        } else break;
    }
    pos = val->end;

    ast_t *ast = NewAST(ctx->file, start, pos, StackReference, .value=val);
    ast_t *optional = parse_optional_suffix(ctx, ast);
    if (optional) ast = optional;
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

PARSER(parse_text) {
    // ('"' ... '"' / "'" ... "'" / "`" ... "`")
    // "$" [name] [interp-char] quote-char ... close-quote
    const char *start = pos;
    const char *lang = NULL;

    // Escape sequence, e.g. \r\n
    if (*pos == '\\') {
        CORD cord = CORD_EMPTY;
        do {
            const char *c = unescape(ctx, &pos);
            cord = CORD_cat(cord, c);
            // cord = CORD_cat_char(cord, c);
        } while (*pos == '\\');
        return NewAST(ctx->file, start, pos, TextLiteral, .cord=cord);
    }

    char open_quote, close_quote, open_interp = '$';
    if (match(&pos, "\"")) { // Double quote
        open_quote = '"', close_quote = '"', open_interp = '$';
    } else if (match(&pos, "`")) { // Backtick
        open_quote = '`', close_quote = '`', open_interp = '$';
    } else if (match(&pos, "'")) { // Single quote
        open_quote = '\'', close_quote = '\'', open_interp = '\x03';
    } else if (match(&pos, "$")) { // Customized strings
        lang = get_id(&pos);
        // $"..." or $@"...."
        static const char *interp_chars = "~!@#$%^&*+=\\?";
        if (match(&pos, "$")) { // Disable interpolation with $
            open_interp = '\x03';
        } else if (strchr(interp_chars, *pos)) {
            open_interp = *pos;
            ++pos;
        } else if (*pos == '(') {
            open_interp = '@'; // For shell commands
        }
        static const char *quote_chars = "\"'`|/;([{<";
        if (!strchr(quote_chars, *pos))
            parser_err(ctx, pos, pos+1, "This is not a valid string quotation character. Valid characters are: \"'`|/;([{<");
        open_quote = *pos;
        ++pos;
        close_quote = closing[(int)open_quote] ? closing[(int)open_quote] : open_quote;

        if (!lang && open_quote == '/')
            lang = "Pattern";
        else if (!lang && open_quote == '(')
            lang = "Shell";
    } else {
        return NULL;
    }

    int64_t starting_indent = get_indent(ctx, pos);
    int64_t string_indent = starting_indent + 1;

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
                chunk = NULL;
            }
            ++pos;
            ast_t *interp;
            if (*pos == ' ' || *pos == '\t')
                parser_err(ctx, pos, pos+1, "Whitespace is not allowed before an interpolation here");
            interp = expect(ctx, interp_start, &pos, parse_term_no_suffix, "I expected an interpolation term here");
            chunks = new(ast_list_t, .ast=interp, .next=chunks);
            chunk_start = pos;
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
                parser_err(ctx, pos, strchrnul(pos, '\n'), "This multi-line string should be either indented or have '..' at the front");
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
    expect_closing(ctx, &pos, (char[]){close_quote, 0}, "I was expecting a '%c' to finish this string", close_quote);
    return NewAST(ctx->file, start, pos, TextJoin, .lang=lang, .children=chunks);
}

PARSER(parse_path) {
    // "(" ("~/" / "./" / "../" / "/") ... ")"
    const char *start = pos;

    if (!(match(&pos, "(~/")
          || match(&pos, "(./")
          || match(&pos, "(../")
          || match(&pos, "(/")))
        return NULL;

    const char *chunk_start = start + 1;
    ast_list_t *chunks = NULL;
    CORD chunk_text = CORD_EMPTY;
    int paren_depth = 1;
    while (pos < ctx->file->text + ctx->file->len) {
        switch (*pos) {
        case '\\': {
            ++pos;
            chunk_text = CORD_asprintf("%r%.*s%c", chunk_text, (size_t)(pos - chunk_start), chunk_start, *pos);
            ++pos;
            chunk_start = pos;
            continue;
        }
        case '$': {
            const char *interp_start = pos;

            if (pos > chunk_start)
                chunk_text = CORD_asprintf("%r%.*s", chunk_text, (size_t)(pos - chunk_start), chunk_start);

            if (chunk_text) {
                ast_t *literal = NewAST(ctx->file, chunk_start, pos, TextLiteral, .cord=chunk_text);
                chunks = new(ast_list_t, .ast=literal, .next=chunks);
                chunk_text = CORD_EMPTY;
            }
            ++pos;
            if (*pos == ' ' || *pos == '\t')
                parser_err(ctx, pos, pos+1, "Whitespace is not allowed before an interpolation here");
            ast_t *interp = expect(ctx, interp_start, &pos, parse_term_no_suffix, "I expected an interpolation term here");
            chunks = new(ast_list_t, .ast=interp, .next=chunks);
            chunk_start = pos;
            continue;
        }
        case '(': {
            paren_depth += 1;
            ++pos;
            continue;
        }
        case ')': {
            paren_depth -= 1;
            if (paren_depth == 0)
                goto end_of_path;
            ++pos;
            continue;
        }
        default: ++pos; continue;
        }
    }
  end_of_path:;

    if (pos > chunk_start)
        chunk_text = CORD_asprintf("%r%.*s", chunk_text, (size_t)(pos - chunk_start), chunk_start);

    if (chunk_text != CORD_EMPTY) {
        ast_t *literal = NewAST(ctx->file, chunk_start, pos, TextLiteral, .cord=chunk_text);
        chunks = new(ast_list_t, .ast=literal, .next=chunks);
    }

    expect_closing(ctx, &pos, ")", "I was expecting a ')' to finish this path");

    REVERSE_LIST(chunks);
    return NewAST(ctx->file, start, pos, TextJoin, .lang="Path", .children=chunks);
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
    if (!match_word(&pos, "skip")) return NULL;
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
    if (!match_word(&pos, "stop")) return NULL;
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
    arg_ast_t *args = parse_args(ctx, &pos, false);
    spaces(&pos);
    expect_closing(ctx, &pos, ")", "I was expecting a ')' to finish this anonymous function's arguments");
    ast_t *body = optional(ctx, &pos, parse_block);
    if (!body) body = NewAST(ctx->file, pos, pos, Block, .statements=NULL);
    return NewAST(ctx->file, start, pos, Lambda, .id=ctx->next_lambda_id++, .args=args, .body=body);
}

PARSER(parse_nil) {
    const char *start = pos;
    if (!match(&pos, "!")) return NULL;
    type_ast_t *type = parse_type(ctx, pos);
    if (!type) return NULL;
    return NewAST(ctx->file, start, type->end, Nil, .type=type);
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
        || (term=parse_nil(ctx, pos))
        || (term=parse_num(ctx, pos))
        || (term=parse_int(ctx, pos))
        || (term=parse_negative(ctx, pos))
        || (term=parse_heap_alloc(ctx, pos))
        || (term=parse_stack_reference(ctx, pos))
        || (term=parse_bool(ctx, pos))
        || (term=parse_text(ctx, pos))
        || (term=parse_path(ctx, pos))
        || (term=parse_lambda(ctx, pos))
        || (term=parse_parens(ctx, pos))
        || (term=parse_table(ctx, pos))
        || (term=parse_set(ctx, pos))
        || (term=parse_var(ctx, pos))
        || (term=parse_array(ctx, pos))
        || (term=parse_channel(ctx, pos))
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
    ast_t *term = parse_term_no_suffix(ctx, pos);
    if (!term) return NULL;

    for (bool progress = true; progress; ) {
        ast_t *new_term;
        progress = (false
            || (new_term=parse_index_suffix(ctx, term))
            || (new_term=parse_field_suffix(ctx, term))
            || (new_term=parse_method_call_suffix(ctx, term))
            || (new_term=parse_fncall_suffix(ctx, term))
            || (new_term=parse_optional_suffix(ctx, term))
            );
        if (progress) term = new_term;
    }
    return term;
}

ast_t *parse_method_call_suffix(parse_ctx_t *ctx, ast_t *self) {
    if (!self) return NULL;

    const char *start = self->start;
    const char *pos = self->end;

    spaces(&pos);
    if (!match(&pos, ":")) return NULL;
    const char *fn = get_id(&pos);
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

binop_e match_binary_operator(const char **pos)
{
    switch (**pos) {
    case '+': {
        *pos += 1;
        return match(pos, "+") ? BINOP_CONCAT : BINOP_PLUS;
    }
    case '-': {
        *pos += 1;
        if ((*pos)[0] != ' ' && (*pos)[-2] == ' ') // looks like `fn -5`
            return BINOP_UNKNOWN;
        return BINOP_MINUS;
    }
    case '*': *pos += 1; return BINOP_MULT;
    case '/': *pos += 1; return BINOP_DIVIDE;
    case '^': *pos += 1; return BINOP_POWER;
    case '<':
        *pos += 1;
        if (match(pos, "=")) return BINOP_LE;
        else if (match(pos, ">")) return BINOP_CMP;
        else if (match(pos, "<")) return BINOP_LSHIFT;
        else return BINOP_LT;
    case '>': *pos += 1; return match(pos, "=") ? BINOP_GE : (match(pos, ">") ? BINOP_RSHIFT : BINOP_GT);
    default: {
        if (match(pos, "!=")) return BINOP_NE;
        else if (match(pos, "==") && **pos != '=') return BINOP_EQ;
        else if (match_word(pos, "and")) return BINOP_AND;
        else if (match_word(pos, "or")) return BINOP_OR;
        else if (match_word(pos, "xor")) return BINOP_XOR;
        else if (match_word(pos, "mod1")) return BINOP_MOD1;
        else if (match_word(pos, "mod")) return BINOP_MOD;
        else if (match_word(pos, "_min_")) return BINOP_MIN;
        else if (match_word(pos, "_max_")) return BINOP_MAX;
        else return BINOP_UNKNOWN;
    }
    }
}

static ast_t *parse_infix_expr(parse_ctx_t *ctx, const char *pos, int min_tightness) {
    ast_t *lhs = optional(ctx, &pos, parse_term);
    if (!lhs) return NULL;

    spaces(&pos);
    for (binop_e op; (op=match_binary_operator(&pos)) != BINOP_UNKNOWN && op_tightness[op] >= min_tightness; spaces(&pos)) {
        ast_t *key = NULL;
        if (op == BINOP_MIN || op == BINOP_MAX) {
            key = NewAST(ctx->file, pos, pos, Var, .name="$");
            for (bool progress = true; progress; ) {
                ast_t *new_term;
                progress = (false
                    || (new_term=parse_index_suffix(ctx, key))
                    || (new_term=parse_field_suffix(ctx, key))
                    || (new_term=parse_method_call_suffix(ctx, key))
                    || (new_term=parse_fncall_suffix(ctx, key))
                    || (new_term=parse_optional_suffix(ctx, key))
                    );
                if (progress) key = new_term;
            }
            if (key->tag == Var) key = NULL;
            else pos = key->end;
        }

        spaces(&pos);
        ast_t *rhs = parse_infix_expr(ctx, pos, op_tightness[op] + 1);
        if (!rhs) break;
        pos = rhs->end;
        
        if (op == BINOP_MIN) {
            return NewAST(ctx->file, lhs->start, rhs->end, Min, .lhs=lhs, .rhs=rhs, .key=key);
        } else if (op == BINOP_MAX) {
            return NewAST(ctx->file, lhs->start, rhs->end, Max, .lhs=lhs, .rhs=rhs, .key=key);
        } else {
            lhs = NewAST(ctx->file, lhs->start, rhs->end, BinaryOp, .lhs=lhs, .op=op, .rhs=rhs);
        }
    }
    return lhs;
}

ast_t *parse_expr(parse_ctx_t *ctx, const char *pos) {
    return parse_infix_expr(ctx, pos, 0);
}

PARSER(parse_declaration) {
    const char *start = pos;
    ast_t *var = parse_var(ctx, pos);
    if (!var) return NULL;
    pos = var->end;
    spaces(&pos);
    if (!match(&pos, ":=")) return NULL;
    spaces(&pos);
    ast_t *val = optional(ctx, &pos, parse_extended_expr);
    if (!val) {
        if (optional(ctx, &pos, parse_use))
            parser_err(ctx, start, pos, "'use' statements are only allowed at the top level of a file");
        else
            parser_err(ctx, pos, strchrnul(pos, '\n'), "This is not a valid expression");
    }
    return NewAST(ctx->file, start, pos, Declare, .var=var, .value=val);
}

PARSER(parse_top_declaration) {
    const char *start = pos;
    ast_t *var = parse_var(ctx, pos);
    if (!var) return NULL;
    pos = var->end;
    spaces(&pos);
    if (!match(&pos, ":=")) return NULL;
    spaces(&pos);
    ast_t *val = optional(ctx, &pos, parse_use);
    if (!val) val = optional(ctx, &pos, parse_extended_expr);
    if (!val) parser_err(ctx, pos, strchrnul(pos, '\n'), "This declaration value didn't parse");
    return NewAST(ctx->file, start, pos, Declare, .var=var, .value=val);
}

PARSER(parse_update) {
    const char *start = pos;
    ast_t *lhs = optional(ctx, &pos, parse_expr);
    if (!lhs) return NULL;
    spaces(&pos);
    binop_e op;
    if (match(&pos, "+=")) op = BINOP_PLUS;
    else if (match(&pos, "++=")) op = BINOP_CONCAT;
    else if (match(&pos, "-=")) op = BINOP_MINUS;
    else if (match(&pos, "*=")) op = BINOP_MULT;
    else if (match(&pos, "/=")) op = BINOP_DIVIDE;
    else if (match(&pos, "^=")) op = BINOP_POWER;
    else if (match(&pos, "and=")) op = BINOP_AND;
    else if (match(&pos, "or=")) op = BINOP_OR;
    else if (match(&pos, "xor=")) op = BINOP_XOR;
    else return NULL;
    ast_t *rhs = expect(ctx, start, &pos, parse_extended_expr, "I expected an expression here");
    return NewAST(ctx->file, start, pos, UpdateAssign, .lhs=lhs, .rhs=rhs, .op=op);
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
        || (stmt=parse_say(ctx, pos)))
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
        || (expr=optional(ctx, &pos, parse_do))
        )
        return expr;

    return parse_expr(ctx, pos);
}

PARSER(parse_block) {
    const char *start = pos;
    if (!match(&pos, ":")) return NULL;

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
                    parser_err(ctx, line_start, strchrnul(pos, '\n'), "Struct definitions are only allowed at the top level");
                else if (match_word(&pos, "enum"))
                    parser_err(ctx, line_start, strchrnul(pos, '\n'), "Enum definitions are only allowed at the top level");
                else if (match_word(&pos, "func"))
                    parser_err(ctx, line_start, strchrnul(pos, '\n'), "Function definitions are only allowed at the top level");
                else if (match_word(&pos, "use"))
                    parser_err(ctx, line_start, strchrnul(pos, '\n'), "'use' statements are only allowed at the top level");

                spaces(&pos);
                if (*pos && *pos != '\r' && *pos != '\n')
                    parser_err(ctx, pos, strchrnul(pos, '\n'), "I couldn't parse this line");
                break;
            }
            statements = new(ast_list_t, .ast=stmt, .next=statements);
            whitespace(&pos);

            // Guard against having two valid statements on the same line, separated by spaces (but no newlines):
            if (!memchr(stmt->end, '\n', (size_t)(pos - stmt->end))) {
                if (*pos)
                    parser_err(ctx, pos, strchrnul(pos, '\n'), "I don't know how to parse the rest of this line");
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
            ||(stmt=optional(ctx, &pos, parse_enum_def))
            ||(stmt=optional(ctx, &pos, parse_lang_def))
            ||(stmt=optional(ctx, &pos, parse_func_def))
            ||(stmt=optional(ctx, &pos, parse_use))
            ||(stmt=optional(ctx, &pos, parse_linker))
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
            if (get_indent(ctx, next) > indent && next < strchrnul(next, '\n'))
                parser_err(ctx, next, strchrnul(next, '\n'), "I couldn't parse this namespace declaration");
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
            ||(stmt=optional(ctx, &pos, parse_enum_def))
            ||(stmt=optional(ctx, &pos, parse_lang_def))
            ||(stmt=optional(ctx, &pos, parse_func_def))
            ||(stmt=optional(ctx, &pos, parse_use))
            ||(stmt=optional(ctx, &pos, parse_linker))
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
        parser_err(ctx, pos, strchrnul(pos, '\n'), "I expect all top-level statements to be declarations of some kind");
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

    arg_ast_t *fields = parse_args(ctx, &pos, false);

    whitespace(&pos);
    bool secret = false;
    if (match(&pos, ";")) { // Extra flags
        whitespace(&pos);
        for (;;) {
            if (match_word(&pos, "secret"))
                secret = true;
            else
                break;

            if (!match_separator(&pos))
                break;
        }
    }

    expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this struct");

    ast_t *namespace = NULL;
    if (match(&pos, ":")) {
        const char *ns_pos = pos;
        whitespace(&ns_pos);
        int64_t ns_indent = get_indent(ctx, ns_pos);
        if (ns_indent > starting_indent) {
            pos = ns_pos;
            namespace = optional(ctx, &pos, parse_namespace);
        }
    }
    if (!namespace)
        namespace = NewAST(ctx->file, pos, pos, Block, .statements=NULL);
    return NewAST(ctx->file, start, pos, StructDef, .name=name, .fields=fields, .namespace=namespace, .secret=secret);
}

ast_t *parse_enum_def(parse_ctx_t *ctx, const char *pos) {
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
    int64_t next_value = 0;

    whitespace(&pos);
    for (;;) {
        const char *tag_start = pos;

        spaces(&pos);
        const char *tag_name = get_id(&pos);
        if (!tag_name) break;

        spaces(&pos);
        arg_ast_t *fields;
        bool secret = false;
        if (match(&pos, "(")) {
            whitespace(&pos);
            fields = parse_args(ctx, &pos, false);
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

        spaces(&pos);
        if (match(&pos, "=")) {
            ast_t *val = expect(ctx, tag_start, &pos, parse_int, "I expected an integer literal after this '='");
            Int_t i = Int$from_text(Text$from_str(Match(val, Int)->str), NULL);
            // TODO check for overflow
            next_value = (i.small >> 2);
        }

        // Check for duplicate values:
        for (tag_ast_t *t = tags; t; t = t->next) {
            if (t->value == next_value)
                parser_err(ctx, tag_start, pos, "This tag value (%ld) is a duplicate of an earlier tag value", next_value);
        }

        tags = new(tag_ast_t, .name=tag_name, .value=next_value, .fields=fields, .secret=secret, .next=tags);
        ++next_value;

        if (!match_separator(&pos))
            break;
    }

    whitespace(&pos);
    expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this enum definition");

    REVERSE_LIST(tags);

    ast_t *namespace = NULL;
    if (match(&pos, ":")) {
        const char *ns_pos = pos;
        whitespace(&ns_pos);
        int64_t ns_indent = get_indent(ctx, ns_pos);
        if (ns_indent > starting_indent) {
            pos = ns_pos;
            namespace = optional(ctx, &pos, parse_namespace);
        }
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
    if (match(&pos, ":")) {
        const char *ns_pos = pos;
        whitespace(&ns_pos);
        int64_t ns_indent = get_indent(ctx, ns_pos);
        if (ns_indent > starting_indent) {
            pos = ns_pos;
            namespace = optional(ctx, &pos, parse_namespace);
        }
    }
    if (!namespace)
        namespace = NewAST(ctx->file, pos, pos, Block, .statements=NULL);

    return NewAST(ctx->file, start, pos, LangDef, .name=name, .namespace=namespace);
}

arg_ast_t *parse_args(parse_ctx_t *ctx, const char **pos, bool allow_unnamed)
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
            const char *name_start = *pos;
            const char *name = get_id(pos);
            if (!name) break;
            whitespace(pos);
            if (strncmp(*pos, "==", 2) != 0 && match(pos, "=")) {
                default_val = expect(ctx, *pos-1, pos, parse_term, "I expected a value after this '='");
                names = new(name_list_t, .name=name, .next=names);
                break;
            } else if (match(pos, ":")) {
                type = expect(ctx, *pos-1, pos, parse_type, "I expected a type here");
                names = new(name_list_t, .name=name, .next=names);
                break;
            } else if (allow_unnamed) {
                *pos = name_start;
                type = optional(ctx, pos, parse_type);
                if (type)
                    names = new(name_list_t, .name=NULL, .next=names);
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
            parser_err(ctx, batch_start, *pos, "I expected a ':' and type, or '=' and a default value after this parameter (%s)",
                       names->name);

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

    if (!match(&pos, "(")) return NULL;

    arg_ast_t *args = parse_args(ctx, &pos, false);
    whitespace(&pos);
    bool is_inline = false;
    ast_t *cache_ast = NULL;
    for (bool specials = match(&pos, ";"); specials; specials = match_separator(&pos)) {
        const char *flag_start = pos;
        if (match_word(&pos, "inline")) {
            is_inline = true;
        } else if (match_word(&pos, "cached")) {
            if (!cache_ast) cache_ast = NewAST(ctx->file, pos, pos, Int, .str="-1", .bits=0);
        } else if (match_word(&pos, "cache_size")) {
            whitespace(&pos);
            if (!match(&pos, "="))
                parser_err(ctx, flag_start, pos, "I expected a value for 'cache_size'");
            whitespace(&pos);
            cache_ast = expect(ctx, start, &pos, parse_expr, "I expected a maximum size for the cache");
        }
    }
    expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this function definition");

    type_ast_t *ret_type = NULL;
    spaces(&pos);
    if (match(&pos, "->"))
        ret_type = optional(ctx, &pos, parse_type);

    ast_t *body = expect(ctx, start, &pos, parse_block,
                             "This function needs a body block");
    return NewAST(ctx->file, start, pos, FunctionDef,
                  .name=name, .args=args, .ret_type=ret_type, .body=body, .cache=cache_ast,
                  .is_inline=is_inline);
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
    if (!match_word(&pos, "inline")) return NULL;
    spaces(&pos);
    if (!match_word(&pos, "C")) return NULL;
    spaces(&pos);
    char open = *pos;
    if (!match(&pos, "(") && !match(&pos, "{"))
        parser_err(ctx, start, pos, "I expected a '(' or '{' here");

    int64_t indent = get_indent(ctx, pos);
    whitespace(&pos);
    // Block:
    CORD c_code = CORD_EMPTY;
    while (get_indent(ctx, pos) > indent) {
        size_t line_len = strcspn(pos, "\r\n");
        c_code = CORD_all(c_code, GC_strndup(pos, line_len), "\n");
        pos += line_len;
        if (whitespace(&pos) == WHITESPACE_NONE) break;
    }
    expect_closing(ctx, &pos, open == '(' ? ")" : "}", "I wasn't able to parse the rest of this inline C");
    spaces(&pos);
    type_ast_t *type = NULL;
    if (open == '(') {
        if (!match(&pos, ":"))
            parser_err(ctx, start, pos, "This inline C needs to have a type after it");
        type = expect(ctx, start, &pos, parse_type, "I couldn't parse the type for this extern");
    }
    return NewAST(ctx->file, start, pos, InlineCCode, .code=c_code, .type=type);
}

PARSER(parse_doctest) {
    const char *start = pos;
    if (!match(&pos, ">>")) return NULL;
    spaces(&pos);
    ast_t *expr = expect(ctx, start, &pos, parse_statement, "I couldn't parse the expression for this doctest");
    whitespace(&pos);
    const char* output = NULL;
    if (match(&pos, "=")) {
        spaces(&pos);
        const char *output_start = pos,
                   *output_end = pos + strcspn(pos, "\r\n");
        if (output_end <= output_start)
            parser_err(ctx, output_start, output_end, "You're missing expected output here");
        int64_t trailing_spaces = 0;
        while (output_end - trailing_spaces - 1 > output_start && (output_end[-trailing_spaces-1] == ' ' || output_end[-trailing_spaces-1] == '\t'))
            ++trailing_spaces;
        output = GC_strndup(output_start, (size_t)((output_end - output_start) - trailing_spaces));
        pos = output_end;
    } else {
        pos = expr->end;
    }
    return NewAST(ctx->file, start, pos, DocTest, .expr=expr, .output=output);
}

PARSER(parse_say) {
    const char *start = pos;
    if (!match(&pos, "!!")) return NULL;
    spaces(&pos);

    ast_list_t *chunks = NULL;
    CORD chunk = CORD_EMPTY;
    const char *chunk_start = pos;
    const char open_interp = '$';
    while (pos < ctx->file->text + ctx->file->len) {
        if (*pos == open_interp) { // Interpolation
            const char *interp_start = pos;
            if (chunk) {
                ast_t *literal = NewAST(ctx->file, chunk_start, pos, TextLiteral, .cord=chunk);
                chunks = new(ast_list_t, .ast=literal, .next=chunks);
                chunk = NULL;
            }
            ++pos;
            ast_t *interp;
            if (*pos == ' ' || *pos == '\t')
                parser_err(ctx, pos, pos+1, "Whitespace is not allowed before an interpolation here");
            interp = expect(ctx, interp_start, &pos, parse_term, "I expected an interpolation term here");
            chunks = new(ast_list_t, .ast=interp, .next=chunks);
            chunk_start = pos;
        } else if (*pos == '\r' || *pos == '\n') { // Newline
            break;
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
    return NewAST(ctx->file, start, pos, PrintStatement, .to_print=chunks);
}

PARSER(parse_use) {
    const char *start = pos;
    if (!match_word(&pos, "use")) return NULL;
    spaces(&pos);
    size_t name_len = strcspn(pos, " \t\r\n;");
    if (name_len < 1)
        parser_err(ctx, start, pos, "There is no module name here to use");
    char *name = GC_strndup(pos, name_len);
    pos += name_len;
    while (match(&pos, ";")) continue;
    int what; 
    if (name[0] == '<')
        what = USE_HEADER;
    else if (starts_with(name, "./") || starts_with(name, "/") || starts_with(name, "../") || starts_with(name, "~/"))
        what = USE_LOCAL;
    else if (ends_with(name, ".so"))
        what = USE_SHARED_OBJECT;
    else
        what = USE_MODULE;
    return NewAST(ctx->file, start, pos, Use, .path=name, .what=what);
}

PARSER(parse_linker) {
    const char *start = pos;
    if (!match_word(&pos, "!link")) return NULL;
    spaces(&pos);
    size_t len = strcspn(pos, "\r\n");
    const char *directive = GC_strndup(pos, len);
    pos += len;
    return NewAST(ctx->file, start, pos, LinkerDirective, .directive=directive);
}

ast_t *parse_file(const char *path, jmp_buf *on_err) {
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
        uint32_t i = arc4random_uniform(cached.entries.length);
        struct {const char *path; ast_t *ast; } *to_remove = Table$entry(cached, i+1);
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

ast_t *parse_expression_str(const char *str) {
    file_t *file = spoof_file("<expression>", str);
    parse_ctx_t ctx = {
        .file=file,
        .on_err=NULL,
        .next_lambda_id=0,
    };

    const char *pos = file->text;
    whitespace(&pos);
    ast_t *ast = parse_extended_expr(&ctx, pos);
    if (!ast) return ast;
    pos = ast->end;
    whitespace(&pos);
    if (strlen(pos) > 0) {
        parser_err(&ctx, pos, pos + strlen(pos), "I couldn't parse this part of the expression");
    }
    return ast;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
