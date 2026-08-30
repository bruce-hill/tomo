// Parsing logic for binary operators
#include <stdbool.h>

#include "../ast.h"
#include "../util.h"
#include "context.h"
#include "errors.h"
#include "expressions.h"
#include "suffixes.h"
#include "utils.h"

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
    case '/': {
        *pos += 1;
        return match(pos, "/") ? FloorDivide : Divide;
    }
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

// Whether `outer_op` absorbs `op` into the expression it encloses, rather than
// leaving it for whatever encloses `outer_op` in turn. It absorbs `op` when `op`
// binds more tightly -- or exactly as tightly, if `outer_op` is
// right-associative, which is what groups `a ^ b ^ c` as `a ^ (b ^ c)`.
//
// `outer_op` is Unknown for an expression with nothing around it. That absorbs
// every real operator, since Unknown's tightness of 0 is below all of them.
static bool absorbs(ast_e outer_op, ast_e op) {
    if (op_is_right_associative[outer_op]) return op_tightness[op] >= op_tightness[outer_op];
    return op_tightness[op] > op_tightness[outer_op];
}

ast_t *parse_infix_expr(parse_ctx_t *ctx, const char *pos, ast_e outer_op) {
    ast_t *lhs = optional(ctx, &pos, parse_term);
    if (!lhs) return NULL;

    int64_t starting_line = get_line_number(ctx->file, pos);
    int64_t starting_indent = get_indent(ctx, pos);
    spaces(&pos);
    for (ast_e op; (op = match_binary_operator(&pos)) != Unknown && absorbs(outer_op, op); spaces(&pos)) {
        ast_t *key = NULL;
        if (op == Min || op == Max) {
            key = NewAST(ctx->file, pos, pos, Var, .name = (op == Min ? "_min_" : "_max_"));
            for (bool progress = true; progress;) {
                ast_t *new_term;
                progress =
                    (false || (new_term = parse_index_suffix(ctx, key))
                     || (new_term = parse_method_call_suffix(ctx, key)) || (new_term = parse_field_suffix(ctx, key))
                     || (new_term = parse_fncall_suffix(ctx, key))
                     || (new_term = parse_record_literal_suffix(ctx, key))
                     || (new_term = parse_non_optional_suffix(ctx, key)));
                if (progress) key = new_term;
            }
            if (key && key->tag == Var) key = NULL;
            else if (key) pos = key->end;
        }

        whitespace(ctx, &pos);
        if (get_line_number(ctx->file, pos) != starting_line && get_indent(ctx, pos) < starting_indent)
            parser_err(ctx, pos, eol(pos), "I expected this line to be at least as indented than the line above it");

        ast_t *rhs = parse_infix_expr(ctx, pos, op);
        if (!rhs) break;
        pos = rhs->end;

        // `_min_`/`_max_` keep going like any other operator: returning here
        // abandoned whatever followed, so `a _min_ b == c` (and even
        // `a _min_ b _min_ c`) failed to parse at all.
        if (op == Min) {
            lhs = NewAST(ctx->file, lhs->start, rhs->end, Min, .lhs = lhs, .rhs = rhs, .key = key);
        } else if (op == Max) {
            lhs = NewAST(ctx->file, lhs->start, rhs->end, Max, .lhs = lhs, .rhs = rhs, .key = key);
        } else {
            lhs = new (ast_t, .file = ctx->file, .start = lhs->start, .end = rhs->end, .tag = op,
                       .__data.Plus.lhs = lhs, .__data.Plus.rhs = rhs);
        }
    }
    return lhs;
}
