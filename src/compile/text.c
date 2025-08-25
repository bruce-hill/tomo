// This file defines how to compile text

#include <ctype.h>

#include "../ast.h"
#include "../environment.h"
#include "../naming.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/tables.h"
#include "../stdlib/text.h"
#include "../typecheck.h"
#include "../types.h"
#include "compilation.h"

public
Text_t expr_as_text(Text_t expr, type_t *t, Text_t color) {
    switch (t->tag) {
    case MemoryType: return Texts("Memory$as_text(stack(", expr, "), ", color, ", &Memory$info)");
    case BoolType:
        // NOTE: this cannot use stack(), since bools may actually be bit
        // fields:
        return Texts("Bool$as_text((Bool_t[1]){", expr, "}, ", color, ", &Bool$info)");
    case CStringType: return Texts("CString$as_text(stack(", expr, "), ", color, ", &CString$info)");
    case BigIntType:
    case IntType:
    case ByteType:
    case NumType: {
        Text_t name = type_to_text(t);
        return Texts(name, "$as_text(stack(", expr, "), ", color, ", &", name, "$info)");
    }
    case TextType: return Texts("Text$as_text(stack(", expr, "), ", color, ", ", compile_type_info(t), ")");
    case ListType: return Texts("List$as_text(stack(", expr, "), ", color, ", ", compile_type_info(t), ")");
    case SetType: return Texts("Table$as_text(stack(", expr, "), ", color, ", ", compile_type_info(t), ")");
    case TableType: return Texts("Table$as_text(stack(", expr, "), ", color, ", ", compile_type_info(t), ")");
    case FunctionType:
    case ClosureType: return Texts("Func$as_text(stack(", expr, "), ", color, ", ", compile_type_info(t), ")");
    case PointerType: return Texts("Pointer$as_text(stack(", expr, "), ", color, ", ", compile_type_info(t), ")");
    case OptionalType: return Texts("Optional$as_text(stack(", expr, "), ", color, ", ", compile_type_info(t), ")");
    case StructType:
    case EnumType: return Texts("generic_as_text(stack(", expr, "), ", color, ", ", compile_type_info(t), ")");
    default: compiler_err(NULL, NULL, NULL, "Stringifying is not supported for ", type_to_str(t));
    }
    return EMPTY_TEXT;
}

public
Text_t compile_text(env_t *env, ast_t *ast, Text_t color) {
    type_t *t = get_type(env, ast);
    Text_t expr = compile(env, ast);
    return expr_as_text(expr, t, color);
}

public
Text_t compile_text_literal(Text_t literal) {
    Text_t code = Text("\"");
    const char *utf8 = Text$as_c_string(literal);
    for (const char *p = utf8; *p; p++) {
        switch (*p) {
        case '\\': code = Texts(code, "\\\\"); break;
        case '"': code = Texts(code, "\\\""); break;
        case '\a': code = Texts(code, "\\a"); break;
        case '\b': code = Texts(code, "\\b"); break;
        case '\n': code = Texts(code, "\\n"); break;
        case '\r': code = Texts(code, "\\r"); break;
        case '\t': code = Texts(code, "\\t"); break;
        case '\v': code = Texts(code, "\\v"); break;
        default: {
            if (isprint(*p)) {
                code = Texts(code, Text$from_strn(p, 1));
            } else {
                uint8_t byte = *(uint8_t *)p;
                code = Texts(code, "\\x", String(hex(byte, .no_prefix = true, .uppercase = true, .digits = 2)), "\"\"");
            }
            break;
        }
        }
    }
    return Texts(code, "\"");
}

PUREFUNC static bool string_literal_is_all_ascii(Text_t literal) {
    TextIter_t state = NEW_TEXT_ITER_STATE(literal);
    for (int64_t i = 0; i < literal.length; i++) {
        int32_t g = Text$get_grapheme_fast(&state, i);
        if (g < 0 || g > 127 || !isascii(g)) return false;
    }
    return true;
}

public
Text_t compile_text_ast(env_t *env, ast_t *ast) {
    if (ast->tag == TextLiteral) {
        Text_t literal = Match(ast, TextLiteral)->text;
        if (literal.length == 0) return Text("EMPTY_TEXT");

        if (string_literal_is_all_ascii(literal)) return Texts("Text(", compile_text_literal(literal), ")");
        else return Texts("Text$from_str(", compile_text_literal(literal), ")");
    }

    const char *lang = Match(ast, TextJoin)->lang;
    Text_t colorize = Match(ast, TextJoin)->colorize ? Text("yes") : Text("no");

    type_t *text_t = lang ? Table$str_get(*env->types, lang) : TEXT_TYPE;
    if (!text_t || text_t->tag != TextType) code_err(ast, quoted(lang), " is not a valid text language name");

    Text_t lang_constructor;
    if (!lang || streq(lang, "Text")) lang_constructor = Text("Text");
    else
        lang_constructor = namespace_name(Match(text_t, TextType)->env, Match(text_t, TextType)->env->namespace->parent,
                                          Text$from_str(lang));

    ast_list_t *chunks = Match(ast, TextJoin)->children;
    if (!chunks) {
        return Texts(lang_constructor, "(\"\")");
    } else if (!chunks->next && chunks->ast->tag == TextLiteral) {
        Text_t literal = Match(chunks->ast, TextLiteral)->text;
        if (string_literal_is_all_ascii(literal))
            return Texts(lang_constructor, "(", compile_text_literal(literal), ")");
        return Texts("((", compile_type(text_t), ")", compile(env, chunks->ast), ")");
    } else {
        Text_t code = EMPTY_TEXT;
        for (ast_list_t *chunk = chunks; chunk; chunk = chunk->next) {
            Text_t chunk_code;
            type_t *chunk_t = get_type(env, chunk->ast);
            if (chunk->ast->tag == TextLiteral || type_eq(chunk_t, text_t)) {
                chunk_code = compile(env, chunk->ast);
            } else {
                binding_t *constructor =
                    get_constructor(env, text_t, new (arg_ast_t, .value = chunk->ast),
                                    env->current_type != NULL && type_eq(env->current_type, text_t));
                if (constructor) {
                    arg_t *arg_spec = Match(constructor->type, FunctionType)->args;
                    arg_ast_t *args = new (arg_ast_t, .value = chunk->ast);
                    chunk_code = Texts(constructor->code, "(", compile_arguments(env, ast, arg_spec, args), ")");
                } else if (type_eq(text_t, TEXT_TYPE)) {
                    if (chunk_t->tag == TextType) chunk_code = compile(env, chunk->ast);
                    else chunk_code = compile_text(env, chunk->ast, colorize);
                } else {
                    code_err(chunk->ast, "I don't know how to convert ", type_to_str(chunk_t), " to ",
                             type_to_str(text_t));
                }
            }
            code = Texts(code, chunk_code);
            if (chunk->next) code = Texts(code, ", ");
        }
        if (chunks->next) return Texts(lang_constructor, "s(", code, ")");
        else return code;
    }
}
