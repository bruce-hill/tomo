#include "../types.h"
#include "../ast.h"
#include "../environment.h"
#include "../naming.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "text.h"

public
Text_t compile_type(type_t *t) {
    if (t == PATH_TYPE) return Text("Path_t");
    else if (t == PATH_TYPE_TYPE) return Text("PathType_t");

    switch (t->tag) {
    case ReturnType: errx(1, "Shouldn't be compiling ReturnType to a type");
    case AbortType: return Text("void");
    case VoidType: return Text("void");
    case MemoryType: return Text("void");
    case BoolType: return Text("Bool_t");
    case ByteType: return Text("Byte_t");
    case CStringType: return Text("const char*");
    case BigIntType: return Text("Int_t");
    case IntType: return Texts("Int", String(Match(t, IntType)->bits), "_t");
    case NumType:
        return Match(t, NumType)->bits == TYPE_NBITS64 ? Text("Num_t")
                                                       : Texts("Num", String(Match(t, NumType)->bits), "_t");
    case TextType: {
        DeclareMatch(text, t, TextType);
        if (!text->lang || streq(text->lang, "Text")) return Text("Text_t");
        else return namespace_name(text->env, text->env->namespace, Text("$type"));
    }
    case ListType: return Text("List_t");
    case SetType: return Text("Table_t");
    case TableType: return Text("Table_t");
    case FunctionType: {
        DeclareMatch(fn, t, FunctionType);
        Text_t code = Texts(compile_type(fn->ret), " (*)(");
        for (arg_t *arg = fn->args; arg; arg = arg->next) {
            code = Texts(code, compile_type(arg->type));
            if (arg->next) code = Texts(code, ", ");
        }
        if (!fn->args) code = Texts(code, "void");
        return Texts(code, ")");
    }
    case ClosureType: return Text("Closure_t");
    case PointerType: return Texts(compile_type(Match(t, PointerType)->pointed), "*");
    case StructType: {
        DeclareMatch(s, t, StructType);
        if (s->external) return Text$from_str(s->name);
        return Texts("struct ", namespace_name(s->env, s->env->namespace, Text("$struct")));
    }
    case EnumType: {
        DeclareMatch(e, t, EnumType);
        return namespace_name(e->env, e->env->namespace, Text("$type"));
    }
    case OptionalType: {
        type_t *nonnull = Match(t, OptionalType)->type;
        switch (nonnull->tag) {
        case CStringType:
        case FunctionType:
        case ClosureType:
        case PointerType:
        case EnumType: return compile_type(nonnull);
        case TextType: return Match(nonnull, TextType)->lang ? compile_type(nonnull) : Text("OptionalText_t");
        case IntType:
        case BigIntType:
        case NumType:
        case BoolType:
        case ByteType:
        case ListType:
        case TableType:
        case SetType: return Texts("Optional", compile_type(nonnull));
        case StructType: {
            if (nonnull == PATH_TYPE) return Text("OptionalPath_t");
            if (nonnull == PATH_TYPE_TYPE) return Text("OptionalPathType_t");
            DeclareMatch(s, nonnull, StructType);
            return namespace_name(s->env, s->env->namespace->parent, Texts("$Optional", s->name, "$$type"));
        }
        default: compiler_err(NULL, NULL, NULL, "Optional types are not supported for: ", type_to_str(t));
        }
    }
    case TypeInfoType: return Text("TypeInfo_t");
    default: compiler_err(NULL, NULL, NULL, "Compiling type is not implemented for type with tag ", t->tag);
    }
    return EMPTY_TEXT;
}

public
Text_t compile_type_info(type_t *t) {
    if (t == NULL) compiler_err(NULL, NULL, NULL, "Attempt to compile a NULL type");
    if (t == PATH_TYPE) return Text("&Path$info");
    else if (t == PATH_TYPE_TYPE) return Text("&PathType$info");

    switch (t->tag) {
    case BoolType:
    case ByteType:
    case IntType:
    case BigIntType:
    case NumType:
    case CStringType: return Texts("&", type_to_text(t), "$info");
    case TextType: {
        DeclareMatch(text, t, TextType);
        if (!text->lang || streq(text->lang, "Text")) return Text("&Text$info");
        return Texts("(&", namespace_name(text->env, text->env->namespace, Text("$info")), ")");
    }
    case StructType: {
        DeclareMatch(s, t, StructType);
        return Texts("(&", namespace_name(s->env, s->env->namespace, Text("$info")), ")");
    }
    case EnumType: {
        DeclareMatch(e, t, EnumType);
        return Texts("(&", namespace_name(e->env, e->env->namespace, Text("$info")), ")");
    }
    case ListType: {
        type_t *item_t = Match(t, ListType)->item_type;
        return Texts("List$info(", compile_type_info(item_t), ")");
    }
    case SetType: {
        type_t *item_type = Match(t, SetType)->item_type;
        return Texts("Set$info(", compile_type_info(item_type), ")");
    }
    case TableType: {
        DeclareMatch(table, t, TableType);
        type_t *key_type = table->key_type;
        type_t *value_type = table->value_type;
        return Texts("Table$info(", compile_type_info(key_type), ", ", compile_type_info(value_type), ")");
    }
    case PointerType: {
        DeclareMatch(ptr, t, PointerType);
        const char *sigil = ptr->is_stack ? "&" : "@";
        return Texts("Pointer$info(", quoted_str(sigil), ", ", compile_type_info(ptr->pointed), ")");
    }
    case FunctionType: {
        return Texts("Function$info(", quoted_text(type_to_text(t)), ")");
    }
    case ClosureType: {
        return Texts("Closure$info(", quoted_text(type_to_text(t)), ")");
    }
    case OptionalType: {
        type_t *non_optional = Match(t, OptionalType)->type;
        return Texts("Optional$info(sizeof(", compile_type(non_optional), "), __alignof__(", compile_type(non_optional),
                     "), ", compile_type_info(non_optional), ")");
    }
    case TypeInfoType: return Texts("Type$info(", quoted_text(type_to_text(Match(t, TypeInfoType)->type)), ")");
    case MemoryType: return Text("&Memory$info");
    case VoidType: return Text("&Void$info");
    default: compiler_err(NULL, 0, 0, "I couldn't convert to a type info: ", type_to_str(t));
    }
    return EMPTY_TEXT;
}
