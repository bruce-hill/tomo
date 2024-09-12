// Logic for compiling new struct types defined in code
#include <ctype.h>
#include <gc/cord.h>
#include <gc.h>
#include <stdio.h>

#include "ast.h"
#include "builtins/text.h"
#include "compile.h"
#include "environment.h"
#include "typecheck.h"
#include "builtins/util.h"

static CORD compile_str_method(env_t *env, ast_t *ast)
{
    auto def = Match(ast, StructDef);
    CORD full_name = CORD_cat(namespace_prefix(env->libname, env->namespace), def->name);
    const char *name = def->name;
    const char *dollar = strrchr(name, '$');
    if (dollar) name = dollar + 1;
    CORD str_func = CORD_asprintf("static Text_t %r$as_text(%r_t *obj, bool use_color) {\n"
                                  "\tif (!obj) return Text(\"%s\");\n", full_name, full_name, name);
    if (def->secret) {
        CORD_appendf(&str_func, "\treturn use_color ? Text(\"\\x1b[0;1m%s\\x1b[m(\\x1b[2m...\\x1b[m)\") : Text(\"%s(...)\");\n}",
                     name, name);
    } else if (def->fields && !def->fields->next) { // Single-member structs don't need to print names:
        type_t *field_type = get_arg_ast_type(env, def->fields);
        CORD field_str = expr_as_text(env, CORD_cat("obj->$", def->fields->name), field_type, "use_color");
        str_func = CORD_all(str_func, "\treturn Text$concat(use_color ? Text(\"\\x1b[0;1m", name, "\\x1b[m(\") : Text(\"", name, "(\"), ",
            field_str, ", Text(\")\"));\n}\n");
    } else {
        CORD_appendf(&str_func, "\treturn Text$concat(use_color ? Text(\"\\x1b[0;1m%s\\x1b[m(\") : Text(\"%s(\")", name, name);
        for (arg_ast_t *field = def->fields; field; field = field->next) {
            type_t *field_type = get_arg_ast_type(env, field);
            CORD field_str = expr_as_text(env, CORD_cat("obj->$", field->name), field_type, "use_color");
            CORD_appendf(&str_func, ", Text(\"%s=\"), %r", field->name, field_str);
            if (field->next) CORD_appendf(&str_func, ", Text(\", \")");
        }
        CORD_appendf(&str_func, ", Text(\")\"));\n}\n");
    }
    return str_func;
}

static CORD compile_compare_method(env_t *env, ast_t *ast)
{
    auto def = Match(ast, StructDef);
    CORD full_name = CORD_cat(namespace_prefix(env->libname, env->namespace), def->name);
    CORD cmp_func = CORD_all("static int ", full_name, "$compare(const ", full_name, "_t *x, const ", full_name,
                             "_t *y, const TypeInfo *info) {\n"
                             "(void)info;\n",
                             "int diff;\n");
    for (arg_ast_t *field = def->fields; field; field = field->next) {
        type_t *field_type = get_arg_ast_type(env, field);
        switch (field_type->tag) {
        case BigIntType:
            cmp_func = CORD_all(cmp_func, "diff = Int$compare_value(x->$", field->name, ", y->$", field->name, ");");
            break;
        case BoolType: case IntType: case NumType: case PointerType: case FunctionType:
            cmp_func = CORD_all(cmp_func, "diff = (x->$", field->name, " > y->$", field->name, ") - (x->$", field->name, " < y->$", field->name, ");");
            break;
        default:
            cmp_func = CORD_all(cmp_func, "diff = generic_compare(&x->$", field->name, ", &y->$", field->name, ", ",
                                compile_type_info(env, field_type), ");\n");
            break;
        }
        cmp_func = CORD_all(cmp_func, "if (diff != 0) return diff;\n");
    }
    cmp_func = CORD_all(cmp_func, "return 0;\n}\n");
    return cmp_func;
}

static CORD compile_equals_method(env_t *env, ast_t *ast)
{
    auto def = Match(ast, StructDef);
    CORD full_name = CORD_cat(namespace_prefix(env->libname, env->namespace), def->name);
    CORD eq_func = CORD_all("static bool ", full_name, "$equal(const ", full_name, "_t *x, const ", full_name,
                             "_t *y, const TypeInfo *info) {\n"
                             "(void)info;\n");
    CORD condition = CORD_EMPTY;
    for (arg_ast_t *field = def->fields; field; field = field->next) {
        if (condition != CORD_EMPTY)
            condition = CORD_all(condition, " && ");
        type_t *field_type = get_arg_ast_type(env, field);
        switch (field_type->tag) {
        case BigIntType:
            condition = CORD_all(condition, "Int$equal_value(x->$", field->name, ", y->$", field->name, ")");
            break;
        case BoolType: case IntType: case NumType: case PointerType: case FunctionType:
            condition = CORD_all(condition, "(x->$", field->name, " == y->$", field->name, ")");
            break;
        default:
            condition = CORD_all(condition, "generic_equal(&x->$", field->name, ", &y->$", field->name, ", ",
                                compile_type_info(env, field_type), ")");
            break;
        }
    }
    eq_func = CORD_all(eq_func, "return ", condition, ";\n}\n");
    return eq_func;
}

