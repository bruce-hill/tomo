// Recursive descent parser for parsing code

#include <gc.h>
#include <stdbool.h>
#include <string.h>

#include "../ast.h"
#include "../stdlib/print.h"
#include "../stdlib/util.h"
#include "containers.h"
#include "context.h"
#include "errors.h"
#include "files.h"
#include "functions.h"
#include "numbers.h"
#include "parse.h"
#include "text.h"
#include "types.h"
#include "utils.h"

int op_tightness[] = {
    [Power] = 9,
    [Multiply] = 8,
    [Divide] = 8,
    [Mod] = 8,
    [Mod1] = 8,
    [Plus] = 7,
    [Minus] = 7,
    [Concat] = 6,
    [LeftShift] = 5,
    [RightShift] = 5,
    [UnsignedLeftShift] = 5,
    [UnsignedRightShift] = 5,
    [Min] = 4,
    [Max] = 4,
    [Equals] = 3,
    [NotEquals] = 3,
    [LessThan] = 2,
    [LessThanOrEquals] = 2,
    [GreaterThan] = 2,
    [GreaterThanOrEquals] = 2,
    [Compare] = 2,
    [And] = 1,
    [Or] = 1,
    [Xor] = 1,
};

ast_t *parse_parens(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    spaces(&pos);
    if (!match(&pos, "(")) return NULL;
    whitespace(&pos);
    ast_t *expr = optional(ctx, &pos, parse_extended_expr);
    if (!expr) return NULL;

    ast_t *comprehension = parse_comprehension_suffix(ctx, expr);
    while (comprehension) {
        expr = comprehension;
        pos = comprehension->end;
        comprehension = parse_comprehension_suffix(ctx, expr);
    }

    whitespace(&pos);
    expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this expression");

    // Update the span to include the parens:
    return new (ast_t, .file = (ctx)->file, .start = start, .end = pos, .tag = expr->tag, .__data = expr->__data);
}

ast_t *parse_field_suffix(parse_ctx_t *ctx, ast_t *lhs) {
    if (!lhs) return NULL;
    const char *pos = lhs->end;
    whitespace(&pos);
    if (!match(&pos, ".")) return NULL;
    if (*pos == '.') return NULL;
    whitespace(&pos);
    bool dollar = match(&pos, "$");
    const char *field = get_id(&pos);
    if (!field) return NULL;
    if (dollar) field = String("$", field);
    return NewAST(ctx->file, lhs->start, pos, FieldAccess, .fielded = lhs, .field = field);
}

ast_t *parse_optional_suffix(parse_ctx_t *ctx, ast_t *lhs) {
    if (!lhs) return NULL;
    const char *pos = lhs->end;
    if (match(&pos, "?")) return NewAST(ctx->file, lhs->start, pos, Optional, .value = lhs);
    else return NULL;
}

ast_t *parse_non_optional_suffix(parse_ctx_t *ctx, ast_t *lhs) {
    if (!lhs) return NULL;
    const char *pos = lhs->end;
    if (match(&pos, "!")) return NewAST(ctx->file, lhs->start, pos, NonOptional, .value = lhs);
    else return NULL;
}

ast_t *parse_reduction(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match(&pos, "(")) return NULL;

    whitespace(&pos);
    ast_e op = match_binary_operator(&pos);
    if (op == Unknown) return NULL;

    ast_t *key = NewAST(ctx->file, pos, pos, Var, .name = "$");
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

    whitespace(&pos);
    if (!match(&pos, ":")) return NULL;

    ast_t *iter = optional(ctx, &pos, parse_extended_expr);
    if (!iter) return NULL;
    ast_t *suffixed = parse_comprehension_suffix(ctx, iter);
    while (suffixed) {
        iter = suffixed;
        pos = suffixed->end;
        suffixed = parse_comprehension_suffix(ctx, iter);
    }

    whitespace(&pos);
    expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this reduction");

    return NewAST(ctx->file, start, pos, Reduction, .iter = iter, .op = op, .key = key);
}

ast_t *parse_index_suffix(parse_ctx_t *ctx, ast_t *lhs) {
    if (!lhs) return NULL;
    const char *start = lhs->start;
    const char *pos = lhs->end;
    if (!match(&pos, "[")) return NULL;
    whitespace(&pos);
    ast_t *index = optional(ctx, &pos, parse_extended_expr);
    whitespace(&pos);
    bool unchecked = match(&pos, ";") && (spaces(&pos), match_word(&pos, "unchecked") != 0);
    expect_closing(ctx, &pos, "]", "I wasn't able to parse the rest of this index");
    return NewAST(ctx->file, start, pos, Index, .indexed = lhs, .index = index, .unchecked = unchecked);
}

