// Logic for compiling tagged unions (enums)
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
#include "builtins/util.h"

static bool has_extra_data(tag_ast_t *tags)
{
    for (tag_ast_t *tag = tags; tag; tag = tag->next) {
        if (tag->fields) return true;
    }
    return false;
}

static CORD compile_str_method(env_t *env, ast_t *ast)
{
    auto def = Match(ast, EnumDef);
    CORD full_name = CORD_cat(namespace_prefix(env->libname, env->namespace), def->name);
    CORD str_func = CORD_all("static CORD ", full_name, "$as_text(", full_name, "_t *obj, bool use_color) {\n"
                             "\tif (!obj) return \"", def->name, "\";\n"
                             "switch (obj->$tag) {\n");
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        if (!tag->fields) {
            str_func = CORD_all(str_func, "\tcase ", full_name, "$tag$", tag->name, ": return use_color ? \"\\x1b[36;1m",
                         def->name, ".", tag->name, "\\x1b[m\" : \"", def->name, ".", tag->name, "\";\n");
            continue;
        }

        str_func = CORD_all(str_func, "\tcase ", full_name, "$tag$", tag->name, ": return CORD_all(use_color ? \"\\x1b[36;1m",
                     def->name, ".", tag->name, "\\x1b[m(\" : \"", def->name, ".", tag->name, "(\"");

        if (tag->secret) {
            str_func = CORD_cat(str_func, ", \"***)\");\n");
            continue;
        }

        for (arg_ast_t *field = tag->fields; field; field = field->next) {
            type_t *field_t = get_arg_ast_type(env, field);
            CORD field_str = expr_as_text(env, CORD_all("obj->", tag->name, ".", field->name), field_t, "use_color");
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
    CORD full_name = CORD_cat(namespace_prefix(env->libname, env->namespace), def->name);
    if (!has_extra_data(def->tags)) {
        // Comparisons are simpler if there is only a tag, no tagged data:
        return CORD_all("static int ", full_name, "$compare(const ", full_name, "_t *x, const ", full_name,
                        "_t *y, const TypeInfo *info) {\n"
                        "(void)info;\n"
                        "return (x->$tag - y->$tag);\n"
                        "}\n");
    }
    CORD cmp_func = CORD_all("static int ", full_name, "$compare(const ", full_name, "_t *x, const ", full_name,
                             "_t *y, const TypeInfo *info) {\n"
                             "(void)info;\n"
                             "int diff = x->$tag - y->$tag;\n"
                             "if (diff) return diff;\n"
                             "switch (x->$tag) {\n");
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        if (tag->fields) {
            type_t *tag_type = Table$str_get(*env->types, CORD_to_const_char_star(CORD_all(def->name, "$", tag->name)));
            cmp_func = CORD_all(cmp_func, "\tcase ", full_name, "$tag$", tag->name, ": "
                                "return generic_compare(&x->", tag->name, ", &y->", tag->name, ", ", compile_type_info(env, tag_type), ");\n");
        } else {
            cmp_func = CORD_all(cmp_func, "\tcase ", full_name, "$tag$", tag->name, ": return 0;\n");
        }
    }
    cmp_func = CORD_all(cmp_func, "default: return 0;\n}\n}\n");
    return cmp_func;
}

