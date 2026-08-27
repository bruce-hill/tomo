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

static void initialize_vars_and_statics(env_t *env, ast_t *ast);
static void initialize_namespace(env_t *env, const char *name, ast_t *namespace);
static Text_t compile_top_level_code(env_t *env, ast_t *ast);
static Text_t compile_namespace(env_t *env, const char *name, ast_t *namespace);

void initialize_namespace(env_t *env, const char *name, ast_t *namespace) {
    initialize_vars_and_statics(namespace_env(env, name), namespace);
}

void initialize_vars_and_statics(env_t *env, ast_t *ast) {
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
            bool value_is_constant =
                decl->value
                && (decl->value->tag == Embed ? embed_is_constant(decl->value, t) : is_constant(env, decl->value, t));
            if ((decl->value && !value_is_constant) || (!decl->value && has_heap_memory(t))) {
                // Only non-constant values need runtime initialization here.
                // (Compile the value inside this branch, not above -- for a
                // constant value it's emitted as a static initializer by
                // compile_top_level_code instead, and compiling it here too
                // would duplicate any hoisted static defs, e.g. a list
                // literal's backing array.)
                Text_t val_code = compile_declared_value(env, stmt->ast);
                Text_t initialized_name = namespace_name(env, env->namespace, Texts(decl_name, "$$initialized"));
                env->code->variable_initializers =
                    Texts(env->code->variable_initializers,
                          with_source_info(env, stmt->ast,
                                           Texts(full_name, " = ", val_code, ",\n", initialized_name, " = true;\n")));
            }
        } else if (stmt->ast->tag == StructDef) {
            initialize_namespace(env, Match(stmt->ast, StructDef)->name, Match(stmt->ast, StructDef)->namespace);
        } else if (stmt->ast->tag == EnumDef) {
            initialize_namespace(env, Match(stmt->ast, EnumDef)->name, Match(stmt->ast, EnumDef)->namespace);
        } else if (stmt->ast->tag == LangDef) {
            initialize_namespace(env, Match(stmt->ast, LangDef)->name, Match(stmt->ast, LangDef)->namespace);
        } else if (stmt->ast->tag == Use) {
            continue;
        } else if (stmt->ast->tag == Test) {
            // Tests are only emitted by `tomo test`, never in normal builds:
            continue;
        } else {
            Text_t code = compile_statement(env, stmt->ast);
            if (code.length > 0)
                env->code->variable_initializers =
                    Texts(env->code->variable_initializers, with_source_info(env, stmt->ast, code));
        }
    }
}

Text_t compile_namespace(env_t *env, const char *name, ast_t *namespace) {
    env_t *ns_env = namespace_env(env, name);
    return namespace ? compile_top_level_code(ns_env, namespace) : EMPTY_TEXT;
}

Text_t compile_top_level_code(env_t *env, ast_t *ast) {
    if (!ast) return EMPTY_TEXT;

    switch (ast->tag) {
    case Use: {
        // DeclareMatch(use, ast, Use);
        // if (use->what == USE_C_CODE) {
        //     Path_t path = Path$relative_to(Path$from_str(use->path),
        //     Path(".tomo")); return Texts("#include \"",
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
        bool value_is_constant =
            decl->value
            && (decl->value->tag == Embed ? embed_is_constant(decl->value, t) : is_constant(env, decl->value, t));
        if (value_is_constant || (!decl->value && !has_heap_memory(t))) {
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
        // Dots in subcommand names (`main.add`) become `$`s in the C name:
        Text_t name_code = namespace_name(
            env, env->namespace,
            Text$replace(Text$from_str(Match(Match(ast, FunctionDef)->name, Var)->name), Text("."), Text("$")));
        return compile_function(env, name_code, ast, &env->code->staticdefs);
    }
    case ConvertDef: {
        type_t *type = get_function_return_type(env, ast);
        const char *name = get_type_name(type);
        if (!name)
            code_err(ast,
                     "Conversions are only supported for text, struct, and enum "
                     "types, not ",
                     type_to_text(type));
        Text_t name_code =
            namespace_name(env, env->namespace, Texts(name, "$", get_line_number(ast->file, ast->start)));
        return compile_function(env, name_code, ast, &env->code->staticdefs);
    }
    case StructDef: {
        DeclareMatch(def, ast, StructDef);
        type_t *t = Table$str_get(*env->types, def->name);
        assert(t && t->tag == StructType);
        Text_t code = compile_struct_typeinfo(env, t, def->name, def->fields, def->secret, def->opaque);
        return Texts(code, compile_namespace(env, def->name, def->namespace));
    }
    case EnumDef: {
        DeclareMatch(def, ast, EnumDef);
        Text_t code = compile_enum_typeinfo(env, def->name, def->tags);
        code = Texts(code, compile_enum_constructors(env, def->name, def->tags));
        return Texts(code, compile_namespace(env, def->name, def->namespace));
    }
    case LangDef: {
        DeclareMatch(def, ast, LangDef);
        Text_t code =
            Texts("public const TypeInfo_t ", namespace_name(env, env->namespace, Texts(def->name, "$$info")), " = {",
                  (int64_t)sizeof(Text_t), ", ", (int64_t)__alignof__(Text_t),
                  ", .metamethods=Text$metamethods, .tag=TextInfo, .TextInfo={", quoted_str(def->name), "}};\n");
        return Texts(code, compile_namespace(env, def->name, def->namespace));
    }
    case Block: {
        Text_t code = EMPTY_TEXT;
        for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
            code = Texts(code, compile_top_level_code(env, stmt->ast));
        }
        return code;
    }
    case Metadata:
    default: return EMPTY_TEXT;
    }
}

