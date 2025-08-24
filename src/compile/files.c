// This file defines how to compile files

#include <glob.h>

#include "../ast.h"
#include "../config.h"
#include "../environment.h"
#include "../modules.h"
#include "../naming.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/paths.h"
#include "../stdlib/tables.h"
#include "../stdlib/text.h"
#include "../typecheck.h"
#include "../types.h"
#include "declarations.h"
#include "enums.h"
#include "files.h"
#include "functions.h"
#include "statements.h"
#include "structs.h"
#include "text.h"
#include "types.h"

static void initialize_vars_and_statics(env_t *env, ast_t *ast) {
    if (!ast) return;

    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        if (stmt->ast->tag == InlineCCode) {
            Text_t code = compile_statement(env, stmt->ast);
            env->code->staticdefs = Texts(env->code->staticdefs, code, "\n");
        } else if (stmt->ast->tag == Declare) {
            DeclareMatch(decl, stmt->ast, Declare);
            const char *decl_name = Match(decl->var, Var)->name;
            Text_t full_name = namespace_name(env, env->namespace, Text$from_str(decl_name));
            type_t *t = decl->type ? parse_type_ast(env, decl->type) : get_type(env, decl->value);
            if (t->tag == FunctionType) t = Type(ClosureType, t);
            Text_t val_code = compile_declared_value(env, stmt->ast);
            if ((decl->value && !is_constant(env, decl->value)) || (!decl->value && has_heap_memory(t))) {
                Text_t initialized_name = namespace_name(env, env->namespace, Texts(decl_name, "$$initialized"));
                env->code->variable_initializers =
                    Texts(env->code->variable_initializers,
                          with_source_info(env, stmt->ast,
                                           Texts(full_name, " = ", val_code, ",\n", initialized_name, " = true;\n")));
            }
        } else if (stmt->ast->tag == StructDef) {
            initialize_vars_and_statics(namespace_env(env, Match(stmt->ast, StructDef)->name),
                                        Match(stmt->ast, StructDef)->namespace);
        } else if (stmt->ast->tag == EnumDef) {
            initialize_vars_and_statics(namespace_env(env, Match(stmt->ast, EnumDef)->name),
                                        Match(stmt->ast, EnumDef)->namespace);
        } else if (stmt->ast->tag == LangDef) {
            initialize_vars_and_statics(namespace_env(env, Match(stmt->ast, LangDef)->name),
                                        Match(stmt->ast, LangDef)->namespace);
        } else if (stmt->ast->tag == Extend) {
            initialize_vars_and_statics(namespace_env(env, Match(stmt->ast, Extend)->name),
                                        Match(stmt->ast, Extend)->body);
        } else if (stmt->ast->tag == Use) {
            continue;
        } else {
            Text_t code = compile_statement(env, stmt->ast);
            if (code.length > 0) code_err(stmt->ast, "I did not expect this to generate code");
        }
    }
}

static Text_t compile_top_level_code(env_t *env, ast_t *ast) {
    if (!ast) return EMPTY_TEXT;

    switch (ast->tag) {
    case Use: {
        // DeclareMatch(use, ast, Use);
        // if (use->what == USE_C_CODE) {
        //     Path_t path = Path$relative_to(Path$from_str(use->path),
        //     Path(".build")); return Texts("#include \"",
        //     Path$as_c_string(path),
        //     "\"\n");
        // }
        return EMPTY_TEXT;
    }
    case Declare: {
        DeclareMatch(decl, ast, Declare);
        const char *decl_name = Match(decl->var, Var)->name;
        Text_t full_name = namespace_name(env, env->namespace, Text$from_str(decl_name));
        type_t *t = decl->type ? parse_type_ast(env, decl->type) : get_type(env, decl->value);
        if (t->tag == FunctionType) t = Type(ClosureType, t);
        Text_t val_code = compile_declared_value(env, ast);
        bool is_private = decl_name[0] == '_';
        if ((decl->value && is_constant(env, decl->value)) || (!decl->value && !has_heap_memory(t))) {
            set_binding(env, decl_name, t, full_name);
            return Texts(is_private ? "static " : "public ", compile_declaration(t, full_name), " = ", val_code, ";\n");
        } else {
            Text_t init_var = namespace_name(env, env->namespace, Texts(decl_name, "$$initialized"));
            Text_t checked_access = Texts("check_initialized(", full_name, ", ", init_var, ", \"", decl_name, "\")");
            set_binding(env, decl_name, t, checked_access);

            Text_t initialized_name = namespace_name(env, env->namespace, Texts(decl_name, "$$initialized"));
            return Texts("static bool ", initialized_name, " = false;\n", is_private ? "static " : "public ",
                         compile_declaration(t, full_name), ";\n");
        }
    }
    case FunctionDef: {
        Text_t name_code =
            namespace_name(env, env->namespace, Text$from_str(Match(Match(ast, FunctionDef)->name, Var)->name));
        return compile_function(env, name_code, ast, &env->code->staticdefs);
    }
    case ConvertDef: {
        type_t *type = get_function_def_type(env, ast);
        const char *name = get_type_name(Match(type, FunctionType)->ret);
        if (!name)
            code_err(ast,
                     "Conversions are only supported for text, struct, and enum "
                     "types, not ",
                     type_to_str(Match(type, FunctionType)->ret));
        Text_t name_code =
            namespace_name(env, env->namespace, Texts(name, "$", String(get_line_number(ast->file, ast->start))));
        return compile_function(env, name_code, ast, &env->code->staticdefs);
    }
    case StructDef: {
        DeclareMatch(def, ast, StructDef);
        type_t *t = Table$str_get(*env->types, def->name);
        assert(t && t->tag == StructType);
        Text_t code = compile_struct_typeinfo(env, t, def->name, def->fields, def->secret, def->opaque);
        env_t *ns_env = namespace_env(env, def->name);
        return Texts(code, def->namespace ? compile_top_level_code(ns_env, def->namespace) : EMPTY_TEXT);
    }
    case EnumDef: {
        DeclareMatch(def, ast, EnumDef);
        Text_t code = compile_enum_typeinfo(env, ast);
        code = Texts(code, compile_enum_constructors(env, ast));
        env_t *ns_env = namespace_env(env, def->name);
        return Texts(code, def->namespace ? compile_top_level_code(ns_env, def->namespace) : EMPTY_TEXT);
    }
    case LangDef: {
        DeclareMatch(def, ast, LangDef);
        Text_t code =
            Texts("public const TypeInfo_t ", namespace_name(env, env->namespace, Texts(def->name, "$$info")), " = {",
                  String((int64_t)sizeof(Text_t)), ", ", String((int64_t)__alignof__(Text_t)),
                  ", .metamethods=Text$metamethods, .tag=TextInfo, .TextInfo={", quoted_str(def->name), "}};\n");
        env_t *ns_env = namespace_env(env, def->name);
        return Texts(code, def->namespace ? compile_top_level_code(ns_env, def->namespace) : EMPTY_TEXT);
    }
    case Extend: {
        DeclareMatch(extend, ast, Extend);
        binding_t *b = get_binding(env, extend->name);
        if (!b || b->type->tag != TypeInfoType)
            code_err(ast, "'", extend->name, "' is not the name of any type I recognize.");
        env_t *ns_env = Match(b->type, TypeInfoType)->env;
        env_t *extended = new (env_t);
        *extended = *ns_env;
        extended->locals = new (Table_t, .fallback = env->locals);
        extended->namespace_bindings = new (Table_t, .fallback = env->namespace_bindings);
        extended->id_suffix = env->id_suffix;
        return compile_top_level_code(extended, extend->body);
    }
    case Extern: return EMPTY_TEXT;
    case Block: {
        Text_t code = EMPTY_TEXT;
        for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
            code = Texts(code, compile_top_level_code(env, stmt->ast));
        }
        return code;
    }
    default: return EMPTY_TEXT;
    }
}