ast_t *parse_comprehension_suffix(parse_ctx_t *ctx, ast_t *expr) {
    // <expr> "for" [<index>,]<var> "in" <iter> ["if" <condition> | "unless" <condition>]
    if (!expr) return NULL;
    const char *start = expr->start;
    const char *pos = expr->end;
    whitespace(&pos);
    if (!match_word(&pos, "for")) return NULL;

    ast_list_t *vars = NULL;
    for (;;) {
        ast_t *var = optional(ctx, &pos, parse_var);
        if (var) vars = new (ast_list_t, .ast = var, .next = vars);

        spaces(&pos);
        if (!match(&pos, ",")) break;
    }
    REVERSE_LIST(vars);

    expect_str(ctx, start, &pos, "in", "I expected an 'in' for this 'for'");
    ast_t *iter = expect(ctx, start, &pos, parse_expr, "I expected an iterable value for this 'for'");
    const char *next_pos = pos;
    whitespace(&next_pos);
    ast_t *filter = NULL;
    if (match_word(&next_pos, "if")) {
        pos = next_pos;
        filter = expect(ctx, pos - 2, &pos, parse_expr, "I expected a condition for this 'if'");
    } else if (match_word(&next_pos, "unless")) {
        pos = next_pos;
        filter = expect(ctx, pos - 2, &pos, parse_expr, "I expected a condition for this 'unless'");
        filter = WrapAST(filter, Not, filter);
    }
    return NewAST(ctx->file, start, pos, Comprehension, .expr = expr, .vars = vars, .iter = iter, .filter = filter);
}

ast_t *parse_optional_conditional_suffix(parse_ctx_t *ctx, ast_t *stmt) {
    // <statement> "if" <condition> | <statement> "unless" <condition>
    if (!stmt) return stmt;
    const char *start = stmt->start;
    const char *pos = stmt->end;
    if (match_word(&pos, "if")) {
        ast_t *condition = expect(ctx, pos - 2, &pos, parse_expr, "I expected a condition for this 'if'");
        return NewAST(ctx->file, start, pos, If, .condition = condition, .body = stmt);
    } else if (match_word(&pos, "unless")) {
        ast_t *condition = expect(ctx, pos - 2, &pos, parse_expr, "I expected a condition for this 'unless'");
        condition = WrapAST(condition, Not, condition);
        return NewAST(ctx->file, start, pos, If, .condition = condition, .body = stmt);
    } else {
        return stmt;
    }
}

ast_t *parse_if(parse_ctx_t *ctx, const char *pos) {
    // "if" <condition> ["then"] <body> ["else" <body>] | "unless" <condition> <body> ["else" <body>]
    const char *start = pos;
    int64_t starting_indent = get_indent(ctx, pos);

    bool unless;
    if (match_word(&pos, "if")) unless = false;
    else if (match_word(&pos, "unless")) unless = true;
    else return NULL;

    ast_t *condition = unless ? NULL : optional(ctx, &pos, parse_declaration);
    if (!condition) condition = expect(ctx, start, &pos, parse_expr, "I expected to find a condition for this 'if'");

    if (unless) condition = WrapAST(condition, Not, condition);

    (void)match_word(&pos, "then"); // Optional 'then'
    ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a body for this 'if' statement");

    const char *tmp = pos;
    whitespace(&tmp);
    ast_t *else_body = NULL;
    const char *else_start = pos;
    if (get_indent(ctx, tmp) == starting_indent && match_word(&tmp, "else")) {
        pos = tmp;
        spaces(&pos);
        else_body = optional(ctx, &pos, parse_if);
        if (!else_body) else_body = expect(ctx, else_start, &pos, parse_block, "I expected a body for this 'else'");
    }
    return NewAST(ctx->file, start, pos, If, .condition = condition, .body = body, .else_body = else_body);
}

