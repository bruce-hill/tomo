// This file defines how to compile optionals and null

#include "../environment.h"
#include "../naming.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "../types.h"
#include "compilation.h"
#include "indexing.h"

Text_t optional_into_nonnone(type_t *t, Text_t value) {
    if (t->tag == OptionalType) t = Match(t, OptionalType)->type;
    switch (t->tag) {
    case IntType:
    case ByteType: return Texts(value, ".value");
    case StructType: return Texts(value, ".value");
    default: return value;
    }
}

public
Text_t promote_to_optional(type_t *t, Text_t code) {
    if (t->tag == IntType) {
        switch (Match(t, IntType)->bits) {
        case TYPE_IBITS8: return Texts("((OptionalInt8_t){.has_value=true, .value=", code, "})");
        case TYPE_IBITS16: return Texts("((OptionalInt16_t){.has_value=true, .value=", code, "})");
        case TYPE_IBITS32: return Texts("((OptionalInt32_t){.has_value=true, .value=", code, "})");
        case TYPE_IBITS64: return Texts("((OptionalInt64_t){.has_value=true, .value=", code, "})");
        default: errx(1, "Unsupported in type: %s", Text$as_c_string(type_to_text(t)));
        }
        return code;
    } else if (t->tag == ByteType) {
        return Texts("((OptionalByte_t){.has_value=true, .value=", code, "})");
    } else if (t->tag == StructType) {
        return Texts("((", compile_type(Type(OptionalType, .type = t)), "){.has_value=true, .value=", code, "})");
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
    case TextType: return Text("NONE_TEXT");
    case CStringType: return Text("NULL");
    case PointerType: return Texts("((", compile_type(t), ")NULL)");
    case ClosureType: return Text("NONE_CLOSURE");
    case FloatType: return Text("nan(\"none\")");
    case StructType: return Texts("((", compile_type(Type(OptionalType, .type = t)), "){.has_value=false})");
    case EnumType: {
        env_t *enum_env = Match(t, EnumType)->env;
        return Texts("((", compile_type(t), "){", namespace_name(enum_env, enum_env->namespace, Text("none")), "})");
    }
    default: compiler_err(NULL, NULL, NULL, "none isn't implemented for this type: ", type_to_text(t));
    }
    return EMPTY_TEXT;
}

public
Text_t check_none(type_t *t, Text_t value) {
    t = Match(t, OptionalType)->type;
    // NOTE: these use statement expressions ({...;}) because some compilers
    // complain about excessive parens around equality comparisons
    if (t->tag == PointerType || t->tag == FunctionType || t->tag == CStringType) return Texts("(", value, " == NULL)");
    else if (t->tag == BigIntType) return Texts("((", value, ").small == 0)");
    else if (t->tag == ClosureType) return Texts("((", value, ").fn == NULL)");
    else if (t->tag == FloatType)
        return Texts(Match(t, FloatType)->bits == TYPE_NBITS64 ? "Float64$isnan(" : "Float32$isnan(", value, ")");
    else if (t->tag == ListType) return Texts("((", value, ").data == NULL)");
    else if (t->tag == TableType) return Texts("((", value, ").entries.data == NULL)");
    else if (t->tag == BoolType) return Texts("((", value, ") == NONE_BOOL)");
    else if (t->tag == TextType) return Texts("((", value, ").tag == TEXT_NONE)");
    else if (t->tag == IntType || t->tag == ByteType || t->tag == StructType) return Texts("!(", value, ").has_value");
    else if (t->tag == EnumType) return Texts("((", value, ").$tag == 0)");
    print_err("Optional check not implemented for: ", type_to_text(t));
    return EMPTY_TEXT;
}

public
Text_t compile_non_optional(env_t *env, ast_t *ast) {
    ast_t *value = Match(ast, NonOptional)->value;
    if (value->tag == Index && Match(value, Index)->index != NULL) return compile_indexing(env, value, true);
    type_t *value_t = get_type(env, value);
    if (value_t->tag == PointerType) {
        // Dereference pointers automatically
        return compile_non_optional(env, WrapAST(ast, NonOptional, WrapAST(ast, Index, .indexed = value)));
    }
    int64_t line = get_line_number(ast->file, ast->start);
    if (value_t->tag == EnumType) {
        // For this case:
        //   enum Foo(FirstField, SecondField(msg:Text))
        //   e := ...
        //   e!
        // We desugar into `e.FirstField!` using the first enum field
        tag_t *first_tag = Match(value_t, EnumType)->tags;
        if (!first_tag) code_err(ast, "'!' cannot be used on an empty enum");
        return compile_non_optional(
            env, WrapAST(ast, NonOptional, WrapAST(value, FieldAccess, .fielded = value, .field = first_tag->name)));
    } else if (value->tag == FieldAccess
               && value_type(get_type(env, Match(value, FieldAccess)->fielded))->tag == EnumType) {
        type_t *enum_t = value_type(get_type(env, Match(value, FieldAccess)->fielded));
        DeclareMatch(e, enum_t, EnumType);
        DeclareMatch(f, value, FieldAccess);
        for (tag_t *tag = e->tags; tag; tag = tag->next) {
            if (streq(f->field, tag->name)) {
                Text_t tag_name = namespace_name(e->env, e->env->namespace, Texts("tag$", tag->name));
                return Texts(
                    "({ ", compile_declaration(enum_t, Text("_test_enum")), " = ",
                    compile_to_pointer_depth(env, f->fielded, 0, true), ";",
                    "if unlikely (_test_enum.$tag != ", tag_name, ") {\n", "#line ", line, "\n", "fail_source(",
                    quoted_str(f->fielded->file->filename), ", ", (int64_t)(f->fielded->start - f->fielded->file->text),
                    ", ", (int64_t)(f->fielded->end - f->fielded->file->text), ", ", "\"This was expected to be ",
                    tag->name, ", but it was: \", ", expr_as_text(Text("_test_enum"), enum_t, Text("false")),
                    ", \"\\n\");\n}\n",
                    compile_maybe_incref(
                        env, WrapLiteralCode(value, Texts("_test_enum.", tag->name), .type = tag->type), tag->type),
                    "; })");
            }
        }
        code_err(ast, "The field '", f->field, "' is not a valid tag name of ", type_to_text(enum_t));
    } else {
        Text_t value_code = compile(env, value);
        return Texts("({ ", compile_declaration(value_t, Text("opt")), " = ", value_code, "; ", "if unlikely (",
                     check_none(value_t, Text("opt")), ")\n", "#line ", line, "\n", "fail_source(",
                     quoted_str(value->file->filename), ", ", (int64_t)(value->start - value->file->text), ", ",
                     (int64_t)(value->end - value->file->text), ", ",
                     "\"This was expected to be a value, but it's `none`\\n\");\n",
                     optional_into_nonnone(value_t, Text("opt")), "; })");
    }
}