static CORD compile_equals_method(env_t *env, ast_t *ast)
{
    auto def = Match(ast, EnumDef);
    CORD full_name = CORD_cat(namespace_prefix(env->libname, env->namespace), def->name);
    if (!has_extra_data(def->tags)) {
        // Equality is simpler if there is only a tag, no tagged data:
        return CORD_all("static bool ", full_name, "$equal(const ", full_name, "_t *x, const ", full_name,
                        "_t *y, const TypeInfo *info) {\n"
                        "(void)info;\n"
                        "return (x->$tag == y->$tag);\n"
                        "}\n");
    }
    CORD eq_func = CORD_all("static bool ", full_name, "$equal(const ", full_name, "_t *x, const ", full_name,
                             "_t *y, const TypeInfo *info) {\n"
                             "(void)info;\n"
                             "if (x->$tag != y->$tag) return no;\n"
                             "switch (x->$tag) {\n");
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        if (tag->fields) {
            type_t *tag_type = Table$str_get(*env->types, CORD_to_const_char_star(CORD_all(def->name, "$", tag->name)));
            eq_func = CORD_all(eq_func, "\tcase ", full_name, "$tag$", tag->name, ": "
                                "return generic_equal(&x->", tag->name, ", &y->", tag->name, ", ", compile_type_info(env, tag_type), ");\n");
        } else {
            eq_func = CORD_all(eq_func, "\tcase ", full_name, "$tag$", tag->name, ": return yes;\n");
        }
    }
    eq_func = CORD_all(eq_func, "default: return 0;\n}\n}\n");
    return eq_func;
}

static CORD compile_hash_method(env_t *env, ast_t *ast)
{
    auto def = Match(ast, EnumDef);
    CORD full_name = CORD_cat(namespace_prefix(env->libname, env->namespace), def->name);
    if (!has_extra_data(def->tags)) {
        // Hashing is simpler if there is only a tag, no tagged data:
        return CORD_all("static uint32_t ", full_name, "$hash(const ", full_name, "_t *obj, const TypeInfo *info) {\n"
                        "(void)info;\n"
                        "uint32_t hash;\n"
                        "halfsiphash(&obj->$tag, sizeof(obj->$tag), TOMO_HASH_KEY, (uint8_t*)&hash, sizeof(hash));\n"
                        "return hash;"
                        "\n}\n");
    }
    CORD hash_func = CORD_all("static uint32_t ", full_name, "$hash(const ", full_name, "_t *obj, const TypeInfo *info) {\n"
                              "(void)info;\n"
                              "uint32_t hashes[2] = {(uint32_t)obj->$tag, 0};\n"
                              "switch (obj->$tag) {\n");
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        if (tag->fields) {
            type_t *tag_type = Table$str_get(*env->types, CORD_to_const_char_star(CORD_all(def->name, "$", tag->name)));
            hash_func = CORD_all(hash_func, "\tcase ", full_name, "$tag$", tag->name, ": "
                                 "hashes[1] = generic_hash(&obj->", tag->name, ", ", compile_type_info(env, tag_type), ");\n"
                                 "break;\n");
        } else {
            hash_func = CORD_all(hash_func, "\tcase ", full_name, "$tag$", tag->name, ": break;\n");
        }
    }
    hash_func = CORD_all(hash_func, "}\n"
                         "uint32_t hash;\n"
                         "halfsiphash(&hashes, sizeof(hashes), TOMO_HASH_KEY, (uint8_t*)&hash, sizeof(hash));\n"
                         "return hash;\n}\n");
    return hash_func;
}

