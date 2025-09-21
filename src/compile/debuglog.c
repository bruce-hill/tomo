// This file defines how to compile debug_log

#include "../ast.h"
#include "../config.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "compilation.h"

public
Text_t compile_debug_log(env_t *env, ast_t *ast) {
    DeclareMatch(debug, ast, DebugLog);
    Text_t code = EMPTY_TEXT;
    for (ast_list_t *value = debug->values; value; value = value->next) {
        type_t *expr_t = get_type(env, value->ast);
        if (!expr_t) code_err(value->ast, "I couldn't figure out the type of this expression");

        Text_t setup = EMPTY_TEXT;
        Text_t value_code;
        if (value->ast->tag == Declare) {
            DeclareMatch(decl, value->ast, Declare);
            type_t *t = decl->type ? parse_type_ast(env, decl->type) : get_type(env, decl->value);
            if (t->tag == FunctionType) t = Type(ClosureType, t);
            Text_t var = Texts("_$", Match(decl->var, Var)->name);
            Text_t val_code = compile_declared_value(env, value->ast);
            setup = Texts(compile_declaration(t, var), ";\n");
            value_code = Texts("(", var, " = ", val_code, ")");
            expr_t = t;
        } else if (value->ast->tag == Assign) {
            DeclareMatch(assign, value->ast, Assign);
            if (!assign->targets->next && assign->targets->ast->tag == Var && is_idempotent(assign->targets->ast)) {
                // Common case: assigning to one variable:
                type_t *lhs_t = get_type(env, assign->targets->ast);
                if (assign->targets->ast->tag == Index && lhs_t->tag == OptionalType
                    && value_type(get_type(env, Match(assign->targets->ast, Index)->indexed))->tag == TableType)
                    lhs_t = Match(lhs_t, OptionalType)->type;
                if (has_stack_memory(lhs_t))
                    code_err(value->ast, "Stack references cannot be assigned "
                                         "to variables because the "
                                         "variable's scope may outlive the "
                                         "scope of the stack memory.");
                env_t *val_scope = with_enum_scope(env, lhs_t);
                value_code = Texts("(",
                                   compile_assignment(env, assign->targets->ast,
                                                      compile_to_type(val_scope, assign->values->ast, lhs_t)),
                                   ")");
                expr_t = lhs_t;
            } else {
                // Multi-assign or assignment to potentially non-idempotent
                // targets
                value_code = Text("({ // Assignment\n");

                int64_t i = 1;
                for (ast_list_t *target = assign->targets, *assign_value = assign->values; target && assign_value;
                     target = target->next, assign_value = assign_value->next) {
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
                    Text_t val_code = compile_to_type(val_scope, assign_value->ast, lhs_t);
                    value_code = Texts(value_code, compile_type(lhs_t), " $", i, " = ", val_code, ";\n");
                    i += 1;
                }
                i = 1;
                for (ast_list_t *target = assign->targets; target; target = target->next) {
                    value_code = Texts(value_code, compile_assignment(env, target->ast, Texts("$", i)), ";\n");
                    i += 1;
                }

                value_code = Texts(value_code, "$1; })");
            }
        } else if (is_update_assignment(value->ast)) {
            binary_operands_t update = UPDATE_OPERANDS(value->ast);
            type_t *lhs_t = get_type(env, update.lhs);
            if (update.lhs->tag == Index) {
                type_t *indexed = value_type(get_type(env, Match(update.lhs, Index)->indexed));
                if (indexed->tag == TableType && Match(indexed, TableType)->default_value == NULL)
                    code_err(update.lhs, "Update assignments are not currently "
                                         "supported for tables");
            }

            ast_t *update_var = new (ast_t);
            *update_var = *value->ast;
            update_var->__data.PlusUpdate.lhs = LiteralCode(Text("(*expr)"), .type = lhs_t); // UNSAFE
            value_code =
                Texts("({", compile_declaration(Type(PointerType, lhs_t), Text("expr")), " = &(",
                      compile_lvalue(env, update.lhs), "); ", compile_statement(env, update_var), "; *expr; })");
            expr_t = lhs_t;
        } else if (expr_t->tag == VoidType || expr_t->tag == AbortType || expr_t->tag == ReturnType) {
            value_code = Texts("({", compile_statement(env, value->ast), " NULL;})");
        } else if (expr_t->tag == FunctionType) {
            expr_t = Type(ClosureType, expr_t);
            value_code = Texts("(Closure_t){.fn=", compile(env, value->ast), "}");
        } else {
            value_code = compile(env, value->ast);
        }
        if (expr_t->tag == VoidType || expr_t->tag == AbortType) {
            value_code = Texts(setup, "inspect_void(", value_code, ", ", compile_type_info(expr_t), ", ",
                               (int64_t)(value->ast->start - value->ast->file->text), ", ",
                               (int64_t)(value->ast->end - value->ast->file->text), ");");
        } else {
            value_code = Texts(setup, "inspect(", compile_type(expr_t), ", ", value_code, ", ",
                               compile_type_info(expr_t), ", ", (int64_t)(value->ast->start - value->ast->file->text),
                               ", ", (int64_t)(value->ast->end - value->ast->file->text), ");");
        }
        code = Texts(code, value_code);
    }
    return code;
}
