// This file defines how to compile files

#include <glob.h>

#include "../ast.h"
#include "../config.h"
#include "../environment.h"
#include "../modules.h"
#include "../naming.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/paths.h"
#include "../stdlib/text.h"
#include "../typecheck.h"
#include "../types.h"
#include "compilation.h"

public
Text_t compile_statement_namespace_header(env_t *env, Path_t header_path, ast_t *ast) {
    env_t *ns_env = NULL;
    ast_t *block = NULL;
    switch (ast->tag) {
    case LangDef: {
        DeclareMatch(def, ast, LangDef);
        ns_env = namespace_env(env, def->name);
        block = def->namespace;
        break;
    }
    case Extend: {
        DeclareMatch(extend, ast, Extend);
        ns_env = namespace_env(env, extend->name);

        env_t *extended = new (env_t);
        *extended = *ns_env;
        extended->locals = new (Table_t, .fallback = env->locals);
        extended->namespace_bindings = new (Table_t, .fallback = env->namespace_bindings);
        extended->id_suffix = env->id_suffix;
        ns_env = extended;

        block = extend->body;
        break;
    }
    case StructDef: {
        DeclareMatch(def, ast, StructDef);
        ns_env = namespace_env(env, def->name);
        block = def->namespace;
        break;
    }
    case EnumDef: {
        DeclareMatch(def, ast, EnumDef);
        ns_env = namespace_env(env, def->name);
        block = def->namespace;
        break;
    }
    case Declare: {
        DeclareMatch(decl, ast, Declare);
        const char *decl_name = Match(decl->var, Var)->name;
        bool is_private = (decl_name[0] == '_');
        if (is_private) return EMPTY_TEXT;

        type_t *t = decl->type ? parse_type_ast(env, decl->type) : get_type(env, decl->value);
        if (t->tag == FunctionType) t = Type(ClosureType, t);
        assert(t->tag != ModuleType);
        if (t->tag == AbortType || t->tag == VoidType || t->tag == ReturnType)
            code_err(ast, "You can't declare a variable with a ", type_to_text(t), " value");

        return Texts(decl->value ? compile_statement_type_header(env, header_path, decl->value) : EMPTY_TEXT, "extern ",
                     compile_declaration(t, namespace_name(env, env->namespace, Text$from_str(decl_name))), ";\n");
    }
    case FunctionDef: return compile_function_declaration(env, ast);
    case ConvertDef: return compile_convert_declaration(env, ast);
    default: return EMPTY_TEXT;
    }
    assert(ns_env);
    Text_t header = EMPTY_TEXT;
    for (ast_list_t *stmt = block ? Match(block, Block)->statements : NULL; stmt; stmt = stmt->next) {
        header = Texts(header, compile_statement_namespace_header(ns_env, header_path, stmt->ast));
    }
    return header;
}

typedef struct {
    env_t *env;
    Text_t *header;
    Path_t header_path;
} compile_typedef_info_t;

