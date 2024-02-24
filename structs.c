
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
            type_t *field_t = parse_type_ast(env, field->type);
            CORD field_str = expr_as_string(env, CORD_cat("obj->", field->name), field_t, "use_color");
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
                             "int diff;\n");
    for (arg_ast_t *field = def->fields; field; field = field->next) {
        type_t *field_type = parse_type_ast(env, field->type);
        cmp_func = CORD_all(cmp_func, "diff = generic_compare(&x->", field->name, ", &y->", field->name, ", ",
                            compile_type_info(env, field_type), ");\n"
                            "if (diff != 0) return diff;\n");
    }
    cmp_func = CORD_all(cmp_func, "return 0;\n}\n");
    return cmp_func;
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
    CORD_appendf(&env->code->typedefs, "typedef struct { TypeInfo type; } %s_namespace_t;\n", def->name);
    CORD_appendf(&env->code->typedefs, "extern %s_namespace_t %s;\n", def->name, def->name);

    env->code->funcs = CORD_all(
        env->code->funcs, compile_compare_method(env, ast), compile_str_method(env, ast));
    env->code->typeinfos = CORD_all(
        env->code->typeinfos,
        "public ", def->name, "_namespace_t ", def->name, " = {{.tag=CustomInfo, .CustomInfo={"
        ".as_str=(void*)", def->name, "__as_str, "
        ".compare=(void*)", def->name, "__compare}}};\n");
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