ast_t *parse_when(parse_ctx_t *ctx, const char *pos) {
    // when <expr> (is var : Tag <body>)* [else <body>]
    const char *start = pos;
    int64_t starting_indent = get_indent(ctx, pos);

    if (!match_word(&pos, "when")) return NULL;

    ast_t *subject = optional(ctx, &pos, parse_declaration);
    if (!subject) subject = expect(ctx, start, &pos, parse_expr, "I expected to find an expression for this 'when'");

    when_clause_t *clauses = NULL;
    const char *tmp = pos;
    whitespace(&tmp);
    while (get_indent(ctx, tmp) == starting_indent && match_word(&tmp, "is")) {
        pos = tmp;
        spaces(&pos);
        ast_t *pattern = expect(ctx, start, &pos, parse_expr, "I expected a pattern to match here");
        spaces(&pos);
        when_clause_t *new_clauses = new (when_clause_t, .pattern = pattern, .next = clauses);
        while (match(&pos, ",")) {
            pattern = expect(ctx, start, &pos, parse_expr, "I expected a pattern to match here");
            new_clauses = new (when_clause_t, .pattern = pattern, .next = new_clauses);
            spaces(&pos);
        }
        (void)match_word(&pos, "then"); // Optional 'then'
        ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a body for this 'when' clause");
        for (when_clause_t *c = new_clauses; c && c != clauses; c = c->next) {
            c->body = body;
        }
        clauses = new_clauses;
        tmp = pos;
        whitespace(&tmp);
    }
    REVERSE_LIST(clauses);

    ast_t *else_body = NULL;
    const char *else_start = pos;
    if (get_indent(ctx, tmp) == starting_indent && match_word(&tmp, "else")) {
        pos = tmp;
        else_body = expect(ctx, else_start, &pos, parse_block, "I expected a body for this 'else'");
    }
    return NewAST(ctx->file, start, pos, When, .subject = subject, .clauses = clauses, .else_body = else_body);
}

ast_t *parse_for(parse_ctx_t *ctx, const char *pos) {
    // for [k,] v in iter [<indent>] body
    const char *start = pos;
    if (!match_word(&pos, "for")) return NULL;
    int64_t starting_indent = get_indent(ctx, pos);
    spaces(&pos);
    ast_list_t *vars = NULL;
    for (;;) {
        ast_t *var = optional(ctx, &pos, parse_var);
        if (var) vars = new (ast_list_t, .ast = var, .next = vars);

        spaces(&pos);
        if (!match(&pos, ",")) break;
    }

    spaces(&pos);
    expect_str(ctx, start, &pos, "in", "I expected an 'in' for this 'for'");

    ast_t *iter = expect(ctx, start, &pos, parse_expr, "I expected an iterable value for this 'for'");

    (void)match_word(&pos, "do"); // Optional 'do'

    ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a body for this 'for'");

    const char *else_start = pos;
    whitespace(&else_start);
    ast_t *empty = NULL;
    if (match_word(&else_start, "else") && get_indent(ctx, else_start) == starting_indent) {
        pos = else_start;
        empty = expect(ctx, pos, &pos, parse_block, "I expected a body for this 'else'");
    }
    REVERSE_LIST(vars);
    return NewAST(ctx->file, start, pos, For, .vars = vars, .iter = iter, .body = body, .empty = empty);
}

ast_t *parse_do(parse_ctx_t *ctx, const char *pos) {
    // do [<indent>] body
    const char *start = pos;
    if (!match_word(&pos, "do")) return NULL;
    ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a body for this 'do'");
    return NewAST(ctx->file, start, pos, Block, .statements = Match(body, Block)->statements);
}

ast_t *parse_while(parse_ctx_t *ctx, const char *pos) {
    // while condition ["do"] [<indent>] body
    const char *start = pos;
    if (!match_word(&pos, "while")) return NULL;

    const char *tmp = pos;
    // Shorthand form: `while when ...`
    if (match_word(&tmp, "when")) {
        ast_t *when = expect(ctx, start, &pos, parse_when, "I expected a 'when' block after this");
        if (!when->__data.When.else_body) when->__data.When.else_body = NewAST(ctx->file, pos, pos, Stop);
        return NewAST(ctx->file, start, pos, While, .body = when);
    }

    (void)match_word(&pos, "do"); // Optional 'do'

    ast_t *condition = expect(ctx, start, &pos, parse_expr, "I don't see a viable condition for this 'while'");
    ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a body for this 'while'");
    return NewAST(ctx->file, start, pos, While, .condition = condition, .body = body);
}

