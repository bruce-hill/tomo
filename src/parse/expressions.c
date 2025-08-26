// Recursive descent parser for parsing code

#include <stdbool.h>
#include <string.h>

#include "../ast.h"
#include "../stdlib/util.h"
#include "binops.h"
#include "containers.h"
#include "context.h"
#include "controlflow.h"
#include "errors.h"
#include "expressions.h"
#include "files.h"
#include "functions.h"
#include "numbers.h"
#include "suffixes.h"
#include "text.h"
#include "types.h"
#include "utils.h"

ast_t *parse_parens(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    spaces(&pos);
    if (!match(&pos, "(")) return NULL;
    whitespace(ctx, &pos);
    ast_t *expr = optional(ctx, &pos, parse_extended_expr);
    if (!expr) return NULL;

    ast_t *comprehension = parse_comprehension_suffix(ctx, expr);
    while (comprehension) {
        expr = comprehension;
        pos = comprehension->end;
        comprehension = parse_comprehension_suffix(ctx, expr);
    }

    whitespace(ctx, &pos);
    expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this expression");

    // Update the span to include the parens:
    return new (ast_t, .file = (ctx)->file, .start = start, .end = pos, .tag = expr->tag, .__data = expr->__data);
}

ast_t *parse_reduction(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match(&pos, "(")) return NULL;

    whitespace(ctx, &pos);
    ast_e op = match_binary_operator(&pos);
    if (op == Unknown) return NULL;

    const char *op_str = binop_operator(op);
    assert(op_str);
    ast_t *key = NewAST(ctx->file, pos, pos, Var, .name = op_str);
    for (bool progress = true; progress;) {
        ast_t *new_term;
        progress =
            (false || (new_term = parse_index_suffix(ctx, key)) || (new_term = parse_method_call_suffix(ctx, key))
             || (new_term = parse_field_suffix(ctx, key)) || (new_term = parse_fncall_suffix(ctx, key))
             || (new_term = parse_optional_suffix(ctx, key)) || (new_term = parse_non_optional_suffix(ctx, key)));
        if (progress) key = new_term;
    }
    if (key && key->tag == Var) key = NULL;
    else if (key) pos = key->end;

    whitespace(ctx, &pos);
    if (!match(&pos, ":")) return NULL;

    ast_t *iter = optional(ctx, &pos, parse_extended_expr);
    if (!iter) return NULL;
    ast_t *suffixed = parse_comprehension_suffix(ctx, iter);
    while (suffixed) {
        iter = suffixed;
        pos = suffixed->end;
        suffixed = parse_comprehension_suffix(ctx, iter);
    }

    whitespace(ctx, &pos);
    expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this reduction");

    return NewAST(ctx->file, start, pos, Reduction, .iter = iter, .op = op, .key = key);
}

ast_t *parse_heap_alloc(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match(&pos, "@")) return NULL;
    spaces(&pos);
    ast_t *val = expect(ctx, start, &pos, parse_term_no_suffix, "I expected an expression for this '@'");

    for (;;) {
        ast_t *new_term;
        if ((new_term = parse_index_suffix(ctx, val)) || (new_term = parse_fncall_suffix(ctx, val))
            || (new_term = parse_method_call_suffix(ctx, val)) || (new_term = parse_field_suffix(ctx, val))) {
            val = new_term;
        } else break;
    }
    pos = val->end;

    ast_t *ast = NewAST(ctx->file, start, pos, HeapAllocate, .value = val);
    for (;;) {
        ast_t *next = parse_optional_suffix(ctx, ast);
        if (!next) next = parse_non_optional_suffix(ctx, ast);
        if (!next) break;
        ast = next;
    }
    return ast;
}

ast_t *parse_stack_reference(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match(&pos, "&")) return NULL;
    spaces(&pos);
    ast_t *val = expect(ctx, start, &pos, parse_term_no_suffix, "I expected an expression for this '&'");

    for (;;) {
        ast_t *new_term;
        if ((new_term = parse_index_suffix(ctx, val)) || (new_term = parse_fncall_suffix(ctx, val))
            || (new_term = parse_method_call_suffix(ctx, val)) || (new_term = parse_field_suffix(ctx, val))) {
            val = new_term;
        } else break;
    }
    pos = val->end;

    ast_t *ast = NewAST(ctx->file, start, pos, StackReference, .value = val);
    for (;;) {
        ast_t *next = parse_optional_suffix(ctx, ast);
        if (!next) next = parse_non_optional_suffix(ctx, ast);
        if (!next) break;
        ast = next;
    }
    return ast;
}

ast_t *parse_not(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match_word(&pos, "not")) return NULL;
    spaces(&pos);
    ast_t *val = expect(ctx, start, &pos, parse_term, "I expected an expression for this 'not'");
    return NewAST(ctx->file, start, pos, Not, .value = val);
}

