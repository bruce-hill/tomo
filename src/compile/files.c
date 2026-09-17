// This file defines how to compile files

#include "../ast.h"
#include "../config.h"
#include "../environment.h"
#include "../naming.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/path.h"
#include "../stdlib/table.h"
#include "../stdlib/text.h"
#include "../typecheck.h"
#include "../types.h"
#include "compilation.h"

// A file is compiled in three passes over the same AST:
//
//  1. compile_variables(): the file-scoped and type-namespace-scoped variables and type infos.
//  2. compile_functions(): functions and type methods.
//  3. compile_initializers(): the code that runs inside `$initialize()` to assign values
//     for those variables that can't use static initializers, plus other top-level statements.
static Text_t compile_variables(env_t *env, ast_t *ast);
static Text_t compile_namespace_variables(env_t *env, const char *name, ast_t *namespace);
static Text_t compile_functions(env_t *env, ast_t *ast);
static Text_t compile_namespace_functions(env_t *env, const char *name, ast_t *namespace);
static Text_t compile_initializers(env_t *env, ast_t *ast);
static Text_t compile_namespace_initializers(env_t *env, const char *name, ast_t *namespace);

Text_t compile_namespace_variables(env_t *env, const char *name, ast_t *namespace) {
    return compile_variables(namespace_env(env, name), namespace);
}

Text_t compile_variables(env_t *env, ast_t *ast) {
    if (!ast) return EMPTY_TEXT;

    switch (ast->tag) {
    case Declare: {
        DeclareMatch(decl, ast, Declare);
        const char *decl_name = Match(decl->var, Var)->name;
        Text_t full_name = namespace_name(env, env->namespace, Text$from_str(decl_name));
        // Variables and functions starting with `_` are private to the file, i.e. `static` in C.
        // Everything else is visible to importers and declared in the header.
        Text_t linkage = decl_name[0] == '_' ? Text("static ") : Text("public ");
        type_t *t = NULL;
        if (needs_runtime_initialization(env, ast, &t)) {
            // Runtime-initialized variables have an accompanying boolean saying whether
            // they're initialized or not, so we can do lazy and once-only initialization.
            Text_t initialized_name = namespace_name(env, env->namespace, Texts(decl_name, "$$initialized"));
            return Texts(linkage, "bool ", initialized_name, " = false;\n", linkage, compile_declaration(t, full_name),
                         ";\n");
        } else {
            return Texts(linkage, compile_declaration(t, full_name), " = ", compile_declared_value(env, ast), ";\n");
        }
    }
    case StructDef: {
        DeclareMatch(def, ast, StructDef);
        type_t *t = Table$str_get(*env->types, def->name);
        assert(t && t->tag == StructType);
        Text_t code = compile_struct_typeinfo(env, t, def->name, def->fields, def->secret, def->opaque);
        return Texts(code, compile_namespace_variables(env, def->name, def->namespace));
    }
    case EnumDef: {
        DeclareMatch(def, ast, EnumDef);
        Text_t code = compile_enum_typeinfo(env, def->name, def->tags);
        return Texts(code, compile_namespace_variables(env, def->name, def->namespace));
    }
    case LangDef: {
        DeclareMatch(def, ast, LangDef);
        Text_t code =
            Texts("public const TypeInfo_t ", namespace_name(env, env->namespace, Texts(def->name, "$$info")), " = {",
                  (int64_t)sizeof(Text_t), ", ", (int64_t)__alignof__(Text_t),
                  ", .metamethods=Text$metamethods, .tag=TextInfo, .TextInfo={", quoted_str(def->name), "}};\n");
        return Texts(code, compile_namespace_variables(env, def->name, def->namespace));
    }
    case Block: {
        Text_t code = EMPTY_TEXT;
        for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
            code = Texts(code, compile_variables(env, stmt->ast));
        }
        return code;
    }
    case InlineCCode: {
        // Inline C code goes with variable declarations so it can be referenced inside functions.
        // Top-level C code can be used to declare variables and macros and whatnot. If you want
        // top-level C code that runs on initialization, you can wrap it in `do C_code"..."`
        Text_t stmt_code = compile_statement(env, ast);
        return with_source_info(env, ast, stmt_code);
    }
    default: return EMPTY_TEXT;
    }
}

