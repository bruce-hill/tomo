// This file defines how to compile field accessing like `foo.x`

#include "../ast.h"
#include "../config.h"
#include "../environment.h"
#include "../stdlib/tables.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "compilation.h"

Text_t compile_field_access(env_t *env, ast_t *ast) {
    DeclareMatch(f, ast, FieldAccess);
    type_t *fielded_t = get_type(env, f->fielded);
    type_t *value_t = value_type(fielded_t);
    switch (value_t->tag) {
    case TypeInfoType: {
        DeclareMatch(info, value_t, TypeInfoType);
        if (f->field[0] == '_') {
            if (!type_eq(env->current_type, info->type))
                code_err(ast, "Fields that start with underscores are not "
                              "accessible "
                              "on types outside of the type definition.");
        }
        binding_t *b = get_binding(info->env, f->field);
        if (!b) code_err(ast, "I couldn't find the field '", f->field, "' on this type");
        if (b->code.length == 0) code_err(ast, "I couldn't figure out how to compile this field");
        return b->code;
    }
    case TextType: {
        const char *lang = Match(value_t, TextType)->lang;
        if (lang && streq(f->field, "text")) {
            Text_t text = compile_to_pointer_depth(env, f->fielded, 0, false);
            return Texts("((Text_t)", text, ")");
        } else if (streq(f->field, "length")) {
            return Texts("Int$from_int64((", compile_to_pointer_depth(env, f->fielded, 0, false), ").length)");
        }
        code_err(ast, "There is no '", f->field, "' field on ", type_to_text(value_t), " values");
    }
    case StructType: {
        return compile_struct_field_access(env, ast);
    }
    case EnumType: {
        return compile_enum_field_access(env, ast);
    }
    case ListType: {
        if (streq(f->field, "length"))
            return Texts("Int$from_int64((", compile_to_pointer_depth(env, f->fielded, 0, false), ").length)");
        code_err(ast, "There is no ", f->field, " field on lists");
    }
    case SetType: {
        if (streq(f->field, "items"))
            return Texts("LIST_COPY((", compile_to_pointer_depth(env, f->fielded, 0, false), ").entries)");
        else if (streq(f->field, "length"))
            return Texts("Int$from_int64((", compile_to_pointer_depth(env, f->fielded, 0, false), ").entries.length)");
        code_err(ast, "There is no '", f->field, "' field on sets");
    }
    case TableType: {
        if (streq(f->field, "length")) {
            return Texts("Int$from_int64((", compile_to_pointer_depth(env, f->fielded, 0, false), ").entries.length)");
        } else if (streq(f->field, "keys")) {
            return Texts("LIST_COPY((", compile_to_pointer_depth(env, f->fielded, 0, false), ").entries)");
        } else if (streq(f->field, "values")) {
            DeclareMatch(table, value_t, TableType);
            Text_t offset = Texts("offsetof(struct { ", compile_declaration(table->key_type, Text("k")), "; ",
                                  compile_declaration(table->value_type, Text("v")), "; }, v)");
            return Texts("({ List_t *entries = &(", compile_to_pointer_depth(env, f->fielded, 0, false),
                         ").entries;\n"
                         "LIST_INCREF(*entries);\n"
                         "List_t values = *entries;\n"
                         "values.data += ",
                         offset,
                         ";\n"
                         "values; })");
        } else if (streq(f->field, "fallback")) {
            return Texts("({ Table_t *_fallback = (", compile_to_pointer_depth(env, f->fielded, 0, false),
                         ").fallback; _fallback ? *_fallback : NONE_TABLE; })");
        }
        code_err(ast, "There is no '", f->field, "' field on tables");
    }
    case ModuleType: {
        const char *name = Match(value_t, ModuleType)->name;
        env_t *module_env = Table$str_get(*env->imports, name);
        return compile(module_env, WrapAST(ast, Var, f->field));
    }
    default: code_err(ast, "Field accesses are not supported on ", type_to_text(fielded_t), " values");
    }
}
