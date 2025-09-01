// This file defines how to compile statements

#include <glob.h>

#include "../ast.h"
#include "../config.h"
#include "../environment.h"
#include "../modules.h"
#include "../naming.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/paths.h"
#include "../stdlib/print.h"
#include "../stdlib/tables.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "compilation.h"

typedef ast_t *(*comprehension_body_t)(ast_t *, ast_t *);

public
Text_t with_source_info(env_t *env, ast_t *ast, Text_t code) {
    if (code.length == 0 || !ast || !ast->file || !env->do_source_mapping) return code;
    int64_t line = get_line_number(ast->file, ast->start);
    return Texts("\n#line ", line, "\n", code);
}

static Text_t _compile_statement(env_t *env, ast_t *ast) {
    switch (ast->tag) {
    case When: return compile_when_statement(env, ast);
    case DocTest: return compile_doctest(env, ast);
    case Assert: return compile_assertion(env, ast);
    case Declare: {
        DeclareMatch(decl, ast, Declare);
        const char *name = Match(decl->var, Var)->name;
        if (streq(name, "_")) { // Explicit discard
            if (decl->value) return Texts("(void)", compile(env, decl->value), ";");
            else return EMPTY_TEXT;
        } else {
            type_t *t = decl->type ? parse_type_ast(env, decl->type) : get_type(env, decl->value);
            if (t->tag == FunctionType) t = Type(ClosureType, t);
            if (t->tag == AbortType || t->tag == VoidType || t->tag == ReturnType)
                code_err(ast, "You can't declare a variable with a ", type_to_str(t), " value");

            Text_t val_code = compile_declared_value(env, ast);
            return Texts(compile_declaration(t, Texts("_$", name)), " = ", val_code, ";");
        }
    }
    case Assign: return compile_assignment_statement(env, ast);
    case PlusUpdate: {
        DeclareMatch(update, ast, PlusUpdate);
        type_t *lhs_t = get_type(env, update->lhs);
        if (is_idempotent(update->lhs) && (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType))
            return Texts(compile_lvalue(env, update->lhs), " += ", compile_to_type(env, update->rhs, lhs_t), ";");
        return compile_update_assignment(env, ast);
    }
    case MinusUpdate: {
        DeclareMatch(update, ast, MinusUpdate);
        type_t *lhs_t = get_type(env, update->lhs);
        if (is_idempotent(update->lhs) && (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType))
            return Texts(compile_lvalue(env, update->lhs), " -= ", compile_to_type(env, update->rhs, lhs_t), ";");
        return compile_update_assignment(env, ast);
    }
    case MultiplyUpdate: {
        DeclareMatch(update, ast, MultiplyUpdate);
        type_t *lhs_t = get_type(env, update->lhs);
        if (is_idempotent(update->lhs) && (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType))
            return Texts(compile_lvalue(env, update->lhs), " *= ", compile_to_type(env, update->rhs, lhs_t), ";");
        return compile_update_assignment(env, ast);
    }
    case DivideUpdate: {
        DeclareMatch(update, ast, DivideUpdate);
        type_t *lhs_t = get_type(env, update->lhs);
        if (is_idempotent(update->lhs) && (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType))
            return Texts(compile_lvalue(env, update->lhs), " /= ", compile_to_type(env, update->rhs, lhs_t), ";");
        return compile_update_assignment(env, ast);
    }
    case ModUpdate: {
        DeclareMatch(update, ast, ModUpdate);
        type_t *lhs_t = get_type(env, update->lhs);
        if (is_idempotent(update->lhs) && (lhs_t->tag == IntType || lhs_t->tag == NumType || lhs_t->tag == ByteType))
            return Texts(compile_lvalue(env, update->lhs), " %= ", compile_to_type(env, update->rhs, lhs_t), ";");
        return compile_update_assignment(env, ast);
    }
    case PowerUpdate:
    case Mod1Update:
    case ConcatUpdate:
    case LeftShiftUpdate:
    case UnsignedLeftShiftUpdate:
    case RightShiftUpdate:
    case UnsignedRightShiftUpdate:
    case AndUpdate:
    case OrUpdate:
    case XorUpdate: {
        return compile_update_assignment(env, ast);
    }
    case StructDef:
    case EnumDef:
    case LangDef:
    case Extend:
    case FunctionDef:
    case ConvertDef: {
        return EMPTY_TEXT;
    }
    case Skip: return compile_skip(env, ast);
    case Stop: return compile_stop(env, ast);
    case Pass: return Text(";");
    case Defer: {
        ast_t *body = Match(ast, Defer)->body;
        Table_t closed_vars = get_closed_vars(env, NULL, body);

        static int defer_id = 0;
        env_t *defer_env = fresh_scope(env);
        Text_t code = EMPTY_TEXT;
        for (int64_t i = 0; i < closed_vars.entries.length; i++) {
            struct {
                const char *name;
                binding_t *b;
            } *entry = closed_vars.entries.data + closed_vars.entries.stride * i;
            if (entry->b->type->tag == ModuleType) continue;
            if (Text$starts_with(entry->b->code, Text("userdata->"), NULL)) {
                Table$str_set(defer_env->locals, entry->name, entry->b);
            } else {
                Text_t defer_name = Texts("defer$", ++defer_id, "$", entry->name);
                defer_id += 1;
                code = Texts(code, compile_declaration(entry->b->type, defer_name), " = ", entry->b->code, ";\n");
                set_binding(defer_env, entry->name, entry->b->type, defer_name);
            }
        }
        env->deferred = new (deferral_t, .defer_env = defer_env, .block = body, .next = env->deferred);
        return code;
    }
    case Return: {
        if (!env->fn_ret) code_err(ast, "This return statement is not inside any function");
        ast_t *ret = Match(ast, Return)->value;

        Text_t code = EMPTY_TEXT;
        for (deferral_t *deferred = env->deferred; deferred; deferred = deferred->next) {
            code = Texts(code, compile_statement(deferred->defer_env, deferred->block));
        }

        if (ret) {
            if (env->fn_ret->tag == VoidType || env->fn_ret->tag == AbortType)
                code_err(ast, "This function is not supposed to return any values, "
                              "according to its type signature");

            env = with_enum_scope(env, env->fn_ret);
            Text_t value = compile_to_type(env, ret, env->fn_ret);
            if (env->deferred) {
                code = Texts(compile_declaration(env->fn_ret, Text("ret")), " = ", value, ";\n", code);
                value = Text("ret");
            }

            return Texts(code, "return ", value, ";");
        } else {
            if (env->fn_ret->tag != VoidType)
                code_err(ast, "This function expects you to return a ", type_to_str(env->fn_ret), " value");
            return Texts(code, "return;");
        }
    }
    case While: return compile_while(env, ast);
    case Repeat: return compile_repeat(env, ast);
    case For: return compile_for_loop(env, ast);
    case If: return compile_if_statement(env, ast);
    case Block: return compile_block(env, ast);
    case Comprehension: {
        if (!env->comprehension_action) code_err(ast, "I don't know what to do with this comprehension!");
        DeclareMatch(comp, ast, Comprehension);
        if (comp->expr->tag == Comprehension) { // Nested comprehension
            ast_t *body = comp->filter ? WrapAST(ast, If, .condition = comp->filter, .body = comp->expr) : comp->expr;
            ast_t *loop = WrapAST(ast, For, .vars = comp->vars, .iter = comp->iter, .body = body);
            return compile_statement(env, loop);
        }

        // List/Set/Table comprehension:
        comprehension_body_t get_body = (void *)env->comprehension_action->fn;
        ast_t *body = get_body(comp->expr, env->comprehension_action->userdata);
        if (comp->filter) body = WrapAST(comp->expr, If, .condition = comp->filter, .body = body);
        ast_t *loop = WrapAST(ast, For, .vars = comp->vars, .iter = comp->iter, .body = body);
        return compile_statement(env, loop);
    }
    case Extern: return EMPTY_TEXT;
    case InlineCCode: {
        DeclareMatch(inline_code, ast, InlineCCode);
        Text_t code = EMPTY_TEXT;
        for (ast_list_t *chunk = inline_code->chunks; chunk; chunk = chunk->next) {
            if (chunk->ast->tag == TextLiteral) {
                code = Texts(code, Match(chunk->ast, TextLiteral)->text);
            } else {
                code = Texts(code, compile(env, chunk->ast));
            }
        }
        return code;
    }
    case Use: {
        DeclareMatch(use, ast, Use);
        if (use->what == USE_LOCAL) {
            Path_t path = Path$from_str(Match(ast, Use)->path);
            Path_t in_file = Path$from_str(ast->file->filename);
            path = Path$resolved(path, Path$parent(in_file));
            Text_t suffix = get_id_suffix(Path$as_c_string(path));
            return with_source_info(env, ast, Texts("$initialize", suffix, "();\n"));
        } else if (use->what == USE_MODULE) {
            module_info_t mod = get_module_info(ast);
            glob_t tm_files;
            const char *folder = mod.version ? String(mod.name, "_", mod.version) : mod.name;
            if (glob(String(TOMO_PREFIX "/lib/tomo_" TOMO_VERSION "/", folder, "/[!._0-9]*.tm"), GLOB_TILDE, NULL,
                     &tm_files)
                != 0) {
                if (!try_install_module(mod, true)) code_err(ast, "Could not find library");
            }

            Text_t initialization = EMPTY_TEXT;

            for (size_t i = 0; i < tm_files.gl_pathc; i++) {
                const char *filename = tm_files.gl_pathv[i];
                initialization = Texts(
                    initialization, with_source_info(env, ast, Texts("$initialize", get_id_suffix(filename), "();\n")));
            }
            globfree(&tm_files);
            return initialization;
        } else {
            return EMPTY_TEXT;
        }
    }
    default:
        // print("Is discardable: ", ast_to_sexp_str(ast), " ==> ",
        // is_discardable(env, ast));
        if (!is_discardable(env, ast))
            code_err(ast, "The ", type_to_str(get_type(env, ast)), " result of this statement cannot be discarded");
        return Texts("(void)", compile(env, ast), ";");
    }
}

Text_t compile_statement(env_t *env, ast_t *ast) {
    Text_t stmt = _compile_statement(env, ast);
    return with_source_info(env, ast, stmt);
}