public
Text_t compile_file(env_t *env, ast_t *ast) {
    Text_t top_level_code = compile_top_level_code(env, ast);
    Text_t includes = EMPTY_TEXT;
    Text_t use_imports = EMPTY_TEXT;

    // First prepare variable initializers to prevent unitialized access:
    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        if (stmt->ast->tag == Use) {
            use_imports = Texts(use_imports, compile_statement(env, stmt->ast));

            DeclareMatch(use, stmt->ast, Use);
            if (use->what == USE_C_CODE) {
                Path_t path = Path$relative_to(Path$from_str(use->path), Path(".build"));
                includes = Texts(includes, "#include \"", Path$as_c_string(path), "\"\n");
            }
        }
    }

    initialize_vars_and_statics(env, ast);

    const char *name = file_base_name(ast->file->filename);
    return Texts(env->do_source_mapping ? Texts("#line 1 ", quoted_str(ast->file->filename), "\n") : EMPTY_TEXT,
                 "#define __SOURCE_FILE__ ", quoted_str(ast->file->filename), "\n",
                 "#include <tomo_" TOMO_VERSION "/tomo.h>\n"
                 "#include \"",
                 name, ".tm.h\"\n\n", includes, env->code->local_typedefs, "\n", env->code->lambdas, "\n",
                 env->code->staticdefs, "\n", top_level_code, "public void ",
                 namespace_name(env, env->namespace, Text("$initialize")), "(void) {\n",
                 "static bool initialized = false;\n", "if (initialized) return;\n", "initialized = true;\n",
                 use_imports, env->code->variable_initializers, "}\n");
}

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
    case Extern: {
        DeclareMatch(ext, ast, Extern);
        type_t *t = parse_type_ast(env, ext->type);
        Text_t decl;
        if (t->tag == ClosureType) {
            t = Match(t, ClosureType)->fn;
            DeclareMatch(fn, t, FunctionType);
            decl = Texts(compile_type(fn->ret), " ", ext->name, "(");
            for (arg_t *arg = fn->args; arg; arg = arg->next) {
                decl = Texts(decl, compile_type(arg->type));
                if (arg->next) decl = Texts(decl, ", ");
            }
            decl = Texts(decl, ")");
        } else {
            decl = compile_declaration(t, Text$from_str(ext->name));
        }
        return Texts("extern ", decl, ";\n");
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
            code_err(ast, "You can't declare a variable with a ", type_to_str(t), " value");

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

public
Text_t compile_file_header(env_t *env, Path_t header_path, ast_t *ast) {
    Text_t header =
        Texts("#pragma once\n",
              env->do_source_mapping ? Texts("#line 1 ", quoted_str(ast->file->filename), "\n") : EMPTY_TEXT,
              "#include <tomo_" TOMO_VERSION "/tomo.h>\n");

    compile_typedef_info_t info = {.env = env, .header = &header, .header_path = header_path};
    visit_topologically(Match(ast, Block)->statements, (Closure_t){.fn = (void *)_make_typedefs, &info});
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
            module_info_t mod = get_module_info(ast);
            glob_t tm_files;
            const char *folder = mod.version ? String(mod.name, "_", mod.version) : mod.name;
            if (glob(String(TOMO_PREFIX "/share/tomo_" TOMO_VERSION "/installed/", folder, "/[!._0-9]*.tm"), GLOB_TILDE,
                     NULL, &tm_files)
                != 0) {
                if (!try_install_module(mod)) code_err(ast, "Could not find library");
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
        return compile_enum_header(env, ast);
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
