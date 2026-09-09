// This file defines utility functions for autoformatting code

#include <stdbool.h>
#include <stdint.h>

#include "../ast.h"
#include "../parse/context.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/files.h"
#include "../stdlib/optionals.h"
#include "../stdlib/table.h"
#include "../stdlib/text.h"
#include "formatter.h"
#include "utils.h"

const Text_t single_indent = Text("    ");

void add_flag(flag_list_t *flags, bool present, Text_t name) {
    if (!present) return;
    assert(flags->count < (int)(sizeof(flags->items) / sizeof(flags->items[0])));
    flags->items[flags->count++] = name;
}

Text_t inline_flags(flag_list_t flags) {
    Text_t code = EMPTY_TEXT;
    for (int i = 0; i < flags.count; i++)
        code = Texts(code, "; ", flags.items[i]);
    return code;
}

void add_line(Text_t *code, Text_t line, Text_t indent) {
    if (code->length == 0) {
        *code = line;
    } else {
        if (line.length > 0) *code = Texts(*code, "\n", indent, line);
        else *code = Texts(*code, "\n");
    }
}

OptionalText_t next_comment(Table_t comments, const char **pos, const char *end) {
    for (const char *p = *pos; p < end; p++) {
        const char **comment_end = Table$get(comments, &p, parse_comments_info);
        if (comment_end) {
            *pos = *comment_end;
            return Text$from_strn(p, (size_t)(*comment_end - p));
        }
    }
    return NONE_TEXT;
}

bool range_has_comment(const char *start, const char *end, Table_t comments) {
    OptionalText_t comment = next_comment(comments, &start, end);
    return (comment.tag != TEXT_NONE);
}

// True if the source between two positions contains a blank line: a line with
// nothing but whitespace on it. This is how the formatter decides where the
// author put blank lines, so that formatting can preserve them.
PUREFUNC
static bool has_blank_line(const char *start, const char *end) {
    bool seen_newline = false, only_space = true;
    for (const char *p = start; p < end; p++) {
        if (*p == '\n') {
            if (seen_newline && only_space) return true;
            seen_newline = true;
            only_space = true;
        } else if (*p != ' ' && *p != '\t' && *p != '\r') {
            only_space = false;
        }
    }
    return false;
}

// How many blank lines belong between two consecutive statements. The
// formatter keeps the blank lines the author wrote (collapsing runs of them
// down to one) and only insists on a blank line of its own after a function
// definition. The other rule -- a blank line after a body that nests two
// levels deep -- depends on how the statement came out once formatted, so it
// lives in the block formatter rather than here.
PUREFUNC int suggested_blank_lines(ast_t *first, ast_t *second) {
    if (first == NULL || second == NULL) return 0;

    if (has_blank_line(first->end, second->start)) return 1;

    switch (first->tag) {
    case FunctionDef:
    case ConvertDef: return 1;
    default: return 0;
    }
}

Text_t indent_code(Text_t code) {
    if (code.length <= 0) return code;
    return Texts(single_indent, Text$replace(code, Text("\n"), Texts("\n", single_indent)));
}

Text_t parenthesize(Text_t code, Text_t indent) {
    if (Text$has(code, Text("\n"))) return Texts("(\n", indent, indent_code(code), "\n", indent, ")");
    else return Texts("(", code, ")");
}

CONSTFUNC ast_t *unwrap_block(ast_t *ast) {
    if (ast == NULL) return NULL;
    while (ast->tag == Block && Match(ast, Block)->statements && Match(ast, Block)->statements->next == NULL) {
        ast = Match(ast, Block)->statements->ast;
    }
    if (ast->tag == Block && Match(ast, Block)->statements == NULL) return NULL;
    return ast;
}

// Whether the final line of `code` is indented further than `indent`, i.e. the
// text ends inside an indented block rather than back at its own level.
PUREFUNC bool ends_deeper_than(Text_t code, Text_t indent) {
    List_t lines = Text$lines(code);
    if (lines.length <= 1) return false;
    Text_t last = *(Text_t *)(lines.data + ((int64_t)lines.length - 1) * lines.stride);
    Text_t body = Text$trim(last, Text(" \t"), true, false);
    return (int64_t)last.length - (int64_t)body.length > (int64_t)indent.length;
}

// Whether this expression needs parentheses to be a self-contained term that
// a suffix or prefix can attach to. Anything is_operation() covers does (which
// takes in a literal with a folded `-`: `(-2).abs()` is not `-2.abs()`), and
// so do the prefixed and multi-clause forms below.
static PUREFUNC bool needs_parens_as_term(ast_t *ast) {
    if (is_operation(ast)) return true;
    switch (ast->tag) {
    case Not:
    case HeapAllocate:
    case If:
    case Lambda:
    case Match:
    case StackReference: return true;
    default: return false;
    }
}

// An inline `if`/`match` runs on through whatever follows it, so wherever the
// surrounding syntax has to resume afterwards -- an argument list's comma, a
// table entry's `:`, the value of a `return`, which the postfix `x if c` would
// otherwise claim -- it needs parentheses to end where it means to.
OptionalText_t bounded_inline(ast_t *ast, Table_t comments) {
    if (ast->tag == If || ast->tag == Match) return parenthesize(must(format_inline_code(ast, comments)), EMPTY_TEXT);
    return format_inline_code(ast, comments);
}

// The same, for a sub-expression that has to be written across several lines.
// The block form runs on just as the one-line form does: the `,` after a list
// item, or a table entry's `:`, lands inside the last clause of a bare
// `if`/`match` rather than after it.
Text_t bounded_at(ast_t *ast, Table_t comments, Text_t indent, int64_t column) {
    // The parentheses take a column of their own on the line.
    if (ast->tag == If || ast->tag == Match)
        return parenthesize(format_code_at(ast, comments, indent, column + 1), indent);
    return format_code_at(ast, comments, indent, column);
}

OptionalText_t termify_inline(ast_t *ast, Table_t comments) {
    if (range_has_comment(ast->start, ast->end, comments)) return NONE_TEXT;
    if (needs_parens_as_term(ast)) return parenthesize(must(format_inline_code(ast, comments)), EMPTY_TEXT);
    return format_inline_code(ast, comments);
}

Text_t termify_at(ast_t *ast, Table_t comments, Text_t indent, int64_t column) {
    if (needs_parens_as_term(ast)) return parenthesize(format_code_at(ast, comments, indent, column + 1), indent);
    Text_t inlined = format_inline_code(ast, comments);
    if (inlined.tag != TEXT_NONE && column + inlined.length <= MAX_WIDTH) return inlined;
    // A rendering spread over several lines that ends back at its own level
    // closed with a delimiter of its own -- `[`...`]`, `f(`...`)` -- and is a
    // term already. One that ends deeper trailed off into an indented block,
    // and needs the parentheses to say where it stopped.
    Text_t code = format_code_at(ast, comments, indent, column);
    return ends_deeper_than(code, indent) ? parenthesize(code, indent) : code;
}