static void _make_typedefs(compile_typedef_info_t *info, ast_t *ast) {
    if (ast->tag == StructDef) {
        DeclareMatch(def, ast, StructDef);
        if (def->external) return;
        Text_t struct_name = namespace_name(info->env, info->env->namespace, Texts(def->name, "$$struct"));
        Text_t type_name = namespace_name(info->env, info->env->namespace, Texts(def->name, "$$type"));
        *info->header = Texts(*info->header, "typedef struct ", struct_name, " ", type_name, ";\n");
    } else if (ast->tag == EnumDef) {
        DeclareMatch(def, ast, EnumDef);
        bool has_any_tags_with_fields = false;
        for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
            has_any_tags_with_fields = has_any_tags_with_fields || (tag->fields != NULL);
        }

        if (has_any_tags_with_fields) {
            Text_t struct_name = namespace_name(info->env, info->env->namespace, Texts(def->name, "$$struct"));
            Text_t type_name = namespace_name(info->env, info->env->namespace, Texts(def->name, "$$type"));
            *info->header = Texts(*info->header, "typedef struct ", struct_name, " ", type_name, ";\n");

            for (tag_ast_t *tag = def->tags; tag; tag = tag->next) {
                if (!tag->fields) continue;
                Text_t tag_struct =
                    namespace_name(info->env, info->env->namespace, Texts(def->name, "$", tag->name, "$$struct"));
                Text_t tag_type =
                    namespace_name(info->env, info->env->namespace, Texts(def->name, "$", tag->name, "$$type"));
                *info->header = Texts(*info->header, "typedef struct ", tag_struct, " ", tag_type, ";\n");
            }
        } else {
            Text_t enum_name = namespace_name(info->env, info->env->namespace, Texts(def->name, "$$enum"));
            Text_t type_name = namespace_name(info->env, info->env->namespace, Texts(def->name, "$$type"));
            *info->header = Texts(*info->header, "typedef enum ", enum_name, " ", type_name, ";\n");
        }
    } else if (ast->tag == LangDef) {
        DeclareMatch(def, ast, LangDef);
        *info->header = Texts(*info->header, "typedef Text_t ",
                              namespace_name(info->env, info->env->namespace, Texts(def->name, "$$type")), ";\n");
    }
}

static void _define_types_and_funcs(compile_typedef_info_t *info, ast_t *ast) {
    *info->header = Texts(*info->header, compile_statement_type_header(info->env, info->header_path, ast),
                          compile_statement_namespace_header(info->env, info->header_path, ast));
}

static void add_type_headers(type_ast_t *type_ast, void *userdata) {
    if (!type_ast) return;

    if (type_ast->tag == EnumTypeAST) {
        compile_typedef_info_t *info = (compile_typedef_info_t *)userdata;
        // Force the type to get defined:
        (void)parse_type_ast(info->env, type_ast);
        DeclareMatch(enum_, type_ast, EnumTypeAST);
        bool has_any_tags_with_fields = false;
        for (tag_ast_t *tag = enum_->tags; tag; tag = tag->next) {
            has_any_tags_with_fields = has_any_tags_with_fields || (tag->fields != NULL);
        }

        const char *name = String("enum$", (int64_t)(type_ast->start - type_ast->file->text));
        if (has_any_tags_with_fields) {
            Text_t struct_name = namespace_name(info->env, info->env->namespace, Texts(name, "$$struct"));
            Text_t type_name = namespace_name(info->env, info->env->namespace, Texts(name, "$$type"));
            *info->header = Texts(*info->header, "typedef struct ", struct_name, " ", type_name, ";\n");

            for (tag_ast_t *tag = enum_->tags; tag; tag = tag->next) {
                if (!tag->fields) continue;
                Text_t tag_struct =
                    namespace_name(info->env, info->env->namespace, Texts(name, "$", tag->name, "$$struct"));
                Text_t tag_type =
                    namespace_name(info->env, info->env->namespace, Texts(name, "$", tag->name, "$$type"));
                *info->header = Texts(*info->header, "typedef struct ", tag_struct, " ", tag_type, ";\n");
            }
        } else {
            Text_t enum_name = namespace_name(info->env, info->env->namespace, Texts(name, "$$enum"));
            Text_t type_name = namespace_name(info->env, info->env->namespace, Texts(name, "$$type"));
            *info->header = Texts(*info->header, "typedef enum ", enum_name, " ", type_name, ";\n");
        }

        *info->header = Texts(*info->header, compile_enum_header(info->env, name, enum_->tags));
    }
}

