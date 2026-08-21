// This file defines utility functions for autoformatting code

#include <stdbool.h>
#include <stdint.h>

#include "../ast.h"
#include "../parse/context.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/files.h"
#include "../stdlib/optionals.h"
#include "../stdlib/tables.h"
#include "../stdlib/text.h"
#include "formatter.h"
#include "utils.h"

const Text_t single_indent = Text("    ");

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

PUREFUNC
static ast_t *nl_deciding_ast(ast_t *ast) {
    switch (ast->tag) {
    case DebugLog: {
        ast_list_t *values = Match(ast, DebugLog)->values;
        while (values && values->next)
            values = values->next;
        return values ? values->ast : ast;
    }
    case Declare: {
        // A bare `x : T` declaration has no value; fall back to the statement
        // itself rather than answering NULL, which every caller dereferences.
        ast_t *value = Match(ast, Declare)->value;
        return value ? value : ast;
    }
    case Assign: {
        ast_list_t *values = Match(ast, Assign)->values;
        while (values && values->next)
            values = values->next;
        return values ? values->ast : ast;
    }
    default: return ast;
    }
}

CONSTFUNC int suggested_blank_lines(ast_t *first, ast_t *second) {
    if (first == NULL || second == NULL) return 0;

    int64_t first_end_line = get_line_number(first->file, first->end);
    int64_t second_start_line = get_line_number(second->file, second->start);

    if (second_start_line > first_end_line + 1) return 1;

    first = nl_deciding_ast(first);
    second = nl_deciding_ast(second);

    switch (first->tag) {
    case If:
    case Match:
    case Repeat:
    case While:
    case For:
    case Block:
    case Defer:
    case ConvertDef:
    case FunctionDef:
    case Lambda:
    case StructDef:
    case EnumDef:
    case LangDef: return 1;
    case Use: {
        if (second->tag != Use) return 1;
        break;
    }
    default: break;
    }

    switch (second->tag) {
    case If:
    case Match:
    case Repeat:
    case While:
    case For:
    case Block:
    case Defer:
    case ConvertDef:
    case FunctionDef:
    case Lambda:
    case StructDef:
    case EnumDef:
    case LangDef: return 1;
    default: break;
    }
    return 0;
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

OptionalText_t termify_inline(ast_t *ast, Table_t comments) {
    if (range_has_comment(ast->start, ast->end, comments)) return NONE_TEXT;
    switch (ast->tag) {
    case BINOP_CASES:
    case Not:
    case Negative:
    case HeapAllocate:
    case If:
    case Match:
    case StackReference: return parenthesize(format_inline_code(ast, comments), EMPTY_TEXT);
    default: return format_inline_code(ast, comments);
    }
}

Text_t termify(ast_t *ast, Table_t comments, Text_t indent) {
    switch (ast->tag) {
    case BINOP_CASES:
    case Not:
    case Negative:
    case HeapAllocate:
    case If:
    case Match:
    case StackReference: return parenthesize(format_code(ast, comments, indent), indent);
    default: {
        Text_t inlined = format_inline_code(ast, comments);
        return (inlined.tag != TEXT_NONE && indent.length + inlined.length <= MAX_WIDTH)
                   ? inlined
                   : parenthesize(format_code(ast, comments, indent), indent);
    }
    }
}

static visit_behavior_t _find_required_multiline(ast_t *ast, void *userdata) {
    if (ast->tag == TextJoin && memchr(ast->start, '\n', (size_t)(ast->end - ast->start))) {
        *(bool *)userdata = true;
        return VISIT_STOP;
    }
    return VISIT_PROCEED;
}

// Under certain circumstances, we really don't want to use an inline value, such
// as when there's a multiline string in the source code (we want to preserve that).
bool requires_multiline(ast_t *ast) {
    bool required = false;
    ast_visit(ast, _find_required_multiline, &required);
    return required;
}