ast_t *parse_repeat(parse_ctx_t *ctx, const char *pos) {
    // repeat [<indent>] body
    const char *start = pos;
    if (!match_word(&pos, "repeat")) return NULL;
    ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a body for this 'repeat'");
    return NewAST(ctx->file, start, pos, Repeat, .body = body);
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

ast_t *parse_path(parse_ctx_t *ctx, const char *pos) {
    // "(" ("~/" / "./" / "../" / "/") ... ")"
    const char *start = pos;

    if (!match(&pos, "(")) return NULL;

    if (!(*pos == '~' || *pos == '.' || *pos == '/')) return NULL;

    const char *path_start = pos;
    size_t len = 1;
    int paren_depth = 1;
    while (pos + len < ctx->file->text + ctx->file->len - 1) {
        if (pos[len] == '\\') {
            len += 2;
            continue;
        } else if (pos[len] == '(') {
            paren_depth += 1;
        } else if (pos[len] == ')') {
            paren_depth -= 1;
            if (paren_depth <= 0) break;
        } else if (pos[len] == '\r' || pos[len] == '\n') {
            parser_err(ctx, path_start, &pos[len - 1], "This path was not closed");
        }
        len += 1;
    }
    pos += len + 1;
    char *path = String(string_slice(path_start, .length = len));
    for (char *src = path, *dest = path;;) {
        if (src[0] == '\\') {
            *(dest++) = src[1];
            src += 2;
        } else if (*src) {
            *(dest++) = *(src++);
        } else {
            *(dest++) = '\0';
            break;
        }
    }
    return NewAST(ctx->file, start, pos, Path, .path = path);
}

ast_t *parse_pass(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    return match_word(&pos, "pass") ? NewAST(ctx->file, start, pos, Pass) : NULL;
}

ast_t *parse_defer(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match_word(&pos, "defer")) return NULL;
    ast_t *body = expect(ctx, start, &pos, parse_block, "I expected a block to be deferred here");
    return NewAST(ctx->file, start, pos, Defer, .body = body);
}

ast_t *parse_skip(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match_word(&pos, "continue") && !match_word(&pos, "skip")) return NULL;
    const char *target;
    if (match_word(&pos, "for")) target = "for";
    else if (match_word(&pos, "while")) target = "while";
    else target = get_id(&pos);
    ast_t *skip = NewAST(ctx->file, start, pos, Skip, .target = target);
    skip = parse_optional_conditional_suffix(ctx, skip);
    return skip;
}

ast_t *parse_stop(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match_word(&pos, "stop") && !match_word(&pos, "break")) return NULL;
    const char *target;
    if (match_word(&pos, "for")) target = "for";
    else if (match_word(&pos, "while")) target = "while";
    else target = get_id(&pos);
    ast_t *stop = NewAST(ctx->file, start, pos, Stop, .target = target);
    stop = parse_optional_conditional_suffix(ctx, stop);
    return stop;
}

ast_t *parse_return(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match_word(&pos, "return")) return NULL;
    ast_t *value = optional(ctx, &pos, parse_expr);
    ast_t *ret = NewAST(ctx->file, start, pos, Return, .value = value);
    ret = parse_optional_conditional_suffix(ctx, ret);
    return ret;
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
    whitespace(&pos);
    ast_t *value = expect(ctx, start, &pos, parse_extended_expr, "I expected an expression here");
    whitespace(&pos);
    expect_str(ctx, start, &pos, "->",
               "I expected a `-> Type` for this `deserialize` call so I know what it deserializes to");
    whitespace(&pos);
    type_ast_t *type = expect(ctx, start, &pos, parse_type, "I couldn't parse the type for this deserialization");
    whitespace(&pos);
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

ast_t *parse_method_call_suffix(parse_ctx_t *ctx, ast_t *self) {
    if (!self) return NULL;

    const char *start = self->start;
    const char *pos = self->end;

    if (!match(&pos, ".")) return NULL;
    if (*pos == ' ') return NULL;
    const char *fn = get_id(&pos);
    if (!fn) return NULL;
    spaces(&pos);
    if (!match(&pos, "(")) return NULL;
    whitespace(&pos);

    arg_ast_t *args = NULL;
    for (;;) {
        const char *arg_start = pos;
        const char *name = get_id(&pos);
        whitespace(&pos);
        if (!name || !match(&pos, "=")) {
            name = NULL;
            pos = arg_start;
        }

        ast_t *arg = optional(ctx, &pos, parse_expr);
        if (!arg) {
            if (name) parser_err(ctx, arg_start, pos, "I expected an argument here");
            break;
        }
        args = new (arg_ast_t, .name = name, .value = arg, .next = args);
        if (!match_separator(&pos)) break;
    }
    REVERSE_LIST(args);

    whitespace(&pos);

    if (!match(&pos, ")")) parser_err(ctx, start, pos, "This parenthesis is unclosed");

    return NewAST(ctx->file, start, pos, MethodCall, .self = self, .name = fn, .args = args);
}