public
Text_t compile_file_header(env_t *env, Path_t header_path, ast_t *ast) {
    Text_t header =
        Texts("#pragma once\n",
              env->do_source_mapping ? Texts("#line 1 ", quoted_str(ast->file->filename), "\n") : EMPTY_TEXT,
              "#include <tomo_" TOMO_VERSION "/tomo.h>\n");

    compile_typedef_info_t info = {.env = env, .header = &header, .header_path = header_path};
    visit_topologically(Match(ast, Block)->statements, (Closure_t){.fn = (void *)_make_typedefs, &info});

    type_ast_visit(ast, add_type_headers, &info);

    visit_topologically(Match(ast, Block)->statements, (Closure_t){.fn = (void *)_define_types_and_funcs, &info});

    header = Texts(header, "void ", namespace_name(env, env->namespace, Text("$initialize")), "(void);\n");
    return header;
}

public
Text_t compile_statement_type_header(env_t *env, Path_t header_path, ast_t *ast) {
    switch (ast->tag) {
    case Use: {
        DeclareMatch(use, ast, Use);
        Path_t source_path = Path$from_str(ast->file->filename);
        Path_t source_dir = Path$parent(source_path);
        Path_t build_dir = Path$resolved(Path$parent(header_path), Path$current_dir());
        switch (use->what) {
        case USE_MODULE: {
            module_info_t mod = get_used_module_info(ast);
            glob_t tm_files;
            const char *folder = mod.version ? String(mod.name, "_", mod.version) : mod.name;
            if (glob(String(TOMO_PATH, "/lib/tomo_" TOMO_VERSION "/", folder, "/[!._0-9]*.tm"), GLOB_TILDE, NULL,
                     &tm_files)
                != 0) {
                if (!try_install_module(mod, true)) code_err(ast, "Could not find library");
            }

            Text_t includes = EMPTY_TEXT;
            for (size_t i = 0; i < tm_files.gl_pathc; i++) {
                const char *filename = tm_files.gl_pathv[i];
                Path_t tm_file = Path$from_str(filename);
                Path_t lib_build_dir = Path$sibling(tm_file, Text(".build"));
                Path_t header = Path$child(lib_build_dir, Texts(Path$base_name(tm_file), Text(".h")));
                includes = Texts(includes, "#include \"", Path$as_c_string(header), "\"\n");
            }
            globfree(&tm_files);
            return with_source_info(env, ast, includes);
        }
        case USE_LOCAL: {
            Path_t used_path = Path$resolved(Path$from_str(use->path), source_dir);
            Path_t used_build_dir = Path$sibling(used_path, Text(".build"));
            Path_t used_header_path = Path$child(used_build_dir, Texts(Path$base_name(used_path), Text(".h")));
            return Texts("#include \"", Path$as_c_string(Path$relative_to(used_header_path, build_dir)), "\"\n");
        }
        case USE_HEADER:
            if (use->path[0] == '<') {
                return Texts("#include ", use->path, "\n");
            } else {
                Path_t used_path = Path$resolved(Path$from_str(use->path), source_dir);
                return Texts("#include \"", Path$as_c_string(Path$relative_to(used_path, build_dir)), "\"\n");
            }
        default: return EMPTY_TEXT;
        }
    }
    case StructDef: {
        return compile_struct_header(env, ast);
    }
    case EnumDef: {
        DeclareMatch(def, ast, EnumDef);
        return compile_enum_header(env, def->name, def->tags);
    }
    case LangDef: {
        DeclareMatch(def, ast, LangDef);
        return Texts(
            // Constructor macro:
            "#define ", namespace_name(env, env->namespace, Text$from_str(def->name)), "(text) ((",
            namespace_name(env, env->namespace, Texts(def->name, "$$type")),
            "){.length=sizeof(text)-1, .tag=TEXT_ASCII, .ascii=\"\" "
            "text})\n"
            "#define ",
            namespace_name(env, env->namespace, Text$from_str(def->name)), "s(...) ((",
            namespace_name(env, env->namespace, Texts(def->name, "$$type")),
            ")Texts(__VA_ARGS__))\n"
            "extern const TypeInfo_t ",
            namespace_name(env, env->namespace, Texts(def->name, Text("$$info"))), ";\n");
    }
    case Extend: {
        return EMPTY_TEXT;
    }
    default: return EMPTY_TEXT;
    }
}
