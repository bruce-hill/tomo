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
#include "assignments.h"
#include "blocks.h"
#include "conditionals.h"
#include "declarations.h"
#include "expressions.h"
#include "forloops.h"
#include "functions.h"
#include "promotions.h"
#include "statements.h"
#include "text.h"
#include "types.h"
#include "whens.h"

typedef ast_t *(*comprehension_body_t)(ast_t *, ast_t *);

public
Text_t with_source_info(env_t *env, ast_t *ast, Text_t code) {
    if (code.length == 0 || !ast || !ast->file || !env->do_source_mapping) return code;
    int64_t line = get_line_number(ast->file, ast->start);
    return Texts("\n#line ", String(line), "\n", code);
}

static Text_t _compile_statement(env_t *env, ast_t *ast) {
    switch (ast->tag) {
    case When: return compile_when_statement(env, ast);
    case DocTest: {
        DeclareMatch(test, ast, DocTest);
        type_t *expr_t = get_type(env, test->expr);
        if (!expr_t) code_err(test->expr, "I couldn't figure out the type of this expression");

        Text_t setup = EMPTY_TEXT;
        Text_t test_code;
        if (test->expr->tag == Declare) {
            DeclareMatch(decl, test->expr, Declare);
            type_t *t = decl->type ? parse_type_ast(env, decl->type) : get_type(env, decl->value);
            if (t->tag == FunctionType) t = Type(ClosureType, t);
            Text_t var = Texts("_$", Match(decl->var, Var)->name);
            Text_t val_code = compile_declared_value(env, test->expr);
            setup = Texts(compile_declaration(t, var), ";\n");
            test_code = Texts("(", var, " = ", val_code, ")");
            expr_t = t;
        } else if (test->expr->tag == Assign) {
            DeclareMatch(assign, test->expr, Assign);
            if (!assign->targets->next && assign->targets->ast->tag == Var && is_idempotent(assign->targets->ast)) {
                // Common case: assigning to one variable:
                type_t *lhs_t = get_type(env, assign->targets->ast);
                if (assign->targets->ast->tag == Index && lhs_t->tag == OptionalType
                    && value_type(get_type(env, Match(assign->targets->ast, Index)->indexed))->tag == TableType)
                    lhs_t = Match(lhs_t, OptionalType)->type;
                if (has_stack_memory(lhs_t))
                    code_err(test->expr, "Stack references cannot be assigned "
                                         "to variables because the "
                                         "variable's scope may outlive the "
                                         "scope of the stack memory.");
                env_t *val_scope = with_enum_scope(env, lhs_t);
                Text_t value = compile_to_type(val_scope, assign->values->ast, lhs_t);
                test_code = Texts("(", compile_assignment(env, assign->targets->ast, value), ")");
                expr_t = lhs_t;
            } else {
                // Multi-assign or assignment to potentially non-idempotent
                // targets
                if (test->expected && assign->targets->next)
                    code_err(ast, "Sorry, but doctesting with '=' is not "
                                  "supported for "
                                  "multi-assignments");

                test_code = Text("({ // Assignment\n");

                int64_t i = 1;
                for (ast_list_t *target = assign->targets, *value = assign->values; target && value;
                     target = target->next, value = value->next) {
                    type_t *lhs_t = get_type(env, target->ast);
                    if (target->ast->tag == Index && lhs_t->tag == OptionalType
                        && value_type(get_type(env, Match(target->ast, Index)->indexed))->tag == TableType)
                        lhs_t = Match(lhs_t, OptionalType)->type;
                    if (has_stack_memory(lhs_t))
                        code_err(ast, "Stack references cannot be assigned to "
                                      "variables because the "
                                      "variable's scope may outlive the scope "
                                      "of the stack memory.");
                    if (target == assign->targets) expr_t = lhs_t;
                    env_t *val_scope = with_enum_scope(env, lhs_t);
                    Text_t val_code = compile_to_type(val_scope, value->ast, lhs_t);
                    test_code = Texts(test_code, compile_type(lhs_t), " $", String(i), " = ", val_code, ";\n");
                    i += 1;
                }
                i = 1;
                for (ast_list_t *target = assign->targets; target; target = target->next) {
                    test_code = Texts(test_code, compile_assignment(env, target->ast, Texts("$", String(i))), ";\n");
                    i += 1;
                }

                test_code = Texts(test_code, "$1; })");
            }
        } else if (is_update_assignment(test->expr)) {
            binary_operands_t update = UPDATE_OPERANDS(test->expr);
            type_t *lhs_t = get_type(env, update.lhs);
            if (update.lhs->tag == Index) {
                type_t *indexed = value_type(get_type(env, Match(update.lhs, Index)->indexed));
                if (indexed->tag == TableType && Match(indexed, TableType)->default_value == NULL)
                    code_err(update.lhs, "Update assignments are not currently "
                                         "supported for tables");
            }

            ast_t *update_var = new (ast_t);
            *update_var = *test->expr;
            update_var->__data.PlusUpdate.lhs = LiteralCode(Text("(*expr)"), .type = lhs_t); // UNSAFE
            test_code =
                Texts("({", compile_declaration(Type(PointerType, lhs_t), Text("expr")), " = &(",
                      compile_lvalue(env, update.lhs), "); ", compile_statement(env, update_var), "; *expr; })");
            expr_t = lhs_t;
        } else if (expr_t->tag == VoidType || expr_t->tag == AbortType || expr_t->tag == ReturnType) {
            test_code = Texts("({", compile_statement(env, test->expr), " NULL;})");
        } else {
            test_code = compile(env, test->expr);
        }
        if (test->expected) {
            return Texts(setup, "test(", compile_type(expr_t), ", ", test_code, ", ",
                         compile_to_type(env, test->expected, expr_t), ", ", compile_type_info(expr_t), ", ",
                         String((int64_t)(test->expr->start - test->expr->file->text)), ", ",
                         String((int64_t)(test->expr->end - test->expr->file->text)), ");");
        } else {
            if (expr_t->tag == VoidType || expr_t->tag == AbortType) {
                return Texts(setup, "inspect_void(", test_code, ", ", compile_type_info(expr_t), ", ",
                             String((int64_t)(test->expr->start - test->expr->file->text)), ", ",
                             String((int64_t)(test->expr->end - test->expr->file->text)), ");");
            }
            return Texts(setup, "inspect(", compile_type(expr_t), ", ", test_code, ", ", compile_type_info(expr_t),
                         ", ", String((int64_t)(test->expr->start - test->expr->file->text)), ", ",
                         String((int64_t)(test->expr->end - test->expr->file->text)), ");");
        }
    }
    case Assert: {
        ast_t *expr = Match(ast, Assert)->expr;
        ast_t *message = Match(ast, Assert)->message;
        const char *failure = NULL;
        switch (expr->tag) {
        case And: {
            DeclareMatch(and_, ast, And);
            return Texts(compile_statement(env, WrapAST(ast, Assert, .expr = and_->lhs, .message = message)),
                         compile_statement(env, WrapAST(ast, Assert, .expr = and_->rhs, .message = message)));
        }
        case Equals: failure = "!="; goto assert_comparison;
        case NotEquals: failure = "=="; goto assert_comparison;
        case LessThan: failure = ">="; goto assert_comparison;
        case LessThanOrEquals: failure = ">"; goto assert_comparison;
        case GreaterThan: failure = "<="; goto assert_comparison;
        case GreaterThanOrEquals:
            failure = "<";
            goto assert_comparison;
            {
            assert_comparison:;
                binary_operands_t cmp = BINARY_OPERANDS(expr);
                type_t *lhs_t = get_type(env, cmp.lhs);
                type_t *rhs_t = get_type(env, cmp.rhs);
                type_t *operand_t;
                if (cmp.lhs->tag == Int && is_numeric_type(rhs_t)) {
                    operand_t = rhs_t;
                } else if (cmp.rhs->tag == Int && is_numeric_type(lhs_t)) {
                    operand_t = lhs_t;
                } else if (can_compile_to_type(env, cmp.rhs, lhs_t)) {
                    operand_t = lhs_t;
                } else if (can_compile_to_type(env, cmp.lhs, rhs_t)) {
                    operand_t = rhs_t;
                } else {
                    code_err(ast, "I can't do comparisons between ", type_to_str(lhs_t), " and ", type_to_str(rhs_t));
                }

                ast_t *lhs_var =
                    FakeAST(InlineCCode, .chunks = new (ast_list_t, .ast = FakeAST(TextLiteral, Text("_lhs"))),
                            .type = operand_t);
                ast_t *rhs_var =
                    FakeAST(InlineCCode, .chunks = new (ast_list_t, .ast = FakeAST(TextLiteral, Text("_rhs"))),
                            .type = operand_t);
                ast_t *var_comparison = new (ast_t, .file = expr->file, .start = expr->start, .end = expr->end,
                                             .tag = expr->tag, .__data.Equals = {.lhs = lhs_var, .rhs = rhs_var});
                int64_t line = get_line_number(ast->file, ast->start);
                return Texts("{ // assertion\n", compile_declaration(operand_t, Text("_lhs")), " = ",
                             compile_to_type(env, cmp.lhs, operand_t), ";\n", "\n#line ", String(line), "\n",
                             compile_declaration(operand_t, Text("_rhs")), " = ",
                             compile_to_type(env, cmp.rhs, operand_t), ";\n", "\n#line ", String(line), "\n", "if (!(",
                             compile_condition(env, var_comparison), "))\n", "#line ", String(line), "\n",
                             Texts("fail_source(", quoted_str(ast->file->filename), ", ",
                                   String((int64_t)(expr->start - expr->file->text)), ", ",
                                   String((int64_t)(expr->end - expr->file->text)), ", ",
                                   message
                                       ? Texts("Text$as_c_string(", compile_to_type(env, message, Type(TextType)), ")")
                                       : Text("\"This assertion failed!\""),
                                   ", ", "\" (\", ", expr_as_text(Text("_lhs"), operand_t, Text("no")),
                                   ", "
                                   "\" ",
                                   failure, " \", ", expr_as_text(Text("_rhs"), operand_t, Text("no")), ", \")\");\n"),
                             "}\n");
            }
        default: {
            int64_t line = get_line_number(ast->file, ast->start);
            return Texts("if (!(", compile_condition(env, expr), "))\n", "#line ", String(line), "\n", "fail_source(",
                         quoted_str(ast->file->filename), ", ", String((int64_t)(expr->start - expr->file->text)), ", ",
                         String((int64_t)(expr->end - expr->file->text)), ", ",
                         message ? Texts("Text$as_c_string(", compile_to_type(env, message, Type(TextType)), ")")
                                 : Text("\"This assertion failed!\""),
                         ");\n");
        }
        }
    }
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
    case Skip: {
        const char *target = Match(ast, Skip)->target;
        for (loop_ctx_t *ctx = env->loop_ctx; ctx; ctx = ctx->next) {
            bool matched = !target || strcmp(target, ctx->loop_name) == 0;
            for (ast_list_t *var = ctx->loop_vars; var && !matched; var = var ? var->next : NULL)
                matched = (strcmp(target, Match(var->ast, Var)->name) == 0);

            if (matched) {
                if (ctx->skip_label.length == 0) {
                    static int64_t skip_label_count = 1;
                    ctx->skip_label = Texts("skip_", String(skip_label_count));
                    ++skip_label_count;
                }
                Text_t code = EMPTY_TEXT;
                for (deferral_t *deferred = env->deferred; deferred && deferred != ctx->deferred;
                     deferred = deferred->next)
                    code = Texts(code, compile_statement(deferred->defer_env, deferred->block));
                if (code.length > 0) return Texts("{\n", code, "goto ", ctx->skip_label, ";\n}\n");
                else return Texts("goto ", ctx->skip_label, ";");
            }
        }
        if (env->loop_ctx) code_err(ast, "This is not inside any loop");
        else if (target) code_err(ast, "No loop target named '", target, "' was found");
        else return Text("continue;");
    }
    case Stop: {
        const char *target = Match(ast, Stop)->target;
        for (loop_ctx_t *ctx = env->loop_ctx; ctx; ctx = ctx->next) {
            bool matched = !target || strcmp(target, ctx->loop_name) == 0;
            for (ast_list_t *var = ctx->loop_vars; var && !matched; var = var ? var->next : var)
                matched = (strcmp(target, Match(var->ast, Var)->name) == 0);

            if (matched) {
                if (ctx->stop_label.length == 0) {
                    static int64_t stop_label_count = 1;
                    ctx->stop_label = Texts("stop_", String(stop_label_count));
                    ++stop_label_count;
                }
                Text_t code = EMPTY_TEXT;
                for (deferral_t *deferred = env->deferred; deferred && deferred != ctx->deferred;
                     deferred = deferred->next)
                    code = Texts(code, compile_statement(deferred->defer_env, deferred->block));
                if (code.length > 0) return Texts("{\n", code, "goto ", ctx->stop_label, ";\n}\n");
                else return Texts("goto ", ctx->stop_label, ";");
            }
        }
        if (env->loop_ctx) code_err(ast, "This is not inside any loop");
        else if (target) code_err(ast, "No loop target named '", target, "' was found");
        else return Text("break;");
    }
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
                Text_t defer_name = Texts("defer$", String(++defer_id), "$", entry->name);
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
    case While: {
        DeclareMatch(while_, ast, While);
        env_t *scope = fresh_scope(env);
        loop_ctx_t loop_ctx = (loop_ctx_t){
            .loop_name = "while",
            .deferred = scope->deferred,
            .next = env->loop_ctx,
        };
        scope->loop_ctx = &loop_ctx;
        Text_t body = compile_statement(scope, while_->body);
        if (loop_ctx.skip_label.length > 0) body = Texts(body, "\n", loop_ctx.skip_label, ": continue;");
        Text_t loop = Texts("while (", while_->condition ? compile(scope, while_->condition) : Text("yes"), ") {\n\t",
                            body, "\n}");
        if (loop_ctx.stop_label.length > 0) loop = Texts(loop, "\n", loop_ctx.stop_label, ":;");
        return loop;
    }
    case Repeat: {
        ast_t *body = Match(ast, Repeat)->body;
        env_t *scope = fresh_scope(env);
        loop_ctx_t loop_ctx = (loop_ctx_t){
            .loop_name = "repeat",
            .deferred = scope->deferred,
            .next = env->loop_ctx,
        };
        scope->loop_ctx = &loop_ctx;
        Text_t body_code = compile_statement(scope, body);
        if (loop_ctx.skip_label.length > 0) body_code = Texts(body_code, "\n", loop_ctx.skip_label, ": continue;");
        Text_t loop = Texts("for (;;) {\n\t", body_code, "\n}");
        if (loop_ctx.stop_label.length > 0) loop = Texts(loop, "\n", loop_ctx.stop_label, ":;");
        return loop;
    }
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
            if (glob(String(TOMO_PREFIX "/share/tomo_" TOMO_VERSION "/installed/", folder, "/[!._0-9]*.tm"), GLOB_TILDE,
                     NULL, &tm_files)
                != 0) {
                if (!try_install_module(mod)) code_err(ast, "Could not find library");
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