typedef struct {
    env_t *env;
    Text_t *code;
} compile_info_t;

static visit_behavior_t add_type_infos(type_ast_t *type_ast, void *userdata) {
    if (type_ast && type_ast->tag == EnumTypeAST) {
        compile_info_t *info = (compile_info_t *)userdata;
        // Force the type to get defined:
        (void)parse_type_ast(info->env, type_ast);
        *info->code = Texts(
            *info->code,
            compile_enum_typeinfo(info->env, String("enum$", (int64_t)(type_ast->start - type_ast->file->text)),
                                  Match(type_ast, EnumTypeAST)->tags),
            compile_enum_constructors(info->env, String("enum$", (int64_t)(type_ast->start - type_ast->file->text)),
                                      Match(type_ast, EnumTypeAST)->tags));
    }
    return VISIT_PROCEED;
}

public
Text_t compile_file(env_t *env, ast_t *ast) {
    Text_t top_level_code = compile_top_level_code(env, ast);

    compile_info_t info = {.env = env, .code = &top_level_code};
    type_ast_visit(ast, add_type_infos, &info);

    Text_t includes = EMPTY_TEXT;
    Text_t use_imports = EMPTY_TEXT;

    // First prepare variable initializers to prevent unitialized access:
    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        if (stmt->ast->tag == Use) {
            use_imports = Texts(use_imports, compile_statement(env, stmt->ast));

            DeclareMatch(use, stmt->ast, Use);
            if (use->what == USE_C_CODE) {
                Path_t path = Path$from_str(use->path);
                if (path[0] != '/') {
                    // If we have `use ./foo.c`, then we need to remap it in source code to
                    // `#include "../foo.c"`, since it will be inside the .tomo directory.
                    Path_t parent = Path$parent(Path(ast->file->filename));
                    path = Path$relative_to(Path$resolved(Path$from_str(use->path), parent), parent);
                    path = Path$concat("..", path);
                }
                includes = Texts(includes, "#include \"", Path$as_c_string(path), "\"\n");
            }
        }
    }

    initialize_vars_and_statics(env, ast);

    const char *name = file_base_name(ast->file->filename);
    return Texts(env->do_source_mapping ? Texts("#line 1 ", quoted_str(ast->file->filename), "\n") : EMPTY_TEXT,
                 "#define __SOURCE_FILE__ ", quoted_str(ast->file->filename), "\n",
                 "#include <tomo.h>\n"
                 "#include \"",
                 name, ".tm.h\"\n\n", includes, env->code->constants, "\n", env->code->local_typedefs, "\n",
                 env->code->lambdas, "\n", env->code->staticdefs, "\n", top_level_code, "public void ",
                 namespace_name(env, env->namespace, Text("$initialize")), "(void) {\n",
                 "static bool initialized = false;\n", "if (initialized) return;\n", "initialized = true;\n",
                 use_imports, env->code->variable_initializers, "}\n");
}

public
Text_t compile_test_runner(env_t *env, ast_t *ast, int64_t *out_count) {
    // Compile each test body first so any lambdas/static helpers they generate
    // accumulate into env->code and get emitted by compile_file() below:
    Text_t test_fns = EMPTY_TEXT, descriptors = EMPTY_TEXT;
    int64_t count = 0;
    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        if (stmt->ast->tag != Test) continue;
        DeclareMatch(test, stmt->ast, Test);
        if (test->expected_compile_error) continue; // handled by the driver's frontend
        env_t *scope = fresh_scope(env);
        Text_t body = compile_block(scope, test->body);
        Text_t fn_name = Texts("test$", count);
        test_fns = Texts(test_fns, "static void ", fn_name, "(void) ", body, "\n");
        descriptors =
            Texts(descriptors, "{.label=", quoted_str(test->label), ", .fn=", fn_name,
                  ", .expect_failure=", test->expected_failure ? "true" : "false", ", .expected_msg=",
                  (test->expected_failure && test->expected_failure[0]) ? quoted_str(test->expected_failure)
                                                                        : Text("NULL"),
                  ", .first_line=", get_line_number(ast->file, stmt->ast->start), ", .last_line=",
                  get_line_number(ast->file, stmt->ast->end), "},\n");
        count += 1;
    }
    *out_count = count;
    if (count == 0) return EMPTY_TEXT;

    // Emit the entire module into the runner's own translation unit -- including
    // its file-private (`static`) helpers -- so the test bodies above can call
    // them. Because the module's code lives here, the driver must NOT also link
    // the module's normal object file (that would duplicate every public
    // symbol); see build_test_runner(). compile_file() emits the accumulated
    // lambdas/staticdefs (module + tests), so append the test functions after.
    Text_t module_code = compile_file(env, ast);

    return Texts(module_code, "\n", test_fns, "static tomo_test_t _tomo_tests[] = {\n", descriptors,
                 "};\n"
                 "int main(int argc, char *argv[]) {\n"
                 "(void)argc; (void)argv;\n"
                 "tomo_init();\n",
                 namespace_name(env, env->namespace, Text("$initialize")),
                 "();\n"
                 "return _tomo_run_tests(_tomo_tests, ",
                 count, ");\n}\n");
}
