// This file defines how to compile optionals and null

#include "../environment.h"
#include "../naming.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "../types.h"
#include "compilation.h"

Text_t optional_into_nonnone(type_t *t, Text_t value) {
    if (t->tag == OptionalType) t = Match(t, OptionalType)->type;
    switch (t->tag) {
    case IntType:
    case ByteType: return Texts(value, ".value");
    case StructType:
        if (t == PATH_TYPE || t == PATH_TYPE_TYPE) return value;
        return Texts(value, ".value");
    default: return value;
    }
}

public
Text_t promote_to_optional(type_t *t, Text_t code) {
    if (t == PATH_TYPE || t == PATH_TYPE_TYPE) {
        return code;
    } else if (t->tag == IntType) {
        switch (Match(t, IntType)->bits) {
        case TYPE_IBITS8: return Texts("((OptionalInt8_t){.value=", code, "})");
        case TYPE_IBITS16: return Texts("((OptionalInt16_t){.value=", code, "})");
        case TYPE_IBITS32: return Texts("((OptionalInt32_t){.value=", code, "})");
        case TYPE_IBITS64: return Texts("((OptionalInt64_t){.value=", code, "})");
        default: errx(1, "Unsupported in type: %s", type_to_str(t));
        }
        return code;
    } else if (t->tag == ByteType) {
        return Texts("((OptionalByte_t){.value=", code, "})");
    } else if (t->tag == StructType) {
        return Texts("({ ", compile_type(Type(OptionalType, .type = t)), " nonnull = {.value=", code,
                     "}; nonnull.is_none = false; nonnull; })");
    } else {
        return code;
    }
}

public
Text_t compile_none(type_t *t) {
    if (t == NULL) compiler_err(NULL, NULL, NULL, "I can't compile a `none` value with no type");

    if (t->tag == OptionalType) t = Match(t, OptionalType)->type;

    if (t == NULL) compiler_err(NULL, NULL, NULL, "I can't compile a `none` value with no type");

    if (t == PATH_TYPE) return Text("NONE_PATH");
    else if (t == PATH_TYPE_TYPE) return Text("((OptionalPathType_t){})");

    switch (t->tag) {
    case BigIntType: return Text("NONE_INT");
    case IntType: {
        switch (Match(t, IntType)->bits) {
        case TYPE_IBITS8: return Text("NONE_INT8");
        case TYPE_IBITS16: return Text("NONE_INT16");
        case TYPE_IBITS32: return Text("NONE_INT32");
        case TYPE_IBITS64: return Text("NONE_INT64");
        default: errx(1, "Invalid integer bit size");
        }
        break;
    }
    case BoolType: return Text("NONE_BOOL");
    case ByteType: return Text("NONE_BYTE");
    case ListType: return Text("NONE_LIST");
    case TableType: return Text("NONE_TABLE");
    case SetType: return Text("NONE_TABLE");
    case TextType: return Text("NONE_TEXT");
    case CStringType: return Text("NULL");
    case PointerType: return Texts("((", compile_type(t), ")NULL)");
    case ClosureType: return Text("NONE_CLOSURE");
    case NumType: return Text("nan(\"none\")");
    case StructType: return Texts("((", compile_type(Type(OptionalType, .type = t)), "){.is_none=true})");
    case EnumType: {
        env_t *enum_env = Match(t, EnumType)->env;
        return Texts("((", compile_type(t), "){", namespace_name(enum_env, enum_env->namespace, Text("none")), "})");
    }
    default: compiler_err(NULL, NULL, NULL, "none isn't implemented for this type: ", type_to_str(t));
    }
    return EMPTY_TEXT;
}

public
Text_t check_none(type_t *t, Text_t value) {
    t = Match(t, OptionalType)->type;
    // NOTE: these use statement expressions ({...;}) because some compilers
    // complain about excessive parens around equality comparisons
    if (t->tag == PointerType || t->tag == FunctionType || t->tag == CStringType) return Texts("(", value, " == NULL)");
    else if (t == PATH_TYPE) return Texts("((", value, ").type.$tag == PATH_NONE)");
    else if (t == PATH_TYPE_TYPE) return Texts("((", value, ").$tag == PATH_NONE)");
    else if (t->tag == BigIntType) return Texts("((", value, ").small == 0)");
    else if (t->tag == ClosureType) return Texts("((", value, ").fn == NULL)");
    else if (t->tag == NumType)
        return Texts(Match(t, NumType)->bits == TYPE_NBITS64 ? "Num$isnan(" : "Num32$isnan(", value, ")");
    else if (t->tag == ListType) return Texts("((", value, ").length < 0)");
    else if (t->tag == TableType || t->tag == SetType) return Texts("((", value, ").entries.length < 0)");
    else if (t->tag == BoolType) return Texts("((", value, ") == NONE_BOOL)");
    else if (t->tag == TextType) return Texts("((", value, ").length < 0)");
    else if (t->tag == IntType || t->tag == ByteType || t->tag == StructType) return Texts("(", value, ").is_none");
    else if (t->tag == EnumType) {
        if (enum_has_fields(t)) return Texts("((", value, ").$tag == 0)");
        else return Texts("((", value, ") == 0)");
    }
    print_err("Optional check not implemented for: ", type_to_str(t));
    return EMPTY_TEXT;
}

public
Text_t compile_optional(env_t *env, ast_t *ast) {
    ast_t *value = Match(ast, Optional)->value;
    Text_t value_code = compile(env, value);
    return promote_to_optional(get_type(env, value), value_code);
}

public
Text_t compile_non_optional(env_t *env, ast_t *ast) {
    ast_t *value = Match(ast, NonOptional)->value;
    type_t *t = get_type(env, value);
    Text_t value_code = compile(env, value);
    int64_t line = get_line_number(ast->file, ast->start);
    return Texts(
        "({ ", compile_declaration(t, Text("opt")), " = ", value_code, "; ", "if unlikely (",
        check_none(t, Text("opt")), ")\n", "#line ", line, "\n", "fail_source(", quoted_str(ast->file->filename), ", ",
        (int64_t)(value->start - value->file->text), ", ", (int64_t)(value->end - value->file->text), ", ",
        "\"This was expected to be a value, but it's none\");\n", optional_into_nonnone(t, Text("opt")), "; })");
}
