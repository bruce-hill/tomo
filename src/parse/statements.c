// Logic for parsing statements

#include <gc.h>
#include <stdbool.h>
#include <string.h>

#include "../ast.h"
#include "../stdlib/util.h"
#include "context.h"
#include "errors.h"
#include "expressions.h"
#include "files.h"
#include "statements.h"
#include "suffixes.h"
#include "types.h"
#include "utils.h"

ast_t *parse_declaration(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    ast_t *var = parse_var(ctx, pos);
    if (!var) return NULL;
    pos = var->end;
    spaces(&pos);
    if (!match(&pos, ":")) return NULL;
    spaces(&pos);
    type_ast_t *type = optional(ctx, &pos, parse_type);
    spaces(&pos);
    ast_t *val = NULL;
    if (match(&pos, "=")) {
        val = optional(ctx, &pos, parse_extended_expr);
        if (!val) {
            if (optional(ctx, &pos, parse_use))
                parser_err(ctx, start, pos, "'use' statements are only allowed at the top level of a file");
            else parser_err(ctx, pos, eol(pos), "This is not a valid expression");
        }
    }
    return NewAST(ctx->file, start, pos, Declare, .var = var, .type = type, .value = val);
}

ast_t *parse_assignment(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    ast_list_t *targets = NULL;
    for (;;) {
        ast_t *lhs = optional(ctx, &pos, parse_term);
        if (!lhs) break;
        targets = new (ast_list_t, .ast = lhs, .next = targets);
        spaces(&pos);
        if (!match(&pos, ",")) break;
        whitespace(ctx, &pos);
    }

    if (!targets) return NULL;

    spaces(&pos);
    if (!match(&pos, "=")) return NULL;
    if (match(&pos, "=")) return NULL; // == comparison

    ast_list_t *values = NULL;
    for (;;) {
        ast_t *rhs = optional(ctx, &pos, parse_extended_expr);
        if (!rhs) break;
        values = new (ast_list_t, .ast = rhs, .next = values);
        spaces(&pos);
        if (!match(&pos, ",")) break;
        whitespace(ctx, &pos);
    }

    REVERSE_LIST(targets);
    REVERSE_LIST(values);

    return NewAST(ctx->file, start, pos, Assign, .targets = targets, .values = values);
}

ast_t *parse_update(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    ast_t *lhs = optional(ctx, &pos, parse_expr);
    if (!lhs) return NULL;
    spaces(&pos);
    ast_e op;
    if (match(&pos, "+=")) op = PlusUpdate;
    else if (match(&pos, "++=")) op = ConcatUpdate;
    else if (match(&pos, "-=")) op = MinusUpdate;
    else if (match(&pos, "*=")) op = MultiplyUpdate;
    else if (match(&pos, "/=")) op = DivideUpdate;
    else if (match(&pos, "^=")) op = PowerUpdate;
    else if (match(&pos, "<<=")) op = LeftShiftUpdate;
    else if (match(&pos, "<<<=")) op = UnsignedLeftShiftUpdate;
    else if (match(&pos, ">>=")) op = RightShiftUpdate;
    else if (match(&pos, ">>>=")) op = UnsignedRightShiftUpdate;
    else if (match(&pos, "and=")) op = AndUpdate;
    else if (match(&pos, "or=")) op = OrUpdate;
    else if (match(&pos, "xor=")) op = XorUpdate;
    else return NULL;
    ast_t *rhs = expect(ctx, start, &pos, parse_extended_expr, "I expected an expression here");
    return new (ast_t, .file = ctx->file, .start = start, .end = pos, .tag = op, .__data.PlusUpdate.lhs = lhs,
                .__data.PlusUpdate.rhs = rhs);
}

ast_t *parse_doctest(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match(&pos, ">>")) return NULL;
    spaces(&pos);
    ast_t *expr = expect(ctx, start, &pos, parse_statement, "I couldn't parse the expression for this doctest");
    whitespace(ctx, &pos);
    ast_t *expected = NULL;
    if (match(&pos, "=")) {
        spaces(&pos);
        expected = expect(ctx, start, &pos, parse_extended_expr, "I couldn't parse the expected expression here");
    } else {
        pos = expr->end;
    }
    return NewAST(ctx->file, start, pos, DocTest, .expr = expr, .expected = expected);
}

ast_t *parse_assert(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match_word(&pos, "assert")) return NULL;
    spaces(&pos);
    ast_t *expr = expect(ctx, start, &pos, parse_extended_expr, "I couldn't parse the expression for this assert");
    spaces(&pos);
    ast_t *message = NULL;
    if (match(&pos, ",")) {
        whitespace(ctx, &pos);
        message = expect(ctx, start, &pos, parse_extended_expr, "I couldn't parse the error message for this assert");
    } else {
        pos = expr->end;
    }
    return NewAST(ctx->file, start, pos, Assert, .expr = expr, .message = message);
}

ast_t *parse_statement(parse_ctx_t *ctx, const char *pos) {
    ast_t *stmt = NULL;
    if ((stmt = parse_declaration(ctx, pos)) || (stmt = parse_doctest(ctx, pos)) || (stmt = parse_assert(ctx, pos)))
        return stmt;

    if (!(false || (stmt = parse_update(ctx, pos)) || (stmt = parse_assignment(ctx, pos))))
        stmt = parse_extended_expr(ctx, pos);

    for (bool progress = (stmt != NULL); progress;) {
        ast_t *new_stmt;
        progress = false;
        if (stmt->tag == Var) {
            progress = (false || (new_stmt = parse_method_call_suffix(ctx, stmt))
                        || (new_stmt = parse_fncall_suffix(ctx, stmt)));
        } else if (stmt->tag == FunctionCall) {
            new_stmt = parse_optional_conditional_suffix(ctx, stmt);
            progress = (new_stmt != stmt);
        }

        if (progress) stmt = new_stmt;
    }
    return stmt;
}
