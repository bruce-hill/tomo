// This file defines how to compile assertions

#include "../ast.h"
#include "../config.h"
#include "../environment.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/text.h"
#include "../stdlib/util.h"
#include "../typecheck.h"
#include "compilation.h"

public
Text_t compile_assertion(env_t *env, ast_t *ast) {
    ast_t *expr = Match(ast, Assert)->expr;
    ast_t *message = Match(ast, Assert)->message;
    const char *failure = NULL;
    switch (expr->tag) {
    case And: {
        DeclareMatch(and_, expr, And);
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
    assert_comparison: {
        binary_operands_t cmp = BINARY_OPERANDS(expr);
        type_t *lhs_t = get_type(env, cmp.lhs);
        type_t *rhs_t = get_type(with_enum_scope(env, lhs_t), cmp.rhs);
        type_t *operand_t;
        if (type_eq(lhs_t, rhs_t)) {
            operand_t = lhs_t;
        } else if (cmp.lhs->tag == Int && is_numeric_type(rhs_t)) {
            operand_t = rhs_t;
        } else if (cmp.rhs->tag == Int && is_numeric_type(lhs_t)) {
            operand_t = lhs_t;
        } else if (can_compile_to_type(with_enum_scope(env, lhs_t), cmp.rhs, lhs_t)) {
            operand_t = lhs_t;
        } else if (can_compile_to_type(env, cmp.lhs, rhs_t)) {
            operand_t = rhs_t;
        } else {
            code_err(ast, "I can't do comparisons between ", type_to_text(lhs_t), " and ", type_to_text(rhs_t));
        }

        ast_t *lhs_var = FakeAST(InlineCCode, .chunks = new (ast_list_t, .ast = FakeAST(TextLiteral, Text("_lhs"))),
                                 .type = operand_t);
        ast_t *rhs_var = FakeAST(InlineCCode, .chunks = new (ast_list_t, .ast = FakeAST(TextLiteral, Text("_rhs"))),
                                 .type = operand_t);
        ast_t *var_comparison = new (ast_t, .file = expr->file, .start = expr->start, .end = expr->end,
                                     .tag = expr->tag, .__data.Equals = {.lhs = lhs_var, .rhs = rhs_var});
        int64_t line = get_line_number(ast->file, ast->start);
        return Texts(
            "{ // assertion\n", compile_declaration(operand_t, Text("_lhs")), " = ",
            compile_to_type(env, cmp.lhs, operand_t), ";\n", "\n#line ", line, "\n",
            compile_declaration(operand_t, Text("_rhs")), " = ", compile_to_type(env, cmp.rhs, operand_t), ";\n",
            "\n#line ", line, "\n", "if (!(", compile_condition(env, var_comparison), "))\n", "#line ", line, "\n",
            Texts("fail_source(", quoted_str(ast->file->filename), ", ", (int64_t)(expr->start - expr->file->text),
                  ", ", (int64_t)(expr->end - expr->file->text), ", Text$concat(",
                  message ? compile_to_type(env, message, Type(TextType)) : Text("Text(\"This assertion failed!\")"),
                  ", Text(\" (\"), ", expr_as_text(Text("_lhs"), operand_t, Text("no")), ", Text(\" ", failure,
                  " \"), ", expr_as_text(Text("_rhs"), operand_t, Text("no")), ", Text(\")\")));\n"),
            "}\n");
    }
    default: {
        int64_t line = get_line_number(ast->file, ast->start);
        return Texts("if (!(", compile_condition(env, expr), "))\n", "#line ", line, "\n", "fail_source(",
                     quoted_str(ast->file->filename), ", ", (int64_t)(expr->start - expr->file->text), ", ",
                     (int64_t)(expr->end - expr->file->text), ", ",
                     message ? compile_to_type(env, message, Type(TextType)) : Text("Text(\"This assertion failed!\")"),
                     ");\n");
    }
    }
}