Text_t compile_namespace_functions(env_t *env, const char *name, ast_t *namespace) {
    return compile_functions(namespace_env(env, name), namespace);
}

Text_t compile_functions(env_t *env, ast_t *ast) {
    if (!ast) return EMPTY_TEXT;

    switch (ast->tag) {
    case FunctionDef: {
        Text_t name_code = namespace_name(
            env, env->namespace,
            // Dots in subcommand names (`main.add`) become `$`s in the C name:
            Text$replace(Text$from_str(Match(Match(ast, FunctionDef)->name, Var)->name), Text("."), Text("$")));
        return compile_function(env, name_code, ast);
    }
    case ConvertDef: {
        type_t *type = get_function_return_type(env, ast);
        const char *name = get_type_name(type);
        if (!name)
            code_err(ast, "Conversions are only supported for text, struct, and enum types, not ", type_to_text(type));
        Text_t name_code =
            namespace_name(env, env->namespace, Texts(name, "$", get_line_number(ast->file, ast->start)));
        return compile_function(env, name_code, ast);
    }
    case StructDef: {
        DeclareMatch(def, ast, StructDef);
        return compile_namespace_functions(env, def->name, def->namespace);
    }
    case EnumDef: {
        DeclareMatch(def, ast, EnumDef);
        Text_t code = compile_enum_constructors(env, def->name, def->tags);
        return Texts(code, compile_namespace_functions(env, def->name, def->namespace));
    }
    case LangDef: {
        DeclareMatch(def, ast, LangDef);
        return compile_namespace_functions(env, def->name, def->namespace);
    }
    case Block: {
        Text_t code = EMPTY_TEXT;
        for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
            code = Texts(code, compile_functions(env, stmt->ast));
        }
        return code;
    }
    default: return EMPTY_TEXT;
    }
}

Text_t compile_namespace_initializers(env_t *env, const char *name, ast_t *namespace) {
    return compile_initializers(namespace_env(env, name), namespace);
}

Text_t compile_initializers(env_t *env, ast_t *ast) {
    if (!ast) return EMPTY_TEXT;

    Text_t code = EMPTY_TEXT;
    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        switch (stmt->ast->tag) {
        case Declare: {
            if (!needs_runtime_initialization(env, stmt->ast, NULL)) break;
            // Compile the value here and not for a statically initialized
            // variable, whose value compile_variables() emits as a static
            // initializer: compiling it in both places would duplicate any
            // hoisted static defs, e.g. a list literal's backing array.
            const char *decl_name = Match(Match(stmt->ast, Declare)->var, Var)->name;
            Text_t full_name = namespace_name(env, env->namespace, Text$from_str(decl_name));
            Text_t initialized_name = namespace_name(env, env->namespace, Texts(decl_name, "$$initialized"));
            Text_t val_code = compile_declared_value(env, stmt->ast);
            code =
                Texts(code, with_source_info(env, stmt->ast,
                                             Texts(full_name, " = ", val_code, ",\n", initialized_name, " = true;\n")));
            break;
        }
        case StructDef:
            code = Texts(code, compile_namespace_initializers(env, Match(stmt->ast, StructDef)->name,
                                                              Match(stmt->ast, StructDef)->namespace));
            break;
        case EnumDef:
            code = Texts(code, compile_namespace_initializers(env, Match(stmt->ast, EnumDef)->name,
                                                              Match(stmt->ast, EnumDef)->namespace));
            break;
        case LangDef:
            code = Texts(code, compile_namespace_initializers(env, Match(stmt->ast, LangDef)->name,
                                                              Match(stmt->ast, LangDef)->namespace));
            break;
        case FunctionDef:
        case ConvertDef:
        case InlineCCode:
        case Use:
        case Test: break;
        default: {
            Text_t stmt_code = compile_statement(env, stmt->ast);
            if (stmt_code.length > 0) code = Texts(code, with_source_info(env, stmt->ast, stmt_code));
            break;
        }
        }
    }
    return code;
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
        *info->code =
            Texts(*info->code,
                  compile_enum_typeinfo(info->env, String("enum$", (int64_t)(type_ast->start - type_ast->file->text)),
                                        Match(type_ast, EnumTypeAST)->tags));
    }
    return VISIT_PROCEED;
}

