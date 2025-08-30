// This file defines utility functions for autoformatting code

#include <stdbool.h>
#include <stdint.h>

#include "../ast.h"
#include "../parse/context.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/optionals.h"
#include "../stdlib/tables.h"
#include "../stdlib/text.h"
#include "formatter.h"

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
            return Text$from_strn(p, (int64_t)(*comment_end - p));
        }
    }
    return NONE_TEXT;
}

bool range_has_comment(const char *start, const char *end, Table_t comments) {
    OptionalText_t comment = next_comment(comments, &start, end);
    return (comment.length >= 0);
}

CONSTFUNC int suggested_blank_lines(ast_t *first, ast_t *second) {
    if (second == NULL) return 0;

    if (first->tag == Use && second->tag != Use) return 1;

    switch (first->tag) {
    case If:
    case When:
    case Repeat:
    case While:
    case For:
    case Block:
    case Defer:
    case ConvertDef:
    case FunctionDef:
    case StructDef:
    case EnumDef:
    case LangDef:
    case Extend: return 1;
    default: break;
    }

    switch (second->tag) {
    case If:
    case When:
    case Repeat:
    case While:
    case For:
    case Block:
    case Defer:
    case ConvertDef:
    case FunctionDef:
    case StructDef:
    case EnumDef:
    case LangDef:
    case Extend: return 1;
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
    case StackReference: return parenthesize(format_code(ast, comments, indent), indent);
    default: return format_inline_code(ast, comments);
    }
}
