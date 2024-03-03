
#include <ctype.h>
#include <gc/cord.h>
#include <gc.h>
#include <stdio.h>

#include "ast.h"
#include "builtins/string.h"
#include "compile.h"
#include "environment.h"
#include "typecheck.h"
#include "util.h"

static bool is_plain_data(env_t *env, type_t *t)
{
    switch (t->tag) {
    case BoolType: case IntType: case NumType: case PointerType: case FunctionType:
        return true;
    case StructType: {
        for (arg_t *arg = Match(t, StructType)->fields; arg; arg = arg->next) {
            if (!is_plain_data(env, get_arg_type(env, arg)))
                return false;
        }
        return true;
    }
    case EnumType: {
        for (tag_t *tag = Match(t, EnumType)->tags; tag; tag = tag->next) {
            if (!is_plain_data(env, tag->type))
                return false;
        }
        return true;
    }
    default:
        return false;
    }
}

static CORD compile_str_method(env_t *env, ast_t *ast)
{
    auto def = Match(ast, StructDef);
    CORD str_func = CORD_asprintf("static CORD %s__as_str(%s_t *obj, bool use_color) {\n"
                                  "\tif (!obj) return \"%s\";\n", def->name, def->name, def->name);
    if (def->secret) {
        CORD_appendf(&str_func, "\treturn use_color ? \"\\x1b[0;1m%s\\x1b[m(\\x1b[2m...\\x1b[m)\" : \"%s(...)\";\n}",
                     def->name, def->name);
    } else {
        CORD_appendf(&str_func, "\treturn CORD_all(use_color ? \"\\x1b[0;1m%s\\x1b[m(\" : \"%s(\"", def->name, def->name);
        for (arg_ast_t *field = def->fields; field; field = field->next) {
            type_t *field_type = get_arg_ast_type(env, field);
            CORD field_str = expr_as_string(env, CORD_cat("obj->", field->name), field_type, "use_color");
            CORD_appendf(&str_func, ", \"%s=\", %r", field->name, field_str);
            if (field->next) CORD_appendf(&str_func, ", \", \"");
        }
        CORD_appendf(&str_func, ", \")\");\n}\n");
    }
    return str_func;
}

static CORD compile_compare_method(env_t *env, ast_t *ast)
{
    auto def = Match(ast, StructDef);
    CORD cmp_func = CORD_all("static int ", def->name, "__compare(const ", def->name, "_t *x, const ", def->name,
                             "_t *y, const TypeInfo *info) {\n"
                             "(void)info;\n",
                             "int diff;\n");
    for (arg_ast_t *field = def->fields; field; field = field->next) {
        type_t *field_type = get_arg_ast_type(env, field);
        switch (field_type->tag) {
        case BoolType: case IntType: case NumType: case PointerType: case FunctionType:
            cmp_func = CORD_all(cmp_func, "diff = (x->", field->name, " > y->", field->name, ") - (x->", field->name, " < y->", field->name, ");");
            break;
        case StringType:
            cmp_func = CORD_all(cmp_func, "diff = CORD_cmp(x->", field->name, ", y->", field->name, ");");
            break;
        default:
            cmp_func = CORD_all(cmp_func, "diff = generic_compare(&x->", field->name, ", &y->", field->name, ", ",
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
    CORD eq_func = CORD_all("static bool ", def->name, "__equal(const ", def->name, "_t *x, const ", def->name,
                             "_t *y, const TypeInfo *info) {\n"
                             "(void)info;\n");
    for (arg_ast_t *field = def->fields; field; field = field->next) {
        type_t *field_type = parse_type_ast(env, field->type);
        switch (field_type->tag) {
        case BoolType: case IntType: case NumType: case PointerType: case FunctionType:
            eq_func = CORD_all(eq_func, "if (x->", field->name, " != y->", field->name, ") return no;\n");
            break;
        case StringType:
            eq_func = CORD_all(eq_func, "if (CORD_cmp(x->", field->name, ", y->", field->name, ") != 0) return no;\n");
            break;
        default:
            eq_func = CORD_all(eq_func, "if (!generic_equal(&x->", field->name, ", &y->", field->name, ", ",
                                compile_type_info(env, field_type), ")) return no;\n");
            break;
        }
    }
    eq_func = CORD_all(eq_func, "return yes;\n}\n");
    return eq_func;
}

static CORD compile_hash_method(env_t *env, ast_t *ast)
{
    auto def = Match(ast, StructDef);
    CORD hash_func = CORD_all("static uint32_t ", def->name, "__hash(const ", def->name, "_t *obj, const TypeInfo *info) {\n"
                              "(void)info;\n"
                              "uint32_t field_hashes[] = {");
    for (arg_ast_t *field = def->fields; field; field = field->next) {
        type_t *field_type = get_arg_ast_type(env, field);
        hash_func = CORD_all(hash_func, "\ngeneric_hash(&obj->", field->name, ", ", compile_type_info(env, field_type), "),");
    }
    hash_func = CORD_all(hash_func, "};\n"
                         "uint32_t hash;\n"
                         "halfsiphash(&field_hashes, sizeof(field_hashes), SSS_HASH_VECTOR, (uint8_t*)&hash, sizeof(hash));\n"
                         "return hash;\n}\n");
    return hash_func;
}

void compile_struct_def(env_t *env, ast_t *ast)
{
    auto def = Match(ast, StructDef);
    CORD_appendf(&env->code->typedefs, "typedef struct %s_s %s_t;\n", def->name, def->name);
    CORD_appendf(&env->code->typedefs, "#define %s(...) ((%s_t){__VA_ARGS__})\n", def->name, def->name);

    CORD_appendf(&env->code->typecode, "struct %s_s {\n", def->name);
    for (arg_ast_t *field = def->fields; field; field = field->next) {
        CORD type = compile_type_ast(field->type);
        CORD_appendf(&env->code->typecode, "%r %s%s;\n", type, field->name,
                     CORD_cmp(type, "Bool_t") ? "" : ":1");
    }
    CORD_appendf(&env->code->typecode, "};\n");

    // Typeinfo:
    CORD_appendf(&env->code->typedefs, "extern const TypeInfo %s;\n", def->name);

    type_t *t = Table_str_get(env->types, def->name);
    CORD typeinfo = CORD_asprintf("public const TypeInfo %s = {%zu, %zu, {.tag=CustomInfo, .CustomInfo={",
                                  def->name, type_size(t), type_align(t));

    typeinfo = CORD_all(typeinfo, ".as_str=(void*)", def->name, "__as_str, ");
    env->code->funcs = CORD_all(env->code->funcs, compile_str_method(env, ast));
    if (!t || !is_plain_data(env, t)) {
        env->code->funcs = CORD_all(
            env->code->funcs, compile_equals_method(env, ast), compile_compare_method(env, ast),
            compile_hash_method(env, ast));
        typeinfo = CORD_all(
            typeinfo,
            ".equal=(void*)", def->name, "__equal, "
            ".hash=(void*)", def->name, "__hash, "
            ".compare=(void*)", def->name, "__compare");
    }
    typeinfo = CORD_cat(typeinfo, "}}};\n");
    env->code->typeinfos = CORD_all(env->code->typeinfos, typeinfo);
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
