#include "../compile.h"
#include "../environment.h"
#include "../naming.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../types.h"

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
