// This file defines how to compile statements

#include <glob.h>

#include "../ast.h"
#include "../compile.h"
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
#include "functions.h"
#include "optionals.h"
#include "pointers.h"
#include "promotions.h"
#include "statements.h"
#include "text.h"
#include "types.h"

typedef ast_t *(*comprehension_body_t)(ast_t *, ast_t *);

public
Text_t with_source_info(env_t *env, ast_t *ast, Text_t code) {
    if (code.length == 0 || !ast || !ast->file || !env->do_source_mapping) return code;
    int64_t line = get_line_number(ast->file, ast->start);
    return Texts("\n#line ", String(line), "\n", code);
}

public
Text_t compile_condition(env_t *env, ast_t *ast) {
    type_t *t = get_type(env, ast);
    if (t->tag == BoolType) {
        return compile(env, ast);
    } else if (t->tag == TextType) {
        return Texts("(", compile(env, ast), ").length");
    } else if (t->tag == ListType) {
        return Texts("(", compile(env, ast), ").length");
    } else if (t->tag == TableType || t->tag == SetType) {
        return Texts("(", compile(env, ast), ").entries.length");
    } else if (t->tag == OptionalType) {
        return Texts("!", check_none(t, compile(env, ast)));
    } else if (t->tag == PointerType) {
        code_err(ast, "This pointer will always be non-none, so it should not be "
                      "used in a conditional.");
    } else {
        code_err(ast, type_to_str(t), " values cannot be used for conditionals");
    }
    return EMPTY_TEXT;
}

