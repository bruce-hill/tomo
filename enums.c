
#include <ctype.h>
#include <gc/cord.h>
#include <gc.h>
#include <stdio.h>

#include "ast.h"
#include "builtins/text.h"
#include "compile.h"
#include "structs.h"
#include "environment.h"
#include "typecheck.h"
#include "util.h"

static CORD compile_str_method(env_t *env, ast_t *ast)
{
    auto def = Match(ast, EnumDef);
    CORD str_func = CORD_all("static CORD ", def->name, "$as_text(", def->name, "_t *obj, bool use_color) {\n"
                             "\tif (!obj) return \"", def->name, "\";\n"
                             "switch (obj->$tag) {\n");
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        str_func = CORD_all(str_func, "\tcase $tag$", def->name, "$", tag->name, ": return CORD_all(use_color ? \"\\x1b[36;1m",
                     def->name, ".", tag->name, "\\x1b[m(\" : \"", def->name, ".", tag->name, "(\"");

        if (tag->secret) {
            str_func = CORD_cat(str_func, ", \"***)\");\n");
            continue;
        }

        for (arg_ast_t *field = tag->fields; field; field = field->next) {
            type_t *field_t = get_arg_ast_type(env, field);
            CORD field_str = expr_as_texting(env, CORD_all("obj->", tag->name, ".", field->name), field_t, "use_color");
            str_func = CORD_all(str_func, ", \"", field->name, "=\", ", field_str);
            if (field->next) str_func = CORD_cat(str_func, ", \", \"");
        }
        str_func = CORD_cat(str_func, ", \")\");\n");
    }
    str_func = CORD_cat(str_func, "\tdefault: return CORD_EMPTY;\n\t}\n}\n");
    return str_func;
}

static CORD compile_compare_method(env_t *env, ast_t *ast)
{
    auto def = Match(ast, EnumDef);
    CORD cmp_func = CORD_all("static int ", def->name, "$compare(const ", def->name, "_t *x, const ", def->name,
                             "_t *y, const TypeInfo *info) {\n"
                             "(void)info;\n"
                             "int diff = x->$tag - y->$tag;\n"
                             "if (diff) return diff;\n"
                             "switch (x->$tag) {\n");
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        type_t *tag_type = Table_str_get(env->types, heap_strf("%s$%s", def->name, tag->name));
        cmp_func = CORD_all(cmp_func, "\tcase $tag$", def->name, "$", tag->name, ": "
                            "return generic_compare(&x->", tag->name, ", &y->", tag->name, ", ", compile_type_info(env, tag_type), ");\n");
    }
    cmp_func = CORD_all(cmp_func, "default: return 0;\n}\n}\n");
    return cmp_func;
}

static CORD compile_equals_method(env_t *env, ast_t *ast)
{
    auto def = Match(ast, EnumDef);
    CORD eq_func = CORD_all("static bool ", def->name, "$equal(const ", def->name, "_t *x, const ", def->name,
                             "_t *y, const TypeInfo *info) {\n"
                             "(void)info;\n"
                             "if (x->$tag != y->$tag) return no;\n"
                             "switch (x->$tag) {\n");
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        type_t *tag_type = Table_str_get(env->types, heap_strf("%s$%s", def->name, tag->name));
        eq_func = CORD_all(eq_func, "\tcase $tag$", def->name, "$", tag->name, ": "
                            "return generic_equal(&x->", tag->name, ", &y->", tag->name, ", ", compile_type_info(env, tag_type), ");\n");
    }
    eq_func = CORD_all(eq_func, "default: return 0;\n}\n}\n");
    return eq_func;
}

static CORD compile_hash_method(env_t *env, ast_t *ast)
{
    auto def = Match(ast, EnumDef);
    CORD hash_func = CORD_all("static uint32_t ", def->name, "$hash(const ", def->name, "_t *obj, const TypeInfo *info) {\n"
                              "(void)info;\n"
                              "uint32_t hashes[2] = {(uint32_t)obj->$tag};\n"
                              "switch (obj->$tag) {\n");
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        type_t *tag_type = Table_str_get(env->types, heap_strf("%s$%s", def->name, tag->name));
        hash_func = CORD_all(hash_func, "\tcase $tag$", def->name, "$", tag->name, ": "
                             "hashes[1] = generic_hash(&obj->", tag->name, ", ", compile_type_info(env, tag_type), ");\n"
                             "break;\n");
    }
    hash_func = CORD_all(hash_func, "}\n"
                         "uint32_t hash;\n"
                         "halfsiphash(&hashes, sizeof(hashes), SSS_HASH_VECTOR, (uint8_t*)&hash, sizeof(hash));\n"
                         "return hash;\n}\n");
    return hash_func;
}