ast_t *parse_negative(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match(&pos, "-")) return NULL;
    spaces(&pos);
    ast_t *val = expect(ctx, start, &pos, parse_term, "I expected an expression for this '-'");
    return NewAST(ctx->file, start, pos, Negative, .value = val);
}

ast_t *parse_bool(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (match_word(&pos, "yes")) return NewAST(ctx->file, start, pos, Bool, .b = true);
    else if (match_word(&pos, "no")) return NewAST(ctx->file, start, pos, Bool, .b = false);
    else return NULL;
}

ast_t *parse_none(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match_word(&pos, "none")) return NULL;
    return NewAST(ctx->file, start, pos, None);
}

ast_t *parse_deserialize(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match_word(&pos, "deserialize")) return NULL;

    spaces(&pos);
    expect_str(ctx, start, &pos, "(", "I expected arguments for this `deserialize` call");
    whitespace(ctx, &pos);
    ast_t *value = expect(ctx, start, &pos, parse_extended_expr, "I expected an expression here");
    whitespace(ctx, &pos);
    expect_str(ctx, start, &pos, "->",
               "I expected a `-> Type` for this `deserialize` call so I know what it deserializes to");
    whitespace(ctx, &pos);
    type_ast_t *type = expect(ctx, start, &pos, parse_type, "I couldn't parse the type for this deserialization");
    whitespace(ctx, &pos);
    expect_closing(ctx, &pos, ")", "I expected a closing ')' for this `deserialize` call");
    return NewAST(ctx->file, start, pos, Deserialize, .value = value, .type = type);
}

ast_t *parse_var(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    const char *name = get_id(&pos);
    if (!name) return NULL;
    return NewAST(ctx->file, start, pos, Var, .name = name);
}

ast_t *parse_term_no_suffix(parse_ctx_t *ctx, const char *pos) {
    spaces(&pos);
    ast_t *term = NULL;
    (void)(false || (term = parse_none(ctx, pos)) || (term = parse_num(ctx, pos)) // Must come before int
           || (term = parse_int(ctx, pos)) || (term = parse_negative(ctx, pos)) // Must come after num/int
           || (term = parse_heap_alloc(ctx, pos)) || (term = parse_stack_reference(ctx, pos))
           || (term = parse_bool(ctx, pos)) || (term = parse_text(ctx, pos)) || (term = parse_path(ctx, pos))
           || (term = parse_lambda(ctx, pos)) || (term = parse_parens(ctx, pos)) || (term = parse_table(ctx, pos))
           || (term = parse_set(ctx, pos)) || (term = parse_deserialize(ctx, pos)) || (term = parse_var(ctx, pos))
           || (term = parse_list(ctx, pos)) || (term = parse_reduction(ctx, pos)) || (term = parse_pass(ctx, pos))
           || (term = parse_defer(ctx, pos)) || (term = parse_skip(ctx, pos)) || (term = parse_stop(ctx, pos))
           || (term = parse_return(ctx, pos)) || (term = parse_not(ctx, pos)) || (term = parse_extern(ctx, pos))
           || (term = parse_inline_c(ctx, pos)));
    return term;
}

ast_t *parse_term(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (match(&pos, "???")) parser_err(ctx, start, pos, "This value needs to be filled in!");

    ast_t *term = parse_term_no_suffix(ctx, pos);
    if (!term) return NULL;

    for (bool progress = true; progress;) {
        ast_t *new_term;
        progress =
            (false || (new_term = parse_index_suffix(ctx, term)) || (new_term = parse_method_call_suffix(ctx, term))
             || (new_term = parse_field_suffix(ctx, term)) || (new_term = parse_fncall_suffix(ctx, term))
             || (new_term = parse_optional_suffix(ctx, term)) || (new_term = parse_non_optional_suffix(ctx, term)));
        if (progress) term = new_term;
    }
    return term;
}

ast_t *parse_expr(parse_ctx_t *ctx, const char *pos) { return parse_infix_expr(ctx, pos, 0); }

ast_t *parse_extended_expr(parse_ctx_t *ctx, const char *pos) {
    ast_t *expr = NULL;

    if (false || (expr = optional(ctx, &pos, parse_for)) || (expr = optional(ctx, &pos, parse_while))
        || (expr = optional(ctx, &pos, parse_if)) || (expr = optional(ctx, &pos, parse_when))
        || (expr = optional(ctx, &pos, parse_repeat)) || (expr = optional(ctx, &pos, parse_do)))
        return expr;

    return parse_expr(ctx, pos);
}

ast_t *parse_expr_str(const char *str) {
    file_t *file = spoof_file("<string>", str);
    parse_ctx_t ctx = {
        .file = file,
        .on_err = NULL,
    };

    const char *pos = file->text;
    whitespace(&ctx, &pos);
    ast_t *ast = parse_extended_expr(&ctx, pos);
    pos = ast->end;
    whitespace(&ctx, &pos);
    if (pos < file->text + file->len && *pos != '\0')
        parser_err(&ctx, pos, pos + strlen(pos), "I couldn't parse this part of the string");
    return ast;
}