static Text_t _compile_statement(env_t *env, ast_t *ast) {
    switch (ast->tag) {
    case When: {
        // Typecheck to verify exhaustiveness:
        type_t *result_t = get_type(env, ast);
        (void)result_t;

        DeclareMatch(when, ast, When);
        type_t *subject_t = get_type(env, when->subject);

        if (subject_t->tag != EnumType) {
            Text_t prefix = EMPTY_TEXT, suffix = EMPTY_TEXT;
            ast_t *subject = when->subject;
            if (!is_idempotent(when->subject)) {
                prefix = Texts("{\n", compile_declaration(subject_t, Text("_when_subject")), " = ",
                               compile(env, subject), ";\n");
                suffix = Text("}\n");
                subject = LiteralCode(Text("_when_subject"), .type = subject_t);
            }

            Text_t code = EMPTY_TEXT;
            for (when_clause_t *clause = when->clauses; clause; clause = clause->next) {
                ast_t *comparison = WrapAST(clause->pattern, Equals, .lhs = subject, .rhs = clause->pattern);
                (void)get_type(env, comparison);
                if (code.length > 0) code = Texts(code, "else ");
                code = Texts(code, "if (", compile(env, comparison), ")", compile_statement(env, clause->body));
            }
            if (when->else_body) code = Texts(code, "else ", compile_statement(env, when->else_body));
            code = Texts(prefix, code, suffix);
            return code;
        }

        DeclareMatch(enum_t, subject_t, EnumType);

        Text_t code;
        if (enum_has_fields(subject_t))
            code = Texts("WHEN(", compile_type(subject_t), ", ", compile(env, when->subject), ", _when_subject, {\n");
        else code = Texts("switch(", compile(env, when->subject), ") {\n");

        for (when_clause_t *clause = when->clauses; clause; clause = clause->next) {
            if (clause->pattern->tag == Var) {
                const char *clause_tag_name = Match(clause->pattern, Var)->name;
                type_t *clause_type = clause->body ? get_type(env, clause->body) : Type(VoidType);
                code = Texts(
                    code, "case ", namespace_name(enum_t->env, enum_t->env->namespace, Texts("tag$", clause_tag_name)),
                    ": {\n", compile_inline_block(env, clause->body),
                    (clause_type->tag == ReturnType || clause_type->tag == AbortType) ? EMPTY_TEXT : Text("break;\n"),
                    "}\n");
                continue;
            }

            if (clause->pattern->tag != FunctionCall || Match(clause->pattern, FunctionCall)->fn->tag != Var)
                code_err(clause->pattern, "This is not a valid pattern for a ", type_to_str(subject_t), " enum type");

            const char *clause_tag_name = Match(Match(clause->pattern, FunctionCall)->fn, Var)->name;
            code = Texts(code, "case ",
                         namespace_name(enum_t->env, enum_t->env->namespace, Texts("tag$", clause_tag_name)), ": {\n");
            type_t *tag_type = NULL;
            for (tag_t *tag = enum_t->tags; tag; tag = tag->next) {
                if (streq(tag->name, clause_tag_name)) {
                    tag_type = tag->type;
                    break;
                }
            }
            assert(tag_type);
            env_t *scope = env;

            DeclareMatch(tag_struct, tag_type, StructType);
            arg_ast_t *args = Match(clause->pattern, FunctionCall)->args;
            if (args && !args->next && tag_struct->fields && tag_struct->fields->next) {
                if (args->value->tag != Var) code_err(args->value, "This is not a valid variable to bind to");
                const char *var_name = Match(args->value, Var)->name;
                if (!streq(var_name, "_")) {
                    Text_t var = Texts("_$", var_name);
                    code = Texts(code, compile_declaration(tag_type, var), " = _when_subject.",
                                 valid_c_name(clause_tag_name), ";\n");
                    scope = fresh_scope(scope);
                    set_binding(scope, Match(args->value, Var)->name, tag_type, EMPTY_TEXT);
                }
            } else if (args) {
                scope = fresh_scope(scope);
                arg_t *field = tag_struct->fields;
                for (arg_ast_t *arg = args; arg || field; arg = arg->next) {
                    if (!arg)
                        code_err(ast, "The field ", type_to_str(subject_t), ".", clause_tag_name, ".", field->name,
                                 " wasn't accounted for");
                    if (!field) code_err(arg->value, "This is one more field than ", type_to_str(subject_t), " has");
                    if (arg->name) code_err(arg->value, "Named arguments are not currently supported");

                    const char *var_name = Match(arg->value, Var)->name;
                    if (!streq(var_name, "_")) {
                        Text_t var = Texts("_$", var_name);
                        code = Texts(code, compile_declaration(field->type, var), " = _when_subject.",
                                     valid_c_name(clause_tag_name), ".", valid_c_name(field->name), ";\n");
                        set_binding(scope, Match(arg->value, Var)->name, field->type, var);
                    }
                    field = field->next;
                }
            }
            if (clause->body->tag == Block) {
                ast_list_t *statements = Match(clause->body, Block)->statements;
                if (!statements || (statements->ast->tag == Pass && !statements->next))
                    code = Texts(code, "break;\n}\n");
                else code = Texts(code, compile_inline_block(scope, clause->body), "\nbreak;\n}\n");
            } else {
                code = Texts(code, compile_statement(scope, clause->body), "\nbreak;\n}\n");
            }
        }
        if (when->else_body) {
            if (when->else_body->tag == Block) {
                ast_list_t *statements = Match(when->else_body, Block)->statements;
                if (!statements || (statements->ast->tag == Pass && !statements->next))
                    code = Texts(code, "default: break;");
                else code = Texts(code, "default: {\n", compile_inline_block(env, when->else_body), "\nbreak;\n}\n");
            } else {
                code = Texts(code, "default: {\n", compile_statement(env, when->else_body), "\nbreak;\n}\n");
            }
        } else {
            code = Texts(code, "default: errx(1, \"Invalid tag!\");\n");
        }
        code = Texts(code, "\n}", enum_has_fields(subject_t) ? Text(")") : EMPTY_TEXT, "\n");
        return code;
    }
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
    case Assign: {
        DeclareMatch(assign, ast, Assign);
        // Single assignment, no temp vars needed:
        if (assign->targets && !assign->targets->next) {
            type_t *lhs_t = get_type(env, assign->targets->ast);
            if (assign->targets->ast->tag == Index && lhs_t->tag == OptionalType
                && value_type(get_type(env, Match(assign->targets->ast, Index)->indexed))->tag == TableType)
                lhs_t = Match(lhs_t, OptionalType)->type;
            if (has_stack_memory(lhs_t))
                code_err(ast, "Stack references cannot be assigned to "
                              "variables because the "
                              "variable's scope may outlive the scope of the "
                              "stack memory.");
            env_t *val_env = with_enum_scope(env, lhs_t);
            Text_t val = compile_to_type(val_env, assign->values->ast, lhs_t);
            return Texts(compile_assignment(env, assign->targets->ast, val), ";\n");
        }

        Text_t code = Text("{ // Assignment\n");
        int64_t i = 1;
        for (ast_list_t *value = assign->values, *target = assign->targets; value && target;
             value = value->next, target = target->next) {
            type_t *lhs_t = get_type(env, target->ast);
            if (target->ast->tag == Index && lhs_t->tag == OptionalType
                && value_type(get_type(env, Match(target->ast, Index)->indexed))->tag == TableType)
                lhs_t = Match(lhs_t, OptionalType)->type;
            if (has_stack_memory(lhs_t))
                code_err(ast, "Stack references cannot be assigned to "
                              "variables because the "
                              "variable's scope may outlive the scope of the "
                              "stack memory.");
            env_t *val_env = with_enum_scope(env, lhs_t);
            Text_t val = compile_to_type(val_env, value->ast, lhs_t);
            code = Texts(code, compile_type(lhs_t), " $", String(i), " = ", val, ";\n");
            i += 1;
        }
        i = 1;
        for (ast_list_t *target = assign->targets; target; target = target->next) {
            code = Texts(code, compile_assignment(env, target->ast, Texts("$", String(i))), ";\n");
            i += 1;
        }
        return Texts(code, "\n}");
    }
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
    case For: {
        DeclareMatch(for_, ast, For);

        // If we're iterating over a comprehension, that's actually just doing
        // one loop, we don't need to compile the comprehension as a list
        // comprehension. This is a common case for reducers like `(+: i*2 for i
        // in 5)` or `(and) x.is_good() for x in xs`
        if (for_->iter->tag == Comprehension) {
            DeclareMatch(comp, for_->iter, Comprehension);
            ast_t *body = for_->body;
            if (for_->vars) {
                if (for_->vars->next) code_err(for_->vars->next->ast, "This is too many variables for iteration");

                body = WrapAST(
                    ast, Block,
                    .statements = new (
                        ast_list_t, .ast = WrapAST(ast, Declare, .var = for_->vars->ast, .value = comp->expr),
                        .next = body->tag == Block ? Match(body, Block)->statements : new (ast_list_t, .ast = body)));
            }

            if (comp->filter) body = WrapAST(for_->body, If, .condition = comp->filter, .body = body);
            ast_t *loop = WrapAST(ast, For, .vars = comp->vars, .iter = comp->iter, .body = body);
            return compile_statement(env, loop);
        }

        env_t *body_scope = for_scope(env, ast);
        loop_ctx_t loop_ctx = (loop_ctx_t){
            .loop_name = "for",
            .loop_vars = for_->vars,
            .deferred = body_scope->deferred,
            .next = body_scope->loop_ctx,
        };
        body_scope->loop_ctx = &loop_ctx;
        // Naked means no enclosing braces:
        Text_t naked_body = compile_inline_block(body_scope, for_->body);
        if (loop_ctx.skip_label.length > 0) naked_body = Texts(naked_body, "\n", loop_ctx.skip_label, ": continue;");
        Text_t stop = loop_ctx.stop_label.length > 0 ? Texts("\n", loop_ctx.stop_label, ":;") : EMPTY_TEXT;

        // Special case for improving performance for numeric iteration:
        if (for_->iter->tag == MethodCall && streq(Match(for_->iter, MethodCall)->name, "to")
            && is_int_type(get_type(env, Match(for_->iter, MethodCall)->self))) {
            // TODO: support other integer types
            arg_ast_t *args = Match(for_->iter, MethodCall)->args;
            if (!args) code_err(for_->iter, "to() needs at least one argument");

            type_t *int_type = get_type(env, Match(for_->iter, MethodCall)->self);
            type_t *step_type = int_type->tag == ByteType ? Type(IntType, .bits = TYPE_IBITS8) : int_type;

            Text_t last = EMPTY_TEXT, step = EMPTY_TEXT, optional_step = EMPTY_TEXT;
            if (!args->name || streq(args->name, "last")) {
                last = compile_to_type(env, args->value, int_type);
                if (args->next) {
                    if (args->next->name && !streq(args->next->name, "step"))
                        code_err(args->next->value, "Invalid argument name: ", args->next->name);
                    if (get_type(env, args->next->value)->tag == OptionalType)
                        optional_step = compile_to_type(env, args->next->value, Type(OptionalType, step_type));
                    else step = compile_to_type(env, args->next->value, step_type);
                }
            } else if (streq(args->name, "step")) {
                if (get_type(env, args->value)->tag == OptionalType)
                    optional_step = compile_to_type(env, args->value, Type(OptionalType, step_type));
                else step = compile_to_type(env, args->value, step_type);
                if (args->next) {
                    if (args->next->name && !streq(args->next->name, "last"))
                        code_err(args->next->value, "Invalid argument name: ", args->next->name);
                    last = compile_to_type(env, args->next->value, int_type);
                }
            }

            if (last.length == 0) code_err(for_->iter, "No `last` argument was given");

            Text_t type_code = compile_type(int_type);
            Text_t value = for_->vars ? compile(body_scope, for_->vars->ast) : Text("i");
            if (int_type->tag == BigIntType) {
                if (optional_step.length > 0)
                    step = Texts("({ OptionalInt_t maybe_step = ", optional_step,
                                 "; maybe_step->small == 0 ? "
                                 "(Int$compare_value(last, first) >= 0 "
                                 "? I_small(1) : I_small(-1)) : (Int_t)maybe_step; "
                                 "})");
                else if (step.length == 0)
                    step = Text("Int$compare_value(last, first) >= 0 ? "
                                "I_small(1) : I_small(-1)");
                return Texts("for (", type_code, " first = ", compile(env, Match(for_->iter, MethodCall)->self), ", ",
                             value, " = first, last = ", last, ", step = ", step,
                             "; "
                             "Int$compare_value(",
                             value, ", last) != Int$compare_value(step, I_small(0)); ", value, " = Int$plus(", value,
                             ", step)) {\n"
                             "\t",
                             naked_body, "}", stop);
            } else {
                if (optional_step.length > 0)
                    step = Texts("({ ", compile_type(Type(OptionalType, step_type)), " maybe_step = ", optional_step,
                                 "; "
                                 "maybe_step.is_none ? (",
                                 type_code, ")(last >= first ? 1 : -1) : maybe_step.value; })");
                else if (step.length == 0) step = Texts("(", type_code, ")(last >= first ? 1 : -1)");
                return Texts("for (", type_code, " first = ", compile(env, Match(for_->iter, MethodCall)->self), ", ",
                             value, " = first, last = ", last, ", step = ", step,
                             "; "
                             "step > 0 ? ",
                             value, " <= last : ", value, " >= last; ", value,
                             " += step) {\n"
                             "\t",
                             naked_body, "}", stop);
            }
        } else if (for_->iter->tag == MethodCall && streq(Match(for_->iter, MethodCall)->name, "onward")
                   && get_type(env, Match(for_->iter, MethodCall)->self)->tag == BigIntType) {
            // Special case for Int.onward()
            arg_ast_t *args = Match(for_->iter, MethodCall)->args;
            arg_t *arg_spec =
                new (arg_t, .name = "step", .type = INT_TYPE, .default_val = FakeAST(Int, .str = "1"), .next = NULL);
            Text_t step = compile_arguments(env, for_->iter, arg_spec, args);
            Text_t value = for_->vars ? compile(body_scope, for_->vars->ast) : Text("i");
            return Texts("for (Int_t ", value, " = ", compile(env, Match(for_->iter, MethodCall)->self), ", ",
                         "step = ", step, "; ; ", value, " = Int$plus(", value,
                         ", step)) {\n"
                         "\t",
                         naked_body, "}", stop);
        }

        type_t *iter_t = get_type(env, for_->iter);
        type_t *iter_value_t = value_type(iter_t);

        switch (iter_value_t->tag) {
        case ListType: {
            type_t *item_t = Match(iter_value_t, ListType)->item_type;
            Text_t index = EMPTY_TEXT;
            Text_t value = EMPTY_TEXT;
            if (for_->vars) {
                if (for_->vars->next) {
                    if (for_->vars->next->next)
                        code_err(for_->vars->next->next->ast, "This is too many variables for this loop");

                    index = compile(body_scope, for_->vars->ast);
                    value = compile(body_scope, for_->vars->next->ast);
                } else {
                    value = compile(body_scope, for_->vars->ast);
                }
            }

            Text_t loop = EMPTY_TEXT;
            loop = Texts(loop, "for (int64_t i = 1; i <= iterating.length; ++i)");

            if (index.length > 0) naked_body = Texts("Int_t ", index, " = I(i);\n", naked_body);

            if (value.length > 0) {
                loop = Texts(loop, "{\n", compile_declaration(item_t, value), " = *(", compile_type(item_t),
                             "*)(iterating.data + (i-1)*iterating.stride);\n", naked_body, "\n}");
            } else {
                loop = Texts(loop, "{\n", naked_body, "\n}");
            }

            if (for_->empty)
                loop = Texts("if (iterating.length > 0) {\n", loop, "\n} else ", compile_statement(env, for_->empty));

            if (iter_t->tag == PointerType) {
                loop = Texts("{\n"
                             "List_t *ptr = ",
                             compile_to_pointer_depth(env, for_->iter, 1, false),
                             ";\n"
                             "\nLIST_INCREF(*ptr);\n"
                             "List_t iterating = *ptr;\n",
                             loop, stop,
                             "\nLIST_DECREF(*ptr);\n"
                             "}\n");

            } else {
                loop = Texts("{\n"
                             "List_t iterating = ",
                             compile_to_pointer_depth(env, for_->iter, 0, false), ";\n", loop, stop, "}\n");
            }
            return loop;
        }
        case SetType:
        case TableType: {
            Text_t loop = Text("for (int64_t i = 0; i < iterating.length; ++i) {\n");
            if (for_->vars) {
                if (iter_value_t->tag == SetType) {
                    if (for_->vars->next) code_err(for_->vars->next->ast, "This is too many variables for this loop");
                    Text_t item = compile(body_scope, for_->vars->ast);
                    type_t *item_type = Match(iter_value_t, SetType)->item_type;
                    loop = Texts(loop, compile_declaration(item_type, item), " = *(", compile_type(item_type), "*)(",
                                 "iterating.data + i*iterating.stride);\n");
                } else {
                    Text_t key = compile(body_scope, for_->vars->ast);
                    type_t *key_t = Match(iter_value_t, TableType)->key_type;
                    loop = Texts(loop, compile_declaration(key_t, key), " = *(", compile_type(key_t), "*)(",
                                 "iterating.data + i*iterating.stride);\n");

                    if (for_->vars->next) {
                        if (for_->vars->next->next)
                            code_err(for_->vars->next->next->ast, "This is too many variables for this loop");

                        type_t *value_t = Match(iter_value_t, TableType)->value_type;
                        Text_t value = compile(body_scope, for_->vars->next->ast);
                        Text_t value_offset = Texts("offsetof(struct { ", compile_declaration(key_t, Text("k")), "; ",
                                                    compile_declaration(value_t, Text("v")), "; }, v)");
                        loop = Texts(loop, compile_declaration(value_t, value), " = *(", compile_type(value_t), "*)(",
                                     "iterating.data + i*iterating.stride + ", value_offset, ");\n");
                    }
                }
            }

            loop = Texts(loop, naked_body, "\n}");

            if (for_->empty) {
                loop = Texts("if (iterating.length > 0) {\n", loop, "\n} else ", compile_statement(env, for_->empty));
            }

            if (iter_t->tag == PointerType) {
                loop = Texts("{\n", "Table_t *t = ", compile_to_pointer_depth(env, for_->iter, 1, false),
                             ";\n"
                             "LIST_INCREF(t->entries);\n"
                             "List_t iterating = t->entries;\n",
                             loop,
                             "LIST_DECREF(t->entries);\n"
                             "}\n");
            } else {
                loop = Texts("{\n", "List_t iterating = (", compile_to_pointer_depth(env, for_->iter, 0, false),
                             ").entries;\n", loop, "}\n");
            }
            return loop;
        }
        case BigIntType: {
            Text_t n;
            if (for_->iter->tag == Int) {
                const char *str = Match(for_->iter, Int)->str;
                Int_t int_val = Int$from_str(str);
                if (int_val.small == 0) code_err(for_->iter, "Failed to parse this integer");
                mpz_t i;
                mpz_init_set_int(i, int_val);
                if (mpz_cmpabs_ui(i, BIGGEST_SMALL_INT) <= 0) n = Text$from_str(mpz_get_str(NULL, 10, i));
                else goto big_n;

                if (for_->empty && mpz_cmp_si(i, 0) <= 0) {
                    return compile_statement(env, for_->empty);
                } else {
                    return Texts("for (int64_t i = 1; i <= ", n, "; ++i) {\n",
                                 for_->vars
                                     ? Texts("\tInt_t ", compile(body_scope, for_->vars->ast), " = I_small(i);\n")
                                     : EMPTY_TEXT,
                                 "\t", naked_body, "}\n", stop, "\n");
                }
            }

        big_n:
            n = compile_to_pointer_depth(env, for_->iter, 0, false);
            Text_t i = for_->vars ? compile(body_scope, for_->vars->ast) : Text("i");
            Text_t n_var = for_->vars ? Texts("max", i) : Text("n");
            if (for_->empty) {
                return Texts("{\n"
                             "Int_t ",
                             n_var, " = ", n,
                             ";\n"
                             "if (Int$compare_value(",
                             n_var,
                             ", I(0)) > 0) {\n"
                             "for (Int_t ",
                             i, " = I(1); Int$compare_value(", i, ", ", n_var, ") <= 0; ", i, " = Int$plus(", i,
                             ", I(1))) {\n", "\t", naked_body,
                             "}\n"
                             "} else ",
                             compile_statement(env, for_->empty), stop,
                             "\n"
                             "}\n");
            } else {
                return Texts("for (Int_t ", i, " = I(1), ", n_var, " = ", n, "; Int$compare_value(", i, ", ", n_var,
                             ") <= 0; ", i, " = Int$plus(", i, ", I(1))) {\n", "\t", naked_body, "}\n", stop, "\n");
            }
        }
        case FunctionType:
        case ClosureType: {
            // Iterator function:
            Text_t code = Text("{\n");

            Text_t next_fn;
            if (is_idempotent(for_->iter)) {
                next_fn = compile_to_pointer_depth(env, for_->iter, 0, false);
            } else {
                code = Texts(code, compile_declaration(iter_value_t, Text("next")), " = ",
                             compile_to_pointer_depth(env, for_->iter, 0, false), ";\n");
                next_fn = Text("next");
            }

            __typeof(iter_value_t->__data.FunctionType) *fn =
                iter_value_t->tag == ClosureType ? Match(Match(iter_value_t, ClosureType)->fn, FunctionType)
                                                 : Match(iter_value_t, FunctionType);

            Text_t get_next;
            if (iter_value_t->tag == ClosureType) {
                type_t *fn_t = Match(iter_value_t, ClosureType)->fn;
                arg_t *closure_fn_args = NULL;
                for (arg_t *arg = Match(fn_t, FunctionType)->args; arg; arg = arg->next)
                    closure_fn_args = new (arg_t, .name = arg->name, .type = arg->type, .default_val = arg->default_val,
                                           .next = closure_fn_args);
                closure_fn_args = new (arg_t, .name = "userdata",
                                       .type = Type(PointerType, .pointed = Type(MemoryType)), .next = closure_fn_args);
                REVERSE_LIST(closure_fn_args);
                Text_t fn_type_code =
                    compile_type(Type(FunctionType, .args = closure_fn_args, .ret = Match(fn_t, FunctionType)->ret));
                get_next = Texts("((", fn_type_code, ")", next_fn, ".fn)(", next_fn, ".userdata)");
            } else {
                get_next = Texts(next_fn, "()");
            }

            if (fn->ret->tag == OptionalType) {
                // Use an optional variable `cur` for each iteration step, which
                // will be checked for none
                code = Texts(code, compile_declaration(fn->ret, Text("cur")), ";\n");
                get_next = Texts("(cur=", get_next, ", !", check_none(fn->ret, Text("cur")), ")");
                if (for_->vars) {
                    naked_body = Texts(compile_declaration(Match(fn->ret, OptionalType)->type,
                                                           Texts("_$", Match(for_->vars->ast, Var)->name)),
                                       " = ", optional_into_nonnone(fn->ret, Text("cur")), ";\n", naked_body);
                }
                if (for_->empty) {
                    code = Texts(code, "if (", get_next,
                                 ") {\n"
                                 "\tdo{\n\t\t",
                                 naked_body, "\t} while(", get_next,
                                 ");\n"
                                 "} else {\n\t",
                                 compile_statement(env, for_->empty), "}", stop, "\n}\n");
                } else {
                    code = Texts(code, "while(", get_next, ") {\n\t", naked_body, "}\n", stop, "\n}\n");
                }
            } else {
                if (for_->vars) {
                    naked_body = Texts(compile_declaration(fn->ret, Texts("_$", Match(for_->vars->ast, Var)->name)),
                                       " = ", get_next, ";\n", naked_body);
                } else {
                    naked_body = Texts(get_next, ";\n", naked_body);
                }
                if (for_->empty)
                    code_err(for_->empty, "This iteration loop will always have values, "
                                          "so this block will never run");
                code = Texts(code, "for (;;) {\n\t", naked_body, "}\n", stop, "\n}\n");
            }

            return code;
        }
        default: code_err(for_->iter, "Iteration is not implemented for type: ", type_to_str(iter_t));
        }
    }
    case If: {
        DeclareMatch(if_, ast, If);
        ast_t *condition = if_->condition;
        if (condition->tag == Declare) {
            if (Match(condition, Declare)->value == NULL) code_err(condition, "This declaration must have a value");
            env_t *truthy_scope = fresh_scope(env);
            Text_t code = Texts("IF_DECLARE(", compile_statement(truthy_scope, condition), ", ");
            bind_statement(truthy_scope, condition);
            ast_t *var = Match(condition, Declare)->var;
            code = Texts(code, compile_condition(truthy_scope, var), ", ");
            type_t *cond_t = get_type(truthy_scope, var);
            if (cond_t->tag == OptionalType) {
                set_binding(truthy_scope, Match(var, Var)->name, Match(cond_t, OptionalType)->type,
                            optional_into_nonnone(cond_t, compile(truthy_scope, var)));
            }
            code = Texts(code, compile_statement(truthy_scope, if_->body), ")");
            if (if_->else_body) code = Texts(code, "\nelse ", compile_statement(env, if_->else_body));
            return code;
        } else {
            Text_t code = Texts("if (", compile_condition(env, condition), ")");
            env_t *truthy_scope = env;
            type_t *cond_t = get_type(env, condition);
            if (condition->tag == Var && cond_t->tag == OptionalType) {
                truthy_scope = fresh_scope(env);
                set_binding(truthy_scope, Match(condition, Var)->name, Match(cond_t, OptionalType)->type,
                            optional_into_nonnone(cond_t, compile(truthy_scope, condition)));
            }
            code = Texts(code, compile_statement(truthy_scope, if_->body));
            if (if_->else_body) code = Texts(code, "\nelse ", compile_statement(env, if_->else_body));
            return code;
        }
    }
    case Block: {
        return compile_block(env, ast);
    }
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