void compile_enum_def(env_t *env, ast_t *ast)
{
    auto def = Match(ast, EnumDef);
    CORD_appendf(&env->code->typedefs, "typedef struct %s_s %s_t;\n", def->name, def->name);
    CORD_appendf(&env->code->typedefs, "extern const TypeInfo %s;\n", def->name);
    CORD enum_def = CORD_all("struct ", def->name, "_s {\n"
                             "\tenum {");
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        CORD_appendf(&enum_def, "$tag$%s$%s = %ld", def->name, tag->name, tag->value);
        if (tag->next) enum_def = CORD_cat(enum_def, ", ");
    }
    enum_def = CORD_cat(enum_def, "} $tag;\n"
                        "union {\n");
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        compile_struct_def(env, WrapAST(ast, StructDef, .name=heap_strf("%s$%s", def->name, tag->name), .fields=tag->fields));
        enum_def = CORD_all(enum_def, def->name, "$", tag->name, "_t ", tag->name, ";\n");
        // Constructor:
        CORD arg_sig = CORD_EMPTY;
        for (arg_ast_t *field = tag->fields; field; field = field->next) {
            type_t *field_t = get_arg_ast_type(env, field);
            CORD type_code = compile_type(field_t);
            arg_sig = CORD_all(arg_sig, type_code, " ", field->name);
            if (CORD_cmp(type_code, "Bool_t") == 0) arg_sig = CORD_cat(arg_sig, ":1");
            if (field->next) arg_sig = CORD_cat(arg_sig, ", ");
        }
        if (arg_sig == CORD_EMPTY) arg_sig = "void";
        CORD constructor_def = CORD_all(def->name, "_t ", def->name, "$tagged$", tag->name, "(", arg_sig, ");\n");
        env->code->fndefs = CORD_cat(env->code->fndefs, constructor_def);

        CORD constructor_impl = CORD_all("public inline ", def->name, "_t ", def->name, "$tagged$", tag->name, "(", arg_sig, ") { return (",
                                         def->name, "_t){.$tag=$tag$", def->name, "$", tag->name, ", .", tag->name, "={");
        for (arg_ast_t *field = tag->fields; field; field = field->next) {
            constructor_impl = CORD_all(constructor_impl, field->name);
            if (field->next) constructor_impl = CORD_cat(constructor_impl, ", ");
        }
        constructor_impl = CORD_cat(constructor_impl, "}}; }\n");
        env->code->funcs = CORD_cat(env->code->funcs, constructor_impl);
    }
    enum_def = CORD_cat(enum_def, "};\n};\n");
    env->code->typecode = CORD_cat(env->code->typecode, enum_def);

    type_t *t = Table_str_get(env->types, def->name);
    CORD typeinfo = CORD_asprintf("public const TypeInfo %s = {%zu, %zu, {.tag=CustomInfo, .CustomInfo={",
                                  def->name, type_size(t), type_align(t));

    env->code->funcs = CORD_all(
        env->code->funcs,
        compile_str_method(env, ast),
        compile_equals_method(env, ast), compile_compare_method(env, ast),
        compile_hash_method(env, ast));
    typeinfo = CORD_all(
        typeinfo,
        ".as_text=(void*)", def->name, "$as_text, "
        ".equal=(void*)", def->name, "$equal, "
        ".hash=(void*)", def->name, "$hash, "
        ".compare=(void*)", def->name, "$compare");
    typeinfo = CORD_cat(typeinfo, "}}};\n");
    env->code->typeinfos = CORD_all(env->code->typeinfos, typeinfo);

    env_t *ns_env = namespace_env(env, def->name);
    for (ast_list_t *stmt = def->namespace ? Match(def->namespace, Block)->statements : NULL; stmt; stmt = stmt->next) {
        ast_t *ast = stmt->ast;
        switch (ast->tag) {
        case FunctionDef:
            CORD code = compile_statement(ns_env, ast);
            env->code->funcs = CORD_cat(env->code->funcs, code);
            break;
        case Declare: {
            CORD code = compile_statement(ns_env, ast);
            env->code->staticdefs = CORD_cat(env->code->staticdefs, code);
            auto decl = Match(ast, Declare);
            type_t *t = get_type(ns_env, decl->value);
            env->code->fndefs = CORD_all(env->code->fndefs, "extern ", compile_type(t), " ", compile(ns_env, decl->var), ";\n");
            break;
        }
        default: {
            CORD code = compile_statement(ns_env, ast);
            env->code->main = CORD_cat(env->code->main, code);
            break;
        }
    }
    }
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
