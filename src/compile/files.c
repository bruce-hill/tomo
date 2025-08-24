// This file defines how to compile files

#include "../ast.h"
#include "../config.h"
#include "../environment.h"
#include "../naming.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/paths.h"
#include "../stdlib/tables.h"
#include "../stdlib/text.h"
#include "../typecheck.h"
#include "../types.h"
#include "compilation.h"

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
