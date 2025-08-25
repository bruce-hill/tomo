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

public
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

public
ast_t *parse_infix_expr(parse_ctx_t *ctx, const char *pos, int min_tightness) {
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