void compile_enum_def(env_t *env, ast_t *ast)
{
    auto def = Match(ast, EnumDef);
    CORD full_name = CORD_cat(namespace_prefix(env->libname, env->namespace), def->name);
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        compile_struct_def(env, WrapAST(ast, StructDef, .name=CORD_to_const_char_star(CORD_all(def->name, "$", tag->name)), .fields=tag->fields));
        if (tag->fields) { // Constructor macros:
            CORD arg_sig = CORD_EMPTY;
            for (arg_ast_t *field = tag->fields; field; field = field->next) {
                type_t *field_t = get_arg_ast_type(env, field);
                arg_sig = CORD_all(arg_sig, compile_declaration(field_t, field->name));
                if (field->next) arg_sig = CORD_cat(arg_sig, ", ");
            }
            if (arg_sig == CORD_EMPTY) arg_sig = "void";
            CORD constructor_impl = CORD_all("public inline ", full_name, "_t ", full_name, "$tagged$", tag->name, "(", arg_sig, ") { return (",
                                             full_name, "_t){.$tag=", full_name, "$tag$", tag->name, ", .", tag->name, "={");
            for (arg_ast_t *field = tag->fields; field; field = field->next) {
                constructor_impl = CORD_all(constructor_impl, field->name);
                if (field->next) constructor_impl = CORD_cat(constructor_impl, ", ");
            }
            constructor_impl = CORD_cat(constructor_impl, "}}; }\n");
            env->code->funcs = CORD_cat(env->code->funcs, constructor_impl);
        }
    }

    type_t *t = Table$str_get(*env->types, def->name);
    CORD typeinfo = CORD_asprintf("public const TypeInfo %s = {%zu, %zu, {.tag=CustomInfo, .CustomInfo={",
                                  full_name, type_size(t), type_align(t));

    env->code->funcs = CORD_all(env->code->funcs, compile_str_method(env, ast));
    if (has_extra_data(def->tags)) {
        env->code->funcs = CORD_all(
            env->code->funcs, 
            compile_equals_method(env, ast), compile_compare_method(env, ast),
            compile_hash_method(env, ast));
        typeinfo = CORD_all(
            typeinfo,
            ".as_text=(void*)", full_name, "$as_text, "
            ".equal=(void*)", full_name, "$equal, "
            ".hash=(void*)", full_name, "$hash, "
            ".compare=(void*)", full_name, "$compare");
    } else {
        // No need for custom methods if there is no tagged data
        typeinfo = CORD_all(typeinfo, ".as_text=(void*)", full_name, "$as_text");
    }
    typeinfo = CORD_cat(typeinfo, "}}};\n");
    env->code->typeinfos = CORD_all(env->code->typeinfos, typeinfo);

    compile_namespace(env, def->name, def->namespace);
}

CORD compile_enum_typedef(env_t *env, ast_t *ast)
{
    auto def = Match(ast, EnumDef);
    CORD full_name = CORD_cat(namespace_prefix(env->libname, env->namespace), def->name);
    CORD all_defs = CORD_all("typedef struct ", full_name, "_s ", full_name, "_t;\n");
    CORD enum_def = CORD_all("struct ", full_name, "_s {\n"
                             "\tenum {");
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        CORD_appendf(&enum_def, "%r$tag$%s = %ld", full_name, tag->name, tag->value);
        if (tag->next) enum_def = CORD_cat(enum_def, ", ");
    }
    enum_def = CORD_cat(enum_def, "} $tag;\n"
                        "union {\n");
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        CORD field_def = compile_struct_typedef(env, WrapAST(ast, StructDef, .name=CORD_to_const_char_star(CORD_all(def->name, "$", tag->name)), .fields=tag->fields));
        all_defs = CORD_all(all_defs, field_def);
        enum_def = CORD_all(enum_def, full_name, "$", tag->name, "_t ", tag->name, ";\n");
    }
    enum_def = CORD_cat(enum_def, "};\n};\n");
    all_defs = CORD_all(all_defs, enum_def);
    return all_defs;
}

CORD compile_enum_declarations(env_t *env, ast_t *ast)
{
    auto def = Match(ast, EnumDef);
    CORD full_name = CORD_cat(namespace_prefix(env->libname, env->namespace), def->name);
    CORD all_defs = CORD_all("extern const TypeInfo ", full_name, ";\n");
    for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
        all_defs = CORD_all(all_defs,
                            "extern const TypeInfo ", namespace_prefix(env->libname, env->namespace), def->name, "$", tag->name, ";\n");
        if (tag->fields) { // Constructor macros:
            CORD arg_sig = CORD_EMPTY;
            for (arg_ast_t *field = tag->fields; field; field = field->next) {
                type_t *field_t = get_arg_ast_type(env, field);
                arg_sig = CORD_all(arg_sig, compile_declaration(field_t, field->name));
                if (field->next) arg_sig = CORD_cat(arg_sig, ", ");
            }
            if (arg_sig == CORD_EMPTY) arg_sig = "void";
            CORD constructor_def = CORD_all(full_name, "_t ", full_name, "$tagged$", tag->name, "(", arg_sig, ");\n");
            all_defs = CORD_cat(all_defs, constructor_def);
        }
    }
    return CORD_all(all_defs, compile_namespace_definitions(env, def->name, def->namespace));
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
