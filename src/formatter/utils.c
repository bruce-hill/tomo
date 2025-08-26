// This file defines utility functions for autoformatting code

#include <stdbool.h>
#include <stdint.h>

#include "../ast.h"
#include "../parse/context.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/optionals.h"
#include "../stdlib/tables.h"
#include "../stdlib/text.h"

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

CONSTFUNC bool should_have_blank_line(ast_t *ast) {
    switch (ast->tag) {
    case If:
    case When:
    case Repeat:
    case While:
    case For:
    case Block:
    case FunctionDef:
    case StructDef:
    case EnumDef:
    case LangDef:
    case ConvertDef:
    case Extend: return true;
    default: return false;
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
    return ast;
}

CONSTFUNC const char *binop_tomo_operator(ast_e tag) {
    switch (tag) {
    case Power: return "^";
    case PowerUpdate: return "^=";
    case Concat: return "++";
    case ConcatUpdate: return "++=";
    case Multiply: return "*";
    case MultiplyUpdate: return "*=";
    case Divide: return "/";
    case DivideUpdate: return "/=";
    case Mod: return "mod";
    case ModUpdate: return "mod=";
    case Mod1: return "mod1";
    case Mod1Update: return "mod1=";
    case Plus: return "+";
    case PlusUpdate: return "+=";
    case Minus: return "-";
    case MinusUpdate: return "-=";
    case LeftShift: return "<<";
    case LeftShiftUpdate: return "<<=";
    case RightShift: return ">>";
    case RightShiftUpdate: return ">>=";
    case And: return "and";
    case AndUpdate: return "and=";
    case Or: return "or";
    case OrUpdate: return "or=";
    case Xor: return "xor";
    case XorUpdate: return "xor=";
    case Equals: return "==";
    case NotEquals: return "!=";
    case LessThan: return "<";
    case LessThanOrEquals: return "<=";
    case GreaterThan: return ">";
    case GreaterThanOrEquals: return ">=";
    default: return NULL;
    }
}