static visit_behavior_t add_type_constructors(type_ast_t *type_ast, void *userdata) {
    if (type_ast && type_ast->tag == EnumTypeAST) {
        compile_info_t *info = (compile_info_t *)userdata;
        // Force the type to get defined:
        (void)parse_type_ast(info->env, type_ast);
        *info->code = Texts(
            *info->code,
            compile_enum_constructors(info->env, String("enum$", (int64_t)(type_ast->start - type_ast->file->text)),
                                      Match(type_ast, EnumTypeAST)->tags));
    }
    return VISIT_PROCEED;
}

public
Text_t compile_file(env_t *env, ast_t *ast) {
    Text_t file_variables = EMPTY_TEXT;
    type_ast_visit(ast, add_type_infos, (void *)(compile_info_t[1]){{.env = env, .code = &file_variables}});
    file_variables = Texts(file_variables, compile_variables(env, ast));

    Text_t functions = EMPTY_TEXT;
    type_ast_visit(ast, add_type_constructors, (void *)(compile_info_t[1]){{.env = env, .code = &functions}});
    functions = Texts(functions, compile_functions(env, ast));

    // A `use` contributes to two places: an imported C file becomes an
    // `#include`, and an imported module's initializer has to be called at the
    // top of this file's own initializer, before any of its code runs:
    Text_t includes = EMPTY_TEXT;
    Text_t use_imports = EMPTY_TEXT;
    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        if (stmt->ast->tag == Use) {
            use_imports = Texts(use_imports, compile_statement(env, stmt->ast));

            DeclareMatch(use, stmt->ast, Use);
            if (use->what == USE_C_CODE) {
                Path_t path = Path$from_str(use->path);
                if (path[0] != '/') {
                    // If we have `use ./foo.c`, then we need to remap it in source code to
                    // `#include "../foo.c"`, since it will be inside the .tomo directory.
                    OptionalPath_t parent = Path$parent(Path(ast->file->filename));
                    assert(parent); // A source file always has a directory
                    path = Path$relative_to(Path$resolved(Path$from_str(use->path), parent), parent);
                    path = Path$concat("..", path);
                }
                includes = Texts(includes, "#include \"", Path$as_c_string(path), "\"\n");
            }
        }
    }

    Text_t initializer_body = compile_initializers(env, ast);

    const char *name = file_base_name(ast->file->filename);
    return Texts(env->do_source_mapping ? Texts("#line 1 ", quoted_str(ast->file->filename), "\n") : EMPTY_TEXT,
                 "#define __SOURCE_FILE__ ", quoted_str(ast->file->filename), "\n",
                 "#include <tomo.h>\n"
                 "#include \"",
                 name, ".tm.h\"\n\n", includes, "\n", env->code->local_typedefs, "\n", env->code->constants, "\n",
                 file_variables, "\n", env->code->lambdas, "\n", env->code->staticdefs, "\n", functions, "\n",
                 "public void ", namespace_name(env, env->namespace, Text("$initialize")), "(void) {\n",
                 "static bool initialized = false;\n", "if (initialized) return;\n", "initialized = true;\n",
                 use_imports, initializer_body, "}\n");
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
        descriptors = Texts(descriptors, "{.label=", quoted_str(test->label), ", .fn=", fn_name,
                            ", .expect_failure=", test->expected_failure ? "true" : "false", ", .expected_msg=",
                            (test->expected_failure && test->expected_failure[0]) ? quoted_str(test->expected_failure)
                                                                                  : Text("NULL"),
                            ", .first_line=", get_line_number(ast->file, stmt->ast->start),
                            ", .last_line=", get_line_number(ast->file, stmt->ast->end), "},\n");
        count += 1;
    }
    *out_count = count;
    if (count == 0) return EMPTY_TEXT;

    // Emit the entire module into the runner's own translation unit, including
    // its file-private (`static`) helpers, so the test bodies above can call
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
