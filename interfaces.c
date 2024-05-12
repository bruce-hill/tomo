// Logic for compiling interfaces
#include <ctype.h>
#include <gc/cord.h>
#include <gc.h>
#include <stdio.h>

#include "ast.h"
#include "builtins/text.h"
#include "compile.h"
#include "interfaces.h"
#include "environment.h"
#include "typecheck.h"
#include "builtins/util.h"

static CORD compile_str_method(env_t *env, ast_t *ast)
{
    auto def = Match(ast, InterfaceDef);
    CORD full_name = CORD_cat(env->file_prefix, def->name);
    const char *name = def->name;
    const char *dollar = strrchr(name, '$');
    if (dollar) name = dollar + 1;
    CORD str_func = CORD_asprintf("static CORD %r$as_text(%r_t *interface, bool use_color) {\n"
                                  "\tif (!interface) return \"%s\";\n", full_name, full_name, name);
    return CORD_all(str_func, "\treturn CORD_asprintf(use_color ? \"\\x1b[0;1m", name, "\\x1b[m<\\x1b[36m%p\\x1b[m>\" : \"", name, "<%p>\", interface->$obj);\n}");
}

static CORD compile_compare_method(env_t *env, ast_t *ast)
{
    auto def = Match(ast, InterfaceDef);
    CORD full_name = CORD_cat(env->file_prefix, def->name);
    return CORD_all("static int ", full_name, "$compare(const ", full_name, "_t *x, const ", full_name,
                    "_t *y, const TypeInfo *info) {\n"
                    "(void)info;\n",
                    "return (x->$obj > y->$obj) - (x->$obj < y->$obj);\n}");
}

static CORD compile_equals_method(env_t *env, ast_t *ast)
{
    auto def = Match(ast, InterfaceDef);
    CORD full_name = CORD_cat(env->file_prefix, def->name);
    return CORD_all("static bool ", full_name, "$equal(const ", full_name, "_t *x, const ", full_name,
                    "_t *y, const TypeInfo *info) {\n"
                    "(void)info;\n",
                    "return (x->$obj == y->$obj);\n}");
}

static CORD compile_hash_method(env_t *env, ast_t *ast)
{
    auto def = Match(ast, InterfaceDef);
    CORD full_name = CORD_cat(env->file_prefix, def->name);
    return CORD_all("static uint32_t ", full_name, "$hash(const ", full_name, "_t *interface, const TypeInfo *info) {\n"
                    "(void)info;\n"
                    "uint32_t hash;\n"
                    "halfsiphash(&interface->$obj, sizeof(void*), TOMO_HASH_VECTOR, (uint8_t*)&hash, sizeof(hash));\n"
                    "return hash;\n}\n");
}

void compile_interface_def(env_t *env, ast_t *ast)
{
    auto def = Match(ast, InterfaceDef);
    CORD full_name = CORD_cat(env->file_prefix, def->name);
    CORD_appendf(&env->code->typedefs, "typedef struct %r_s %r_t;\n", full_name, full_name);
    CORD_appendf(&env->code->typedefs, "#define %r(...) ((%r_t){__VA_ARGS__})\n", full_name, full_name);

    CORD_appendf(&env->code->typecode, "struct %r_s {\nvoid *$obj;\n", full_name);
    type_ast_t *replacement_type_ast = NewTypeAST(ast->file, ast->start, ast->end, VarTypeAST, .name=def->name);
    for (arg_ast_t *field = def->fields; field; field = field->next) {
        type_ast_t *field_type = replace_type_ast(field->type, def->type_parameter, replacement_type_ast);
        type_t *field_t = parse_type_ast(env, field_type);
        if (field_t->tag == ClosureType)
            field_t = Match(field_t, ClosureType)->fn;
        CORD decl = compile_declaration(env, field_t, field->name);
        CORD_appendf(&env->code->typecode, "%r%s;\n", decl,
                     field_t->tag == BoolType ? ":1" : "");
    }
    CORD_appendf(&env->code->typecode, "};\n");

    // Typeinfo:
    CORD_appendf(&env->code->typedefs, "extern const TypeInfo %r;\n", full_name);

    type_t *t = Table$str_get(*env->types, def->name);
    assert(t && t->tag == InterfaceType);
    auto interface = Match(t, InterfaceType);
    CORD typeinfo = CORD_asprintf("public const TypeInfo %r = {%zu, %zu, {.tag=CustomInfo, .CustomInfo={",
                                  full_name, type_size(t), type_align(t));

    typeinfo = CORD_all(typeinfo, ".as_text=(void*)", full_name, "$as_text, ");
    env->code->funcs = CORD_all(env->code->funcs, compile_str_method(env, ast));

    if (interface->fields && !interface->fields->next) { // Single member, can just use its methods
        type_t *member_t = interface->fields->type;
        switch (member_t->tag) {
        case TextType:
            typeinfo = CORD_all(typeinfo, ".hash=(void*)", type_to_cord(member_t), "$hash", ", ");
            // fallthrough
        case IntType: case NumType:
            typeinfo = CORD_all(typeinfo, ".compare=(void*)", type_to_cord(member_t), "$compare, "
                                ".equal=(void*)", type_to_cord(member_t), "$equal, ");
            // fallthrough
        case BoolType: goto got_methods;
        default: break;
        }
    }
    if (interface->fields) {
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

    compile_namespace(env, def->name, def->namespace);
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