ast_t *parse_fncall_suffix(parse_ctx_t *ctx, ast_t *fn) {
    if (!fn) return NULL;

    const char *start = fn->start;
    const char *pos = fn->end;

    if (!match(&pos, "(")) return NULL;

    whitespace(&pos);

    arg_ast_t *args = NULL;
    for (;;) {
        const char *arg_start = pos;
        const char *name = get_id(&pos);
        whitespace(&pos);
        if (!name || !match(&pos, "=")) {
            name = NULL;
            pos = arg_start;
        }

        ast_t *arg = optional(ctx, &pos, parse_expr);
        if (!arg) {
            if (name) parser_err(ctx, arg_start, pos, "I expected an argument here");
            break;
        }
        args = new (arg_ast_t, .name = name, .value = arg, .next = args);
        if (!match_separator(&pos)) break;
    }

    whitespace(&pos);

    if (!match(&pos, ")")) parser_err(ctx, start, pos, "This parenthesis is unclosed");

    REVERSE_LIST(args);
    return NewAST(ctx->file, start, pos, FunctionCall, .fn = fn, .args = args);
}

ast_e match_binary_operator(const char **pos) {
    switch (**pos) {
    case '+': {
        *pos += 1;
        return match(pos, "+") ? Concat : Plus;
    }
    case '-': {
        *pos += 1;
        if ((*pos)[0] != ' ' && (*pos)[-2] == ' ') // looks like `fn -5`
            return Unknown;
        return Minus;
    }
    case '*': *pos += 1; return Multiply;
    case '/': *pos += 1; return Divide;
    case '^': *pos += 1; return Power;
    case '<': {
        *pos += 1;
        if (match(pos, "=")) return LessThanOrEquals; // "<="
        else if (match(pos, ">")) return Compare; // "<>"
        else if (match(pos, "<")) {
            if (match(pos, "<")) return UnsignedLeftShift; // "<<<"
            return LeftShift; // "<<"
        } else return LessThan;
    }
    case '>': {
        *pos += 1;
        if (match(pos, "=")) return GreaterThanOrEquals; // ">="
        if (match(pos, ">")) {
            if (match(pos, ">")) return UnsignedRightShift; // ">>>"
            return RightShift; // ">>"
        }
        return GreaterThan;
    }
    default: {
        if (match(pos, "!=")) return NotEquals;
        else if (match(pos, "==") && **pos != '=') return Equals;
        else if (match_word(pos, "and")) return And;
        else if (match_word(pos, "or")) return Or;
        else if (match_word(pos, "xor")) return Xor;
        else if (match_word(pos, "mod1")) return Mod1;
        else if (match_word(pos, "mod")) return Mod;
        else if (match_word(pos, "_min_")) return Min;
        else if (match_word(pos, "_max_")) return Max;
        else return Unknown;
    }
    }
}

static ast_t *parse_infix_expr(parse_ctx_t *ctx, const char *pos, int min_tightness) {
    ast_t *lhs = optional(ctx, &pos, parse_term);
    if (!lhs) return NULL;

    int64_t starting_line = get_line_number(ctx->file, pos);
    int64_t starting_indent = get_indent(ctx, pos);
    spaces(&pos);
    for (ast_e op; (op = match_binary_operator(&pos)) != Unknown && op_tightness[op] >= min_tightness; spaces(&pos)) {
        ast_t *key = NULL;
        if (op == Min || op == Max) {
            key = NewAST(ctx->file, pos, pos, Var, .name = "$");
            for (bool progress = true; progress;) {
                ast_t *new_term;
                progress =
                    (false || (new_term = parse_index_suffix(ctx, key))
                     || (new_term = parse_method_call_suffix(ctx, key)) || (new_term = parse_field_suffix(ctx, key))
                     || (new_term = parse_fncall_suffix(ctx, key)) || (new_term = parse_optional_suffix(ctx, key))
                     || (new_term = parse_non_optional_suffix(ctx, key)));
                if (progress) key = new_term;
            }
            if (key && key->tag == Var) key = NULL;
            else if (key) pos = key->end;
        }

        whitespace(&pos);
        if (get_line_number(ctx->file, pos) != starting_line && get_indent(ctx, pos) < starting_indent)
            parser_err(ctx, pos, eol(pos), "I expected this line to be at least as indented than the line above it");

        ast_t *rhs = parse_infix_expr(ctx, pos, op_tightness[op] + 1);
        if (!rhs) break;
        pos = rhs->end;

        if (op == Min) {
            return NewAST(ctx->file, lhs->start, rhs->end, Min, .lhs = lhs, .rhs = rhs, .key = key);
        } else if (op == Max) {
            return NewAST(ctx->file, lhs->start, rhs->end, Max, .lhs = lhs, .rhs = rhs, .key = key);
        } else {
            lhs = new (ast_t, .file = ctx->file, .start = lhs->start, .end = rhs->end, .tag = op,
                       .__data.Plus.lhs = lhs, .__data.Plus.rhs = rhs);
        }
    }
    return lhs;
}

ast_t *parse_expr(parse_ctx_t *ctx, const char *pos) { return parse_infix_expr(ctx, pos, 0); }

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

