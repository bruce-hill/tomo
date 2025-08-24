// This file defines how to compile doctests

#include "../ast.h"
#include "../config.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/print.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "assignments.h"
#include "declarations.h"
#include "expressions.h"
#include "promotions.h"
#include "statements.h"
#include "types.h"

Text_t compile_doctest(env_t *env, ast_t *ast) {
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
        test_code = Texts("({", compile_declaration(Type(PointerType, lhs_t), Text("expr")), " = &(",
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
        return Texts(setup, "inspect(", compile_type(expr_t), ", ", test_code, ", ", compile_type_info(expr_t), ", ",
                     String((int64_t)(test->expr->start - test->expr->file->text)), ", ",
                     String((int64_t)(test->expr->end - test->expr->file->text)), ");");
    }
}