static CORD compile_hash_method(env_t *env, ast_t *ast)
{
    auto def = Match(ast, StructDef);
    CORD full_name = CORD_cat(namespace_prefix(env->libname, env->namespace), def->name);
    CORD hash_func = CORD_all("static uint64_t ", full_name, "$hash(const ", full_name, "_t *obj, const TypeInfo *info) {\n"
                              "(void)info;\n"
                              "uint64_t field_hashes[] = {");
    for (arg_ast_t *field = def->fields; field; field = field->next) {
        type_t *field_type = get_arg_ast_type(env, field);
        if (field_type->tag == BoolType) // Bools can be bit fields, so you can't use *obj->field there:
            hash_func = CORD_all(hash_func, "\n(uint32_t)(obj->$", field->name, "),");
        else
            hash_func = CORD_all(hash_func, "\ngeneric_hash(&obj->$", field->name, ", ", compile_type_info(env, field_type), "),");
    }
    hash_func = CORD_all(hash_func, "};\n"
                         "return siphash24((void*)&field_hashes, sizeof(field_hashes));\n}\n");
    return hash_func;
}

void compile_struct_def(env_t *env, ast_t *ast)
{
    auto def = Match(ast, StructDef);
    CORD full_name = CORD_cat(namespace_prefix(env->libname, env->namespace), def->name);

    type_t *t = Table$str_get(*env->types, def->name);
    assert(t && t->tag == StructType);
    auto struct_ = Match(t, StructType);
    if (def->fields) {
        CORD typeinfo = CORD_asprintf("public const TypeInfo %r = {%zu, %zu, {.tag=StructInfo, .CustomInfo={",
                                      full_name, type_size(t), type_align(t));

        typeinfo = CORD_all(typeinfo, ".as_text=(void*)", full_name, "$as_text, ");
        env->code->funcs = CORD_all(env->code->funcs, compile_str_method(env, ast));

        if (struct_->fields && !struct_->fields->next) { // Single member, can just use its methods
            type_t *member_t = struct_->fields->type;
            switch (member_t->tag) {
            case IntType: case NumType:
                typeinfo = CORD_all(typeinfo, ".compare=(void*)", type_to_cord(member_t), "$compare, "
                                    ".equal=(void*)", type_to_cord(member_t), "$equal, ");
                goto got_methods;
            case BigIntType: case TextType:
                typeinfo = CORD_all(typeinfo, ".hash=(void*)", type_to_cord(member_t), "$hash", ", ",
                                    ".compare=(void*)", type_to_cord(member_t), "$compare, "
                                    ".equal=(void*)", type_to_cord(member_t), "$equal, ");
                goto got_methods;
            case BoolType: goto got_methods;
            default: break;
            }
        }
        if (struct_->fields) {
            env->code->funcs = CORD_all(env->code->funcs, compile_compare_method(env, ast),
                                        compile_equals_method(env, ast), compile_hash_method(env, ast));
            typeinfo = CORD_all(
                typeinfo,
                ".compare=(void*)", full_name, "$compare, "
                ".equal=(void*)", full_name, "$equal, "
                ".hash=(void*)", full_name, "$hash");
        }
      got_methods:;
        typeinfo = CORD_cat(typeinfo, "}}};\n");
        env->code->typeinfos = CORD_all(env->code->typeinfos, typeinfo);
    } else {
        // If there are no fields, we can use an EmptyStructInfo typeinfo, which generates less code:
        CORD typeinfo = CORD_asprintf("public const TypeInfo %r = {%zu, %zu, {.tag=EmptyStructInfo, .EmptyStructInfo.name=%r}};\n",
                                      full_name, type_size(t), type_align(t), CORD_quoted(def->name));
        env->code->typeinfos = CORD_all(env->code->typeinfos, typeinfo);
    }

    compile_namespace(env, def->name, def->namespace);
}

CORD compile_struct_typedef(env_t *env, ast_t *ast)
{
    auto def = Match(ast, StructDef);
    CORD full_name = CORD_cat(namespace_prefix(env->libname, env->namespace), def->name);

    CORD fields = CORD_EMPTY;
    for (arg_ast_t *field = def->fields; field; field = field->next) {
        type_t *field_t = get_arg_ast_type(env, field);
        CORD type_code = compile_type(field_t);
        fields = CORD_all(fields, type_code, " $", field->name, field_t->tag == BoolType ? ":1" : CORD_EMPTY, ";\n");
    }
    CORD struct_code = CORD_all("struct ", full_name, "_s {\n");
    struct_code = CORD_all(struct_code, "};\n");
    return CORD_all(
        "typedef struct ", full_name, "_s ", full_name, "_t;\n",
        "struct ", full_name, "_s {\n",
        fields,
        "};\n",
        "typedef struct {\n",
        full_name, "_t value;\n"
        "Bool_t is_null:1;\n"
        "} ", namespace_prefix(env->libname, env->namespace), "$Optional", def->name, "_t;\n");
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