ast_t *parse_assignment(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    ast_list_t *targets = NULL;
    for (;;) {
        ast_t *lhs = optional(ctx, &pos, parse_term);
        if (!lhs) break;
        targets = new (ast_list_t, .ast = lhs, .next = targets);
        spaces(&pos);
        if (!match(&pos, ",")) break;
        whitespace(&pos);
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
        whitespace(&pos);
    }

    REVERSE_LIST(targets);
    REVERSE_LIST(values);

    return NewAST(ctx->file, start, pos, Assign, .targets = targets, .values = values);
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

ast_t *parse_extended_expr(parse_ctx_t *ctx, const char *pos) {
    ast_t *expr = NULL;

    if (false || (expr = optional(ctx, &pos, parse_for)) || (expr = optional(ctx, &pos, parse_while))
        || (expr = optional(ctx, &pos, parse_if)) || (expr = optional(ctx, &pos, parse_when))
        || (expr = optional(ctx, &pos, parse_repeat)) || (expr = optional(ctx, &pos, parse_do)))
        return expr;

    return parse_expr(ctx, pos);
}

ast_t *parse_block(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    spaces(&pos);

    ast_list_t *statements = NULL;
    if (!indent(ctx, &pos)) {
        // Inline block
        spaces(&pos);
        while (*pos) {
            spaces(&pos);
            ast_t *stmt = optional(ctx, &pos, parse_statement);
            if (!stmt) break;
            statements = new (ast_list_t, .ast = stmt, .next = statements);
            spaces(&pos);
            if (!match(&pos, ";")) break;
        }
    } else {
        goto indented;
    }

    if (indent(ctx, &pos)) {
    indented:;
        int64_t block_indent = get_indent(ctx, pos);
        whitespace(&pos);
        while (*pos) {
            ast_t *stmt = optional(ctx, &pos, parse_statement);
            if (!stmt) {
                const char *line_start = pos;
                if (match_word(&pos, "struct"))
                    parser_err(ctx, line_start, eol(pos), "Struct definitions are only allowed at the top level");
                else if (match_word(&pos, "enum"))
                    parser_err(ctx, line_start, eol(pos), "Enum definitions are only allowed at the top level");
                else if (match_word(&pos, "func"))
                    parser_err(ctx, line_start, eol(pos), "Function definitions are only allowed at the top level");
                else if (match_word(&pos, "use"))
                    parser_err(ctx, line_start, eol(pos), "'use' statements are only allowed at the top level");

                spaces(&pos);
                if (*pos && *pos != '\r' && *pos != '\n') parser_err(ctx, pos, eol(pos), "I couldn't parse this line");
                break;
            }
            statements = new (ast_list_t, .ast = stmt, .next = statements);
            whitespace(&pos);

            // Guard against having two valid statements on the same line, separated by spaces (but no newlines):
            if (!memchr(stmt->end, '\n', (size_t)(pos - stmt->end))) {
                if (*pos) parser_err(ctx, pos, eol(pos), "I don't know how to parse the rest of this line");
                pos = stmt->end;
                break;
            }

            if (get_indent(ctx, pos) != block_indent) {
                pos = stmt->end; // backtrack
                break;
            }
        }
    }
    REVERSE_LIST(statements);
    return NewAST(ctx->file, start, pos, Block, .statements = statements);
}

ast_t *parse_namespace(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    whitespace(&pos);
    int64_t indent = get_indent(ctx, pos);
    ast_list_t *statements = NULL;
    for (;;) {
        const char *next = pos;
        whitespace(&next);
        if (get_indent(ctx, next) != indent) break;
        ast_t *stmt;
        if ((stmt = optional(ctx, &pos, parse_struct_def)) || (stmt = optional(ctx, &pos, parse_func_def))
            || (stmt = optional(ctx, &pos, parse_enum_def)) || (stmt = optional(ctx, &pos, parse_lang_def))
            || (stmt = optional(ctx, &pos, parse_extend)) || (stmt = optional(ctx, &pos, parse_convert_def))
            || (stmt = optional(ctx, &pos, parse_use)) || (stmt = optional(ctx, &pos, parse_extern))
            || (stmt = optional(ctx, &pos, parse_inline_c)) || (stmt = optional(ctx, &pos, parse_declaration))) {
            statements = new (ast_list_t, .ast = stmt, .next = statements);
            pos = stmt->end;
            whitespace(&pos); // TODO: check for newline
            // if (!(space_types & WHITESPACE_NEWLINES)) {
            //     pos = stmt->end;
            //     break;
            // }
        } else {
            if (get_indent(ctx, next) > indent && next < eol(next))
                parser_err(ctx, next, eol(next), "I couldn't parse this namespace declaration");
            break;
        }
    }
    REVERSE_LIST(statements);
    return NewAST(ctx->file, start, pos, Block, .statements = statements);
}

ast_t *parse_struct_def(parse_ctx_t *ctx, const char *pos) {
    // struct Foo(...) [: \n body]
    const char *start = pos;
    if (!match_word(&pos, "struct")) return NULL;

    int64_t starting_indent = get_indent(ctx, pos);

    spaces(&pos);
    const char *name = get_id(&pos);
    if (!name) parser_err(ctx, start, pos, "I expected a name for this struct");
    spaces(&pos);

    if (!match(&pos, "(")) parser_err(ctx, pos, pos, "I expected a '(' and a list of fields here");

    arg_ast_t *fields = parse_args(ctx, &pos);

    whitespace(&pos);
    bool secret = false, external = false, opaque = false;
    if (match(&pos, ";")) { // Extra flags
        whitespace(&pos);
        for (;;) {
            if (match_word(&pos, "secret")) {
                secret = true;
            } else if (match_word(&pos, "extern")) {
                external = true;
            } else if (match_word(&pos, "opaque")) {
                if (fields)
                    parser_err(ctx, pos - strlen("opaque"), pos, "A struct can't be opaque if it has fields defined");
                opaque = true;
            } else {
                break;
            }

            if (!match_separator(&pos)) break;
        }
    }

    expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this struct");

    ast_t *namespace = NULL;
    const char *ns_pos = pos;
    whitespace(&ns_pos);
    int64_t ns_indent = get_indent(ctx, ns_pos);
    if (ns_indent > starting_indent) {
        pos = ns_pos;
        namespace = optional(ctx, &pos, parse_namespace);
    }
    if (!namespace) namespace = NewAST(ctx->file, pos, pos, Block, .statements = NULL);
    return NewAST(ctx->file, start, pos, StructDef, .name = name, .fields = fields, .namespace = namespace,
                  .secret = secret, .external = external, .opaque = opaque);
}

ast_t *parse_enum_def(parse_ctx_t *ctx, const char *pos) {
    // tagged union: enum Foo(a, b(x:Int,y:Int)=5, ...) [: \n namespace]
    const char *start = pos;
    if (!match_word(&pos, "enum")) return NULL;
    int64_t starting_indent = get_indent(ctx, pos);
    spaces(&pos);
    const char *name = get_id(&pos);
    if (!name) parser_err(ctx, start, pos, "I expected a name for this enum");
    spaces(&pos);
    if (!match(&pos, "(")) return NULL;

    tag_ast_t *tags = NULL;
    whitespace(&pos);
    for (;;) {
        spaces(&pos);
        const char *tag_name = get_id(&pos);
        if (!tag_name) break;

        spaces(&pos);
        arg_ast_t *fields;
        bool secret = false;
        if (match(&pos, "(")) {
            whitespace(&pos);
            fields = parse_args(ctx, &pos);
            whitespace(&pos);
            if (match(&pos, ";")) { // Extra flags
                whitespace(&pos);
                secret = match_word(&pos, "secret");
                whitespace(&pos);
            }
            expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this tagged union member");
        } else {
            fields = NULL;
        }

        tags = new (tag_ast_t, .name = tag_name, .fields = fields, .secret = secret, .next = tags);

        if (!match_separator(&pos)) break;
    }

    whitespace(&pos);
    expect_closing(ctx, &pos, ")", "I wasn't able to parse the rest of this enum definition");

    REVERSE_LIST(tags);

    if (tags == NULL) parser_err(ctx, start, pos, "This enum does not have any tags!");

    ast_t *namespace = NULL;
    const char *ns_pos = pos;
    whitespace(&ns_pos);
    int64_t ns_indent = get_indent(ctx, ns_pos);
    if (ns_indent > starting_indent) {
        pos = ns_pos;
        namespace = optional(ctx, &pos, parse_namespace);
    }
    if (!namespace) namespace = NewAST(ctx->file, pos, pos, Block, .statements = NULL);

    return NewAST(ctx->file, start, pos, EnumDef, .name = name, .tags = tags, .namespace = namespace);
}

ast_t *parse_lang_def(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    // lang Name: [namespace...]
    if (!match_word(&pos, "lang")) return NULL;
    int64_t starting_indent = get_indent(ctx, pos);
    spaces(&pos);
    const char *name = get_id(&pos);
    if (!name) parser_err(ctx, start, pos, "I expected a name for this lang");
    spaces(&pos);

    ast_t *namespace = NULL;
    const char *ns_pos = pos;
    whitespace(&ns_pos);
    int64_t ns_indent = get_indent(ctx, ns_pos);
    if (ns_indent > starting_indent) {
        pos = ns_pos;
        namespace = optional(ctx, &pos, parse_namespace);
    }
    if (!namespace) namespace = NewAST(ctx->file, pos, pos, Block, .statements = NULL);

    return NewAST(ctx->file, start, pos, LangDef, .name = name, .namespace = namespace);
}

ast_t *parse_extend(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    // extend Name: body...
    if (!match_word(&pos, "extend")) return NULL;
    int64_t starting_indent = get_indent(ctx, pos);
    spaces(&pos);
    const char *name = get_id(&pos);
    if (!name) parser_err(ctx, start, pos, "I expected a name for this lang");

    ast_t *body = NULL;
    const char *ns_pos = pos;
    whitespace(&ns_pos);
    int64_t ns_indent = get_indent(ctx, ns_pos);
    if (ns_indent > starting_indent) {
        pos = ns_pos;
        body = optional(ctx, &pos, parse_namespace);
    }
    if (!body) body = NewAST(ctx->file, pos, pos, Block, .statements = NULL);

    return NewAST(ctx->file, start, pos, Extend, .name = name, .body = body);
}

ast_t *parse_extern(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match_word(&pos, "extern")) return NULL;
    spaces(&pos);
    const char *name = get_id(&pos);
    spaces(&pos);
    if (!match(&pos, ":")) parser_err(ctx, start, pos, "I couldn't get a type for this extern");
    type_ast_t *type = expect(ctx, start, &pos, parse_type, "I couldn't parse the type for this extern");
    return NewAST(ctx->file, start, pos, Extern, .name = name, .type = type);
}

ast_t *parse_doctest(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;
    if (!match(&pos, ">>")) return NULL;
    spaces(&pos);
    ast_t *expr = expect(ctx, start, &pos, parse_statement, "I couldn't parse the expression for this doctest");
    whitespace(&pos);
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
        whitespace(&pos);
        message = expect(ctx, start, &pos, parse_extended_expr, "I couldn't parse the error message for this assert");
    } else {
        pos = expr->end;
    }
    return NewAST(ctx->file, start, pos, Assert, .expr = expr, .message = message);
}

ast_t *parse_use(parse_ctx_t *ctx, const char *pos) {
    const char *start = pos;

    ast_t *var = parse_var(ctx, pos);
    if (var) {
        pos = var->end;
        spaces(&pos);
        if (!match(&pos, ":=")) return NULL;
        spaces(&pos);
    }

    if (!match_word(&pos, "use")) return NULL;
    spaces(&pos);
    size_t name_len = strcspn(pos, " \t\r\n;");
    if (name_len < 1) parser_err(ctx, start, pos, "There is no module name here to use");
    char *name = GC_strndup(pos, name_len);
    pos += name_len;
    while (match(&pos, ";"))
        continue;
    int what;
    if (name[0] == '<' || ends_with(name, ".h")) {
        what = USE_HEADER;
    } else if (starts_with(name, "-l")) {
        what = USE_SHARED_OBJECT;
    } else if (ends_with(name, ".c")) {
        what = USE_C_CODE;
    } else if (ends_with(name, ".S") || ends_with(name, ".s")) {
        what = USE_ASM;
    } else if (starts_with(name, "./") || starts_with(name, "/") || starts_with(name, "../")
               || starts_with(name, "~/")) {
        what = USE_LOCAL;
    } else {
        what = USE_MODULE;
    }
    return NewAST(ctx->file, start, pos, Use, .var = var, .path = name, .what = what);
}

ast_t *parse(const char *str) {
    file_t *file = spoof_file("<string>", str);
    parse_ctx_t ctx = {
        .file = file,
        .on_err = NULL,
    };

    const char *pos = file->text;
    whitespace(&pos);
    ast_t *ast = parse_file_body(&ctx, pos);
    pos = ast->end;
    whitespace(&pos);
    if (pos < file->text + file->len && *pos != '\0')
        parser_err(&ctx, pos, pos + strlen(pos), "I couldn't parse this part of the string");
    return ast;
}

ast_t *parse_expression(const char *str) {
    file_t *file = spoof_file("<string>", str);
    parse_ctx_t ctx = {
        .file = file,
        .on_err = NULL,
    };

    const char *pos = file->text;
    whitespace(&pos);
    ast_t *ast = parse_extended_expr(&ctx, pos);
    pos = ast->end;
    whitespace(&pos);
    if (pos < file->text + file->len && *pos != '\0')
        parser_err(&ctx, pos, pos + strlen(pos), "I couldn't parse this part of the string");
    return ast;
}
