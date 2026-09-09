// This code defines functions for transforming ASTs back into Tomo source text

#include <assert.h>
#include <limits.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/param.h>
#include <unictype.h>

#include "../ast.h"
#include "../parse/context.h"
#include "../parse/files.h"
#include "../parse/utils.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/integers.h"
#include "../stdlib/number.h"
#include "../stdlib/optionals.h"
#include "../stdlib/text.h"
#include "../util.h"
#include "args.h"
#include "enums.h"
#include "formatter.h"
#include "types.h"
#include "utils.h"

#define fmt_inline(...) must(format_inline_code(__VA_ARGS__))
#define fmt(...) format_code(__VA_ARGS__)
#define fmt_at(...) format_code_at(__VA_ARGS__)

static OptionalText_t format_binop_inline(ast_t *ast, Table_t comments, int tighten_from);
static Text_t format_binop(ast_t *ast, Table_t comments, Text_t indent, int64_t column, int tighten_from);

Text_t format_namespace(ast_t *namespace, Table_t comments, Text_t indent) {
    if (unwrap_block(namespace) == NULL) return EMPTY_TEXT;
    return Texts("\n", indent, single_indent, fmt(namespace, comments, Texts(indent, single_indent)));
}

typedef struct {
    Text_t quote, unquote, interp;
    // Inline C is parsed verbatim (no backslash escapes), so its text has to be
    // written back out the same way: escaping it would turn `Text$from_str` in
    // the C code into a literal `Text\$from_str`, growing a backslash on every
    // reformat.
    bool verbatim;
} text_opts_t;

PUREFUNC text_opts_t choose_text_options(ast_list_t *chunks) {
    int double_quotes = 0, single_quotes = 0, backticks = 0;
    for (ast_list_t *chunk = chunks; chunk; chunk = chunk->next) {
        if (chunk->ast->tag == TextLiteral) {
            Text_t literal = Match(chunk->ast, TextLiteral)->text;
            if (Text$has(literal, Text("\""))) double_quotes += 1;
            if (Text$has(literal, Text("'"))) single_quotes += 1;
            if (Text$has(literal, Text("`"))) backticks += 1;
        }
    }
    Text_t quote;
    if (double_quotes == 0) quote = Text("\"");
    else if (single_quotes == 0) quote = Text("'");
    else if (backticks == 0) quote = Text("`");
    else quote = Text("\"");

    text_opts_t opts = {.quote = quote, .unquote = quote, .interp = Text("$")};
    return opts;
}

// Whether the author wrote this literal as a block: opening quote alone on its
// line, text indented beneath it. A block's line breaks belong to its text, so
// it keeps them. The other multi-line layout, a line split, starts its text
// right after the quote and its breaks mean nothing, so it is free to be
// rejoined. The quote is found rather than taken from the node's start, which
// sits before the `$Lang` or `C_code:` a literal can be introduced by.
PUREFUNC static bool is_block_text(ast_t *ast) {
    const char *p = ast->start + strcspn(ast->start, "\"'`");
    char quote = *p;
    p += (p[1] == quote && p[2] == quote) ? 3 : 1;
    for (;; p++) {
        if (*p == '\n') return true;
        if (*p != ' ' && *p != '\t' && *p != '\r') return false;
    }
}

// Word operators (`mod`, `and`, `_min_`, ...) always need surrounding spaces,
// however tightly they bind: `(x + y)modlen` isn't parseable code.
PUREFUNC static bool is_word_operator(const char *op) {
    return op && (uc_is_property_xid_start((ucs4_t)(unsigned char)op[0]) || op[0] == '_');
}

// `test` labels and `fails` expectations are stored as plain strings (they're
// parsed with interpolation disabled), so they have to be re-quoted from
// scratch rather than formatted as a text literal.
static Text_t quoted_label(const char *label) {
    return Text$quoted(Text$from_str(label), false, Text("\""));
}

// The flags a definition can carry after its arguments, as one `; a, b` list.
//
// The cache flag is written `cached` or `cache_size=N`; a bare `cached` parses
// to the sentinel size -1. There is no `cache=` flag: emitting one lost the
// caching entirely and left a function definition that didn't parse.
static flag_list_t signature_flags(ast_t *cache, bool is_inline, Table_t comments, Text_t indent) {
    flag_list_t flags = {};
    add_flag(&flags, is_inline, Text("inline"));
    if (cache) {
        if (cache->tag == Int && Int$equal_value(Match(cache, Int)->i, I_small(-1))) {
            add_flag(&flags, true, Text("cached"));
        } else {
            // Measured from the inner indent, the line of its own that this
            // flag gets whenever the arguments above it wrap.
            int64_t column = (int64_t)(indent.length + single_indent.length);
            add_flag(&flags, true, Texts("cache_size=", fmt_at(cache, comments, indent, column + 13)));
        }
    }
    return flags;
}

static Text_t format_signature(arg_ast_t *args, type_ast_t *ret_type, ast_t *cache, bool is_inline, Table_t comments,
                               Text_t indent, int64_t column) {
    Text_t ret_code = ret_type ? Texts("-> ", format_type(ret_type)) : EMPTY_TEXT;
    flag_list_t flags = signature_flags(cache, is_inline, comments, indent);
    return format_bracketed_args(args, ret_code, flags, comments, indent, column, "(", ")");
}

static bool starts_with_id(Text_t text) {
    if (text.length <= 0) return false;
    List_t codepoints = Text$utf32(Text$slice(text, I_small(1), I_small(1)));
    if (codepoints.length <= 0 || codepoints.data == NULL) return false;
    return uc_is_property_xid_continue(*(ucs4_t *)codepoints.data);
}

// An interpolation is written bare only when nothing could run on from it: a
// `$x` followed by more name characters would read as one longer name. Anything
// but a plain variable gets parentheses whatever follows, since `$x.y` and
// `$f(1)` take their suffixes into the interpolation rather than the text.
PUREFUNC static bool interpolation_needs_parens(ast_list_t *chunk) {
    if (chunk->ast->tag != Var) return true;
    ast_list_t *next = chunk->next;
    return next && next->ast->tag == TextLiteral && starts_with_id(Match(next->ast, TextLiteral)->text);
}

static Text_t comment_range(const char **pos, const char *end, Text_t indent, Table_t comments) {
    Text_t ret = EMPTY_TEXT;
    const char *prev = NULL;
    for (OptionalText_t comment; (comment = next_comment(comments, pos, end)).length > 0;) {
        if (prev) {
            for (const char *p = prev + 1; p < *pos; p++) {
                if (*p == '\n') {
                    ret = Text$concat(ret, Text("\n"));
                    break;
                }
            }
        }
        add_line(&ret, Text$trim(comment, Text(" \t\r\n"), false, true), indent);
        prev = *pos;
    }
    return ret;
}

static OptionalText_t format_inline_text(text_opts_t opts, ast_list_t *chunks, Table_t comments) {
    Text_t code = opts.quote;
    for (ast_list_t *chunk = chunks; chunk; chunk = chunk->next) {
        if (chunk->ast->tag == TextLiteral) {
            Text_t literal = Match(chunk->ast, TextLiteral)->text;
            if (opts.verbatim) {
                // Nothing can be escaped here, so anything that would need to be
                // (a newline, or the quote character itself) has to go multiline.
                if (Text$has(literal, Text("\n")) || Text$has(literal, opts.quote)) return NONE_TEXT;
                code = Texts(code, literal);
                continue;
            }
            Text_t segment = Text$escaped(literal, false, Texts(opts.unquote, opts.interp));
            code = Texts(code, segment);
        } else {
            Text_t chunk_code = fmt_inline(chunk->ast, comments);
            if (interpolation_needs_parens(chunk)) chunk_code = Texts("(", chunk_code, ")");
            code = Texts(code, opts.interp, chunk_code);
        }
    }
    return Texts(code, opts.unquote);
}

// Two dots at the literal's own indentation is all the parser asks for to
// continue the line above; a full indent's worth of them lines the continued
// text up with where the text of a block would sit.
static const Text_t continuation_marker = Text("....");

// A continued line picks up where the one above it left off with nothing in
// between, so text too wide for the page can be broken across lines. It is
// built one atom at a time -- a single escaped grapheme, or a whole
// interpolation -- so a break can never land inside one.
typedef struct {
    Text_t code; // the lines finished so far, each ending in a newline
    Text_t line; // the line being built
    Text_t indent; // the indentation the literal itself sits at
    Text_t marker; // what stands between that indentation and `line`
    bool own_line; // false for the text that follows the opening quote
    bool verbatim; // whether this text can hold a backslash escape at all
} text_wrap_t;

// What stands before `line` on the page. Text sharing the opening quote's line
// is written after the quote by the caller, so it contributes its width
// without being written here. What the statement put in front of the literal
// is not counted -- nothing in the formatter knows its own column, so a first
// line can still come out over the limit by the width of the `x := ` before it.
static Text_t line_start(text_wrap_t *w) {
    return w->own_line ? Texts(w->indent, w->marker) : EMPTY_TEXT;
}

static void end_line(text_wrap_t *w, Text_t marker) {
    if (w->line.length > 0) w->code = Texts(w->code, line_start(w), w->line);
    w->code = Texts(w->code, "\n");
    w->line = EMPTY_TEXT;
    w->marker = marker;
    w->own_line = true;
}

static void append_atom(text_wrap_t *w, Text_t atom) {
    if (w->line.length > 0 && w->indent.length + w->marker.length + w->line.length + atom.length > MAX_WIDTH) {
        // Every dot at the front of a continued line belongs to the `..` that
        // marks it, so a dot of the text's own has to be written escaped to
        // survive there. Verbatim text has no escapes, and a run of dots in it
        // simply can't be broken -- it stays on the line it started on.
        bool needs_escape = Text$starts_with(atom, Text("."), NULL);
        if (!needs_escape || !w->verbatim) {
            end_line(w, continuation_marker);
            if (needs_escape) atom = Texts("\\", atom);
        }
    }
    w->line = Texts(w->line, atom);
}

// A literal that can't be written on one line is written as a block: the
// opening quote alone on its line, the text indented beneath it, and any line
// too wide for the page continued with dots. Every line then begins at one of
// two columns, which is what makes a column of text line up.
//
// A quote in the text needs no escape here. The parser only reads one as the
// closing quote at the very start of a line at the literal's own indentation,
// and every line written here starts either an indent deeper than that or with
// the dots of a continuation.
static Text_t format_text(text_opts_t opts, ast_list_t *chunks, Table_t comments, Text_t indent) {
    text_wrap_t w = {.indent = indent, .marker = single_indent, .own_line = true, .verbatim = opts.verbatim};
    for (ast_list_t *chunk = chunks; chunk; chunk = chunk->next) {
        if (chunk->ast->tag == TextLiteral) {
            Text_t literal = Match(chunk->ast, TextLiteral)->text;
            for (int64_t i = 1; i <= (int64_t)literal.length; i += 1) {
                Text_t grapheme = Text$slice(literal, I_small(i), I_small(i));
                if (Text$equal_values(grapheme, Text("\n"))) end_line(&w, single_indent);
                else append_atom(&w, opts.verbatim ? grapheme : Text$escaped(grapheme, false, opts.interp));
            }
        } else {
            // A newline inside a text literal is part of the text, so an
            // interpolation has to stay on one line whatever it holds. The
            // fallback is for the few expressions with no one-line form at
            // all, which cannot be written here in any case.
            OptionalText_t inlined_chunk = format_inline_code(chunk->ast, comments);
            Text_t chunk_code =
                inlined_chunk.tag != TEXT_NONE ? (Text_t)inlined_chunk : fmt(chunk->ast, comments, indent);
            if (interpolation_needs_parens(chunk)) chunk_code = Texts("(", chunk_code, ")");
            append_atom(&w, Texts(opts.interp, chunk_code));
        }
    }
    Text_t last = w.line.length > 0 ? Texts(line_start(&w), w.line) : EMPTY_TEXT;
    return Texts(opts.quote, "\n", w.code, last, "\n", indent, opts.unquote);
}

// Whether a `!` sits on this expression's suffix spine. `@` and `&` bind every
// suffix but `!` into themselves and leave `!` outside (see parse_heap_alloc),
// so an operand carrying one has to be parenthesized to stay the operand:
// written bare, `&(f.A!)` comes back as `(&f.A)!`.
PUREFUNC static bool has_nonoptional_suffix(ast_t *ast) {
    for (;;) {
        switch (ast->tag) {
        case NonOptional: return true;
        case FieldAccess: ast = Match(ast, FieldAccess)->fielded; break;
        case Index: ast = Match(ast, Index)->indexed; break;
        case MethodCall: ast = Match(ast, MethodCall)->self; break;
        case FunctionCall: ast = Match(ast, FunctionCall)->fn; break;
        case RecordLiteral: ast = Match(ast, RecordLiteral)->type; break;
        default: return false;
        }
    }
}

PUREFUNC static int64_t trailing_line_len(Text_t text) {
    TextIter_t state = NEW_TEXT_ITER_STATE(text);
    int64_t len = 0;
    for (int64_t i = text.length - 1; i >= 0; i--) {
        int32_t g = Text$get_grapheme_fast(&state, i);
        if (g == '\n' || g == '\r') break;
        len += 1;
    }
    return len;
}

// Where writing `code` from `column` leaves the cursor. A rendering that stayed
// on one line ends that many columns further along; one that wrapped carries its
// own indentation on its last line, so that line's length is already the column.
PUREFUNC static int64_t column_after(int64_t column, Text_t code) {
    int64_t last_line = trailing_line_len(code);
    return last_line < (int64_t)code.length ? last_line : column + last_line;
}

// The families the operators fall into for the purpose of reading an
// expression. `op_tightness` puts every operator into a single order, but a
// reader only reliably knows part of that order: that arithmetic binds tighter
// than comparison, and comparison tighter than `and`/`or`. Where a shift, a
// `_min_`, or `or` against `and` sits in it is a table lookup rather than
// knowledge, so those groupings are written out with parentheses instead of
// leaning on the order.
typedef enum { OP_ARITH, OP_CONCAT, OP_SHIFT, OP_MINMAX, OP_COMPARE, OP_LOGIC } op_family_e;

PUREFUNC static op_family_e op_family(ast_e op) {
    switch (op) {
    case Concat: return OP_CONCAT;
    case LeftShift:
    case RightShift:
    case UnsignedLeftShift:
    case UnsignedRightShift: return OP_SHIFT;
    case Min:
    case Max: return OP_MINMAX;
    case And:
    case Or:
    case Xor: return OP_LOGIC;
    case Equals:
    case NotEquals:
    case LessThan:
    case LessThanOrEquals:
    case GreaterThan:
    case GreaterThanOrEquals:
    case Compare: return OP_COMPARE;
    default: return OP_ARITH;
    }
}

// Whether an operand of `outer`, written bare, groups the way it reads. The
// relationships that count as ones a reader takes off the page are arithmetic
// within arithmetic, arithmetic within a comparison, a comparison within
// `and`/`or`/`xor`, and the two cases the comments below give.
PUREFUNC static bool grouping_is_obvious(ast_e outer, ast_t *inner) {
    if (!is_binary_operation(inner)) return true; // A prefix `-` groups visibly
    op_family_e outer_family = op_family(outer), inner_family = op_family(inner->tag);
    // A run of one operator groups the same way whichever way it is read, and
    // so needs nothing said about it -- unless it is a run of shifts, where the
    // grouping is the whole meaning, or of comparisons, where `a == b == c`
    // means `(a == b) == c` here and the chain it looks like elsewhere.
    if (outer == inner->tag) return outer_family != OP_SHIFT && outer_family != OP_COMPARE;
    if (outer_family == inner_family) return outer_family == OP_ARITH;
    // Concatenation is not arithmetic, but a comparison is the one place it
    // reads like it: nobody has to be told that `a ++ b == c` compares the
    // joined text. Against arithmetic proper it is as murky as anything else.
    if (outer_family == OP_COMPARE) return inner_family == OP_ARITH || inner_family == OP_CONCAT;
    if (outer_family == OP_LOGIC) return inner_family == OP_COMPARE;
    return false;
}

// Whether this operand is written as part of the expression above it, rather
// than parenthesized off into one of its own. The operator above has to absorb
// it back -- written bare it would otherwise be read as belonging somewhere
// else -- and the grouping that leaves has to be one that can be read.
PUREFUNC static bool is_bare_operand(ast_t *inner, ast_e outer_op, bool on_left) {
    bool absorbed =
        on_left ? absorbs_lhs(outer_op, expr_tightness(inner)) : absorbs_rhs(outer_op, expr_tightness(inner));
    return absorbed && grouping_is_obvious(outer_op, inner);
}

// The operators the two functions below render. `is_binary_operation()` is a
// wider set: it takes in `_min_`/`_max_`, which carry a key (`a _min_.x b`)
// and have a renderer of their own.
PUREFUNC static bool is_binop_case(ast_t *ast) {
    switch (ast->tag) {
    case BINOP_CASES: return true;
    default: return false;
    }
}

// What an expression holds, for the purpose of deciding its spacing.
typedef struct {
    bool has_add; // An operator of the `+` band
    bool has_mul; // An operator of the `*` band
    bool has_div; // A division, which is one of the `*` band as well
} operator_bands_t;

// Which bands an expression's own operators fall into.
//
// Parentheses start a fresh expression with its own answer, so the walk stops
// at any operand that is written with them.
static void scan_operator_bands(ast_t *ast, operator_bands_t *bands) {
    if (!is_binop_case(ast) || is_update_assignment(ast)) return;
    int tightness = op_tightness[ast->tag];
    if (tightness == op_tightness[Plus] || tightness == op_tightness[Concat]) bands->has_add = true;
    else if (tightness >= op_tightness[Multiply]) bands->has_mul = true;
    if (ast->tag == Divide || ast->tag == FloorDivide) bands->has_div = true;

    binary_operands_t operands = BINARY_OPERANDS(ast);
    if (is_binop_case(operands.lhs) && is_bare_operand(operands.lhs, ast->tag, true))
        scan_operator_bands(operands.lhs, bands);
    if (is_binop_case(operands.rhs) && is_bare_operand(operands.rhs, ast->tag, false))
        scan_operator_bands(operands.rhs, bands);
}

// How tightly an operator has to bind to lose its spaces. An expression that
// mixes the bands drops them from its `*` band; one that doesn't keeps them
// everywhere; a subscript drops them from everything (see format_index()).
#define TIGHTEN_MIXED (op_tightness[Multiply])
#define TIGHTEN_NONE (INT_MAX)
#define TIGHTEN_ALL (0)

// Operators that can never be written without spaces, whatever the expression
// around them: a word operator would stop being a word (`(x + y)mod3` is not
// parseable code), and `!=` would run into the `!` suffix to its left, where
// `a!=b` reads as `a!` followed by `= b`.
PUREFUNC static bool always_spaced(const char *op) {
    return is_word_operator(op) || op[0] == '!';
}

// The spaces around a binary operator. `^` never takes them: an exponent sits
// against what it raises, `r^2`, however little else the expression holds.
static Text_t binop_spacing(ast_e op_tag, int tighten_from) {
    const char *op = binop_info[op_tag].operator;
    if (always_spaced(op)) return Text(" ");
    bool tight = op_tag == Power || op_tightness[op_tag] >= tighten_from;
    return tight ? EMPTY_TEXT : Text(" ");
}

// An operand keeps its parentheses unless it is written bare, which takes both
// of the things is_bare_operand() asks. `dt / (d2 * x)` is not `dt / d2 * x`,
// and `(2 ^ 3) ^ 2` is not `2 ^ 3 ^ 2`, but `2 ^ (3 ^ 2)` and `2 ^ 3 ^ 2` are
// the same. An `if`/`match` keeps them whatever the tightness: written bare it
// runs on through the rest of the operator.
static Text_t operand(Text_t code, ast_t *ast, ast_e outer_op, bool on_left, Text_t indent, int tighten_from) {
    if (ast->tag == If || ast->tag == Match || (is_operation(ast) && !is_bare_operand(ast, outer_op, on_left)))
        return parenthesize(code, indent);
    // Spaces are a claim about grouping, so an operand that keeps its own
    // inside an operator that has given them up contradicts the operator it
    // belongs to: `a mod b/c` divides the modulus but reads as the modulus of
    // a quotient. A word operator can't give its spaces up -- it would stop
    // being a word -- so it takes parentheses instead, and `(a mod b)/c` says
    // what the spacing was trying to.
    if (is_binary_operation(ast) && binop_spacing(outer_op, tighten_from).length == 0
        && binop_spacing(ast->tag, tighten_from).length > 0)
        return parenthesize(code, indent);
    return code;
}

// The spacing an expression's own operators call for, before anything the
// surrounding syntax has to say about it.
//
// The `*` band loses its spaces when the expression mixes it with the `+`
// band, so that the operator the expression actually splits on is the one the
// eye lands on: `x + y*z`. Only the `+` band triggers that, not merely
// anything looser: `a == b * c` and `2 ^ 3 ^ 2 == 512` keep their spaces,
// which is both what gofmt does with the same shapes and how they are written
// in this tree.
//
// A division loses them whatever it sits among, because a quotient is written
// as one quantity everywhere else it is written at all: `1/3` is a number to
// read, `1 / 3` is a sum to work out. That takes the whole band with it,
// spaces being a claim about grouping: `a*b/c` divides the product, and
// `a * b/c` would say it multiplies the quotient.
PUREFUNC static int expression_spacing(ast_t *ast) {
    operator_bands_t bands = {};
    scan_operator_bands(ast, &bands);
    bool tighten = bands.has_div || (bands.has_add && bands.has_mul);
    return tighten ? TIGHTEN_MIXED : TIGHTEN_NONE;
}

// Whether this operand is part of the same expression as the operator above
// it, rather than a parenthesized one of its own: only then does it inherit
// that expression's spacing.
PUREFUNC static bool shares_expression(ast_t *operand_ast, ast_e outer_op, bool on_left) {
    if (!is_binop_case(operand_ast)) return false;
    return is_bare_operand(operand_ast, outer_op, on_left);
}

// One operator of an expression whose spacing has already been settled by
// mixes_operator_bands() at its outermost operator. An operand belonging to
// the same expression is rendered here too, so that it inherits that answer;
// anything else -- a parenthesized operand above all -- goes back through the
// ordinary path and is answered afresh.
static OptionalText_t format_binop_inline(ast_t *ast, Table_t comments, int tighten_from) {
    binary_operands_t operands = BINARY_OPERANDS(ast);
    const char *op = binop_info[ast->tag].operator;

    Text_t lhs = shares_expression(operands.lhs, ast->tag, true)
                     ? must(format_binop_inline(operands.lhs, comments, tighten_from))
                     : fmt_inline(operands.lhs, comments);
    Text_t rhs = shares_expression(operands.rhs, ast->tag, false)
                     ? must(format_binop_inline(operands.rhs, comments, tighten_from))
                     : fmt_inline(operands.rhs, comments);

    if (is_update_assignment(ast)) return Texts(lhs, " ", Text$from_str(op), " ", rhs);

    // See operand() above for which operands keep their parentheses.
    lhs = operand(lhs, operands.lhs, ast->tag, true, EMPTY_TEXT, tighten_from);
    rhs = operand(rhs, operands.rhs, ast->tag, false, EMPTY_TEXT, tighten_from);

    Text_t space = binop_spacing(ast->tag, tighten_from);
    return Texts(lhs, space, Text$from_str(op), space, rhs);
}

static Text_t format_binop(ast_t *ast, Table_t comments, Text_t indent, int64_t column, int tighten_from) {
    binary_operands_t operands = BINARY_OPERANDS(ast);
    const char *op = binop_info[ast->tag].operator;
    Text_t inner_indent = Texts(indent, single_indent);

    // The three gaps this expression is answerable for: before its left
    // operand, between the two, and after its right one. The outer two are
    // empty unless the source parenthesized it, since only then does its span
    // reach past its operands.
    const char *pos = ast->start;
    Text_t opening = comment_range(&pos, operands.lhs->start, inner_indent, comments);
    pos = operands.lhs->end;
    Text_t middle = comment_range(&pos, operands.rhs->start, inner_indent, comments);
    pos = operands.rhs->end;
    Text_t closing = comment_range(&pos, ast->end, inner_indent, comments);
    bool parenthesized = opening.length > 0 || closing.length > 0;
    Text_t operand_indent = parenthesized ? inner_indent : indent;
    // Parenthesized, the operands start on a line of their own one level in.
    int64_t lhs_column = parenthesized ? inner_indent.length : column;

    Text_t lhs = shares_expression(operands.lhs, ast->tag, true)
                     ? format_binop(operands.lhs, comments, operand_indent, lhs_column, tighten_from)
                     : fmt_at(operands.lhs, comments, operand_indent, lhs_column);
    // The operator and the spaces around it sit between the two operands.
    int64_t rhs_column =
        column_after(lhs_column, lhs) + (int64_t)strlen(op) + 2 * binop_spacing(ast->tag, tighten_from).length;
    Text_t rhs = shares_expression(operands.rhs, ast->tag, false)
                     ? format_binop(operands.rhs, comments, operand_indent, rhs_column, tighten_from)
                     : fmt_at(operands.rhs, comments, operand_indent, rhs_column);

    if (is_update_assignment(ast)) return Texts(lhs, " ", Text$from_str(op), " ", rhs);

    // See format_binop_inline() above for which operands keep their parentheses.
    lhs = operand(lhs, operands.lhs, ast->tag, true, operand_indent, tighten_from);
    rhs = operand(rhs, operands.rhs, ast->tag, false, operand_indent, tighten_from);

    Text_t space = binop_spacing(ast->tag, tighten_from);
    Text_t code = Texts(lhs, space, Text$from_str(op));
    if (middle.length > 0) {
        // A comment written by the operator stays by it. It can only follow
        // the operator, never precede it: `a # c` and then `+ b` on the next
        // line is not an expression the parser puts back together.
        code = Texts(code, " ", middle, "\n", parenthesized ? inner_indent : Texts(indent, single_indent), rhs);
    } else {
        code = Texts(code, space, rhs);
    }
    if (closing.length > 0) code = Texts(code, " ", closing);
    if (!parenthesized) return code;
    if (opening.length > 0) code = Texts(opening, "\n", inner_indent, code);
    return Texts("(\n", inner_indent, code, "\n", indent, ")");
}

// A subscript is written compactly, `arr[i+1]` rather than `arr[i + 1]`: the
// brackets already say where it begins and ends, so the spaces inside them buy
// nothing. Anything that isn't a run of operators is written as it would be
// anywhere else, so `arr[foo(a + b)]` keeps the call's own spacing.
static OptionalText_t format_subscript_inline(ast_t *ast, Table_t comments) {
    if (is_binop_case(ast)) return format_binop_inline(ast, comments, TIGHTEN_ALL);
    return format_inline_code(ast, comments);
}

static Text_t format_subscript(ast_t *ast, Table_t comments, Text_t indent, int64_t column) {
    if (is_binop_case(ast)) return format_binop(ast, comments, indent, column, TIGHTEN_ALL);
    return format_code(ast, comments, indent);
}

// Comments written in a gap between the parts of a construct -- before an
// `else`, before a `case` -- have no line of their own to sit at the end of,
// so they take one at the construct's own level. Nothing else writes them out:
// a block scans between its statements, but these gaps are between the clauses
// of one statement, inside its span.
static Text_t gap_comments(const char **pos, const char *end, Text_t indent, Table_t comments) {
    Text_t found = comment_range(pos, end, indent, comments);
    return found.length > 0 ? Texts("\n", indent, found) : EMPTY_TEXT;
}

// A negation whose operand is a numeric literal or another negation always
// parenthesizes it: `-(1)`, `-(-1)`, `-(-(1))`. Written without the
// parentheses, `- 1` and `- -1` read back as a single negative literal rather
// than the negation of one, which is a different syntax tree.
PUREFUNC static bool negation_needs_parens(ast_t *operand) {
    return operand->tag == Int || operand->tag == Num || operand->tag == Negative;
}

OptionalText_t format_inline_code(ast_t *ast, Table_t comments) {
    if (range_has_comment(ast->start, ast->end, comments)) return NONE_TEXT;
    // A block literal stays a block. Its one-line form is a different thing to
    // read: the newlines come back as `\n` escapes, and the line it lands on is
    // as long as the whole text.
    if ((ast->tag == TextJoin || ast->tag == InlineCCode) && is_block_text(ast)) return NONE_TEXT;
    switch (ast->tag) {
    /*inline*/ case Unknown:
        fail("Invalid AST");
    /*inline*/ case Block: {
        ast_list_t *statements = Match(ast, Block)->statements;
        if (statements == NULL) return Text("pass");
        else if (statements->next == NULL) return fmt_inline(statements->ast, comments);
        else return NONE_TEXT;
    }
    /*inline*/ case StructDef:
    /*inline*/ case EnumDef:
    /*inline*/ case LangDef:
    /*inline*/ case FunctionDef:
    /*inline*/ case ConvertDef:
    /*inline*/ case DebugLog:
        return NONE_TEXT;
    /*inline*/ case Assert: {
        DeclareMatch(assert, ast, Assert);
        Text_t expr = must(bounded_inline(assert->expr, comments));
        if (!assert->message) return Texts("assert ", expr);
        Text_t message = fmt_inline(assert->message, comments);
        return Texts("assert ", expr, ", ", message);
    }
    /*inline*/ case Test:
        // A test is a label plus an indented body: never a single line.
        return NONE_TEXT;
    /*inline*/ case Defer:
        return Texts("defer ", fmt_inline(Match(ast, Defer)->body, comments));
    /*inline*/ case Lambda: {
        DeclareMatch(lambda, ast, Lambda);
        Text_t code = Texts("func(", format_inline_args(lambda->args, comments));
        if (lambda->ret_type)
            code = Texts(code, lambda->args ? Text(" -> ") : Text("-> "), format_type(lambda->ret_type));
        code = Texts(code, ") ", fmt_inline(lambda->body, comments));
        return Texts(code);
    }
    /*inline*/ case If: {
        DeclareMatch(if_, ast, If);

        Text_t if_condition = if_->condition->tag == Not
                                  ? Texts("unless ", fmt_inline(Match(if_->condition, Not)->value, comments))
                                  : Texts("if ", fmt_inline(if_->condition, comments));

        if (if_->postfix && if_->else_body == NULL) {
            return Texts(fmt_inline(if_->body, comments), " if ", if_condition);
        }

        if (if_->else_body == NULL && if_->condition->tag != Declare) {
            ast_t *stmt = unwrap_block(if_->body);
            if (!stmt) return Texts("pass ", if_condition);
            switch (stmt->tag) {
            case Return:
            case Continue:
            case Break: return Texts(fmt_inline(stmt, comments), " ", if_condition);
            default: break;
            }
        }

        Text_t code = Texts(if_condition, " then ", fmt_inline(if_->body, comments));
        if (if_->else_body) code = Texts(code, " else ", fmt_inline(if_->else_body, comments));
        return code;
    }
    /*inline*/ case Match: {
        DeclareMatch(match, ast, Match);
        Text_t code = Texts("match ", fmt_inline(match->subject, comments));
        for (match_clause_t *clause = match->clauses; clause; clause = clause->next) {
            code = Texts(code, " case ", fmt_inline(clause->pattern, comments));
            while (clause->next && clause->next->body == clause->body) {
                clause = clause->next;
                code = Texts(code, ", ", fmt_inline(clause->pattern, comments));
            }
            code = Texts(code, " then ", fmt_inline(clause->body, comments));
        }
        if (match->else_body) code = Texts(code, " else ", fmt_inline(match->else_body, comments));
        return code;
    }
    /*inline*/ case Repeat:
        return Texts("repeat ", fmt_inline(Match(ast, Repeat)->body, comments));
    /*inline*/ case While: {
        DeclareMatch(loop, ast, While);
        return Texts("while ", fmt_inline(loop->condition, comments), " do ", fmt_inline(loop->body, comments));
    }
    /*inline*/ case For: {
        DeclareMatch(loop, ast, For);
        Text_t code = Text("for ");
        for (ast_list_t *var = loop->vars; var; var = var->next) {
            code = Texts(code, fmt_inline(var->ast, comments));
            if (var->next) code = Texts(code, ", ");
        }
        if (loop->at) code = Texts(code, " at ", fmt_inline(loop->at, comments));
        code = Texts(code, " in ");
        for (ast_list_t *iter = loop->iters; iter; iter = iter->next) {
            code = Texts(code, fmt_inline(iter->ast, comments));
            if (iter->next) code = Texts(code, ", ");
        }
        code = Texts(code, " do ", fmt_inline(loop->body, comments));
        if (loop->empty) code = Texts(code, " else ", fmt_inline(loop->empty, comments));
        return code;
    }
    /*inline*/ case Comprehension: {
        DeclareMatch(comp, ast, Comprehension);
        Text_t code = Texts(fmt_inline(comp->expr, comments), " for ");
        for (ast_list_t *var = comp->vars; var; var = var->next) {
            code = Texts(code, fmt_inline(var->ast, comments));
            if (var->next) code = Texts(code, ", ");
        }
        if (comp->at) code = Texts(code, " at ", fmt_inline(comp->at, comments));
        code = Texts(code, " in ");
        for (ast_list_t *iter = comp->iters; iter; iter = iter->next) {
            code = Texts(code, fmt_inline(iter->ast, comments));
            if (iter->next) code = Texts(code, ", ");
        }
        if (comp->filter) code = Texts(code, " if ", fmt_inline(comp->filter, comments));
        return code;
    }
    /*inline*/ case List: {
        ast_list_t *items = Match(ast, List)->items;
        Text_t code = EMPTY_TEXT;
        for (ast_list_t *item = items; item; item = item->next) {
            code = Texts(code, fmt_inline(item->ast, comments));
            if (item->next) code = Texts(code, ", ");
        }
        return Texts("[", code, "]");
    }
    /*inline*/ case Table: {
        DeclareMatch(table, ast, Table);
        Text_t code = EMPTY_TEXT;
        for (ast_list_t *entry = table->entries; entry; entry = entry->next) {
            code = Texts(code, fmt_inline(entry->ast, comments));
            if (entry->next) code = Texts(code, ", ");
        }
        if (table->fallback) code = Texts(code, "; fallback=", fmt_inline(table->fallback, comments));
        if (table->default_value) code = Texts(code, "; default=", fmt_inline(table->default_value, comments));
        return Texts("{", code, "}");
    }
    /*inline*/ case TableEntry: {
        DeclareMatch(entry, ast, TableEntry);
        if (entry->value)
            return Texts(must(bounded_inline(entry->key, comments)), ": ",
                         must(bounded_inline(entry->value, comments)));
        else return Texts(fmt_inline(entry->key, comments));
    }
    /*inline*/ case Declare: {
        DeclareMatch(decl, ast, Declare);
        Text_t code = fmt_inline(decl->var, comments);
        if (decl->type) code = Texts(code, " : ", format_type(decl->type));
        if (decl->value) code = Texts(code, decl->type ? Text(" = ") : Text(" := "), fmt_inline(decl->value, comments));
        return code;
    }
    /*inline*/ case Assign: {
        DeclareMatch(assign, ast, Assign);
        Text_t code = EMPTY_TEXT;
        for (ast_list_t *target = assign->targets; target; target = target->next) {
            code = Texts(code, fmt_inline(target->ast, comments));
            if (target->next) code = Texts(code, ", ");
        }
        code = Texts(code, " = ");
        for (ast_list_t *value = assign->values; value; value = value->next) {
            code = Texts(code, fmt_inline(value->ast, comments));
            if (value->next) code = Texts(code, ", ");
        }
        return code;
    }
    /*inline*/ case Pass:
        return Text("pass");
    /*inline*/ case Return: {
        ast_t *value = Match(ast, Return)->value;
        return value ? Texts("return ", must(bounded_inline(value, comments))) : Text("return");
    }
    /*inline*/ case Not: {
        ast_t *val = Match(ast, Not)->value;
        return Texts("not ", must(termify_inline(val, comments)));
    }
    /*inline*/ case Negative: {
        ast_t *val = Match(ast, Negative)->value;
        if (negation_needs_parens(val)) return Texts("-(", fmt_inline(val, comments), ")");
        // An operand this `-` absorbs needs no parentheses to be read back as
        // part of it: `-x ^ 2` is already the negation of the power.
        if (is_binary_operation(val) && absorbs_rhs(Negative, expr_tightness(val)))
            return Texts("-", fmt_inline(val, comments));
        return Texts("-", must(termify_inline(val, comments)));
    }
    /*inline*/ case HeapAllocate: {
        ast_t *val = Match(ast, HeapAllocate)->value;
        if (has_nonoptional_suffix(val)) return Texts("@(", must(format_inline_code(val, comments)), ")");
        return Texts("@", must(termify_inline(val, comments)));
    }
    /*inline*/ case StackReference: {
        ast_t *val = Match(ast, StackReference)->value;
        if (has_nonoptional_suffix(val)) return Texts("&(", must(format_inline_code(val, comments)), ")");
        return Texts("&", must(termify_inline(val, comments)));
    }
    /*inline*/ case NonOptional: {
        ast_t *val = Match(ast, NonOptional)->value;
        return Texts(must(termify_inline(val, comments)), "!");
    }
    /*inline*/ case FieldAccess: {
        DeclareMatch(access, ast, FieldAccess);
        return Texts(must(termify_inline(access->fielded, comments)), ".", Text$from_str(access->field));
    }
    /*inline*/ case Index: {
        DeclareMatch(index, ast, Index);
        Text_t indexed = must(termify_inline(index->indexed, comments));
        if (index->index) return Texts(indexed, "[", must(format_subscript_inline(index->index, comments)), "]");
        else return Texts(indexed, "[]");
    }
    /*inline*/ case TextJoin: {
        text_opts_t opts = choose_text_options(Match(ast, TextJoin)->children);
        Text_t ret = must(format_inline_text(opts, Match(ast, TextJoin)->children, comments));
        type_ast_t *lang = Match(ast, TextJoin)->lang;
        return lang ? Texts("$", format_type(lang), ret) : ret;
    }
    /*inline*/ case InlineCCode: {
        DeclareMatch(c_code, ast, InlineCCode);
        Text_t code = c_code->type_ast ? Texts("C_code:", format_type(c_code->type_ast)) : Text("C_code");
        text_opts_t opts = {.quote = Text("`"), .unquote = Text("`"), .interp = Text("@"), .verbatim = true};
        return Texts(code, must(format_inline_text(opts, Match(ast, InlineCCode)->chunks, comments)));
    }
    /*inline*/ case TextLiteral: { fail("Something went wrong, we shouldn't be formatting text literals directly"); }
    /*inline*/ case Path: {
        return Texts("(", Text$escaped(Text$from_str(Match(ast, Path)->path), false, Text("()")), ")");
    }
    /*inline*/ case Embed: { return Texts("_embed_ ", fmt_inline(Match(ast, Embed)->path, comments)); }
    /*inline*/ case Serialize: { return Texts("serialize(", fmt_inline(Match(ast, Serialize)->value, comments), ")"); }
    /*inline*/ case Deserialize: {
        DeclareMatch(deserialize, ast, Deserialize);
        return Texts("deserialize:", format_type(deserialize->type_ast), "(", fmt_inline(deserialize->value, comments),
                     ")");
    }
    /*inline*/ case Break: {
        const char *target = Match(ast, Break)->target;
        return target ? Texts("break ", Text$from_str(target)) : Text("break");
    }
    /*inline*/ case Continue: {
        const char *target = Match(ast, Continue)->target;
        return target ? Texts("continue ", Text$from_str(target)) : Text("continue");
    }
    /*inline*/ case Min:
    /*inline*/ case Max: {
        ast_t *lhs_ast = ast->tag == Min ? Match(ast, Min)->lhs : Match(ast, Max)->lhs;
        ast_t *rhs_ast = ast->tag == Min ? Match(ast, Min)->rhs : Match(ast, Max)->rhs;
        Text_t lhs = operand(fmt_inline(lhs_ast, comments), lhs_ast, ast->tag, true, EMPTY_TEXT, TIGHTEN_NONE);
        Text_t rhs = operand(fmt_inline(rhs_ast, comments), rhs_ast, ast->tag, false, EMPTY_TEXT, TIGHTEN_NONE);
        ast_t *key = ast->tag == Min ? Match(ast, Min)->key : Match(ast, Max)->key;
        // The keyed form (`a _min_.x b`) needs its spaces just as much as the
        // plain one; without them it ran together as `a_min_.xb`.
        Text_t op = key ? fmt_inline(key, comments) : (ast->tag == Min ? Text("_min_") : Text("_max_"));
        return Texts(lhs, " ", op, " ", rhs);
    }
    /*inline*/ case Reduction: {
        DeclareMatch(reduction, ast, Reduction);
        if (reduction->key) {
            return Texts("(", fmt_inline(reduction->key, comments), ": ", fmt_inline(reduction->iter, comments), ")");
        } else {
            return Texts("(", Text$from_str(binop_info[reduction->op].operator), ": ",
                         fmt_inline(reduction->iter, comments), ")");
        }
    }
    /*inline*/ case None:
        return Text("none");
    /*inline*/ case Bool:
        return Match(ast, Bool)->b ? Text("yes") : Text("no");
    /*inline*/ case Int: {
        // The literal as written, not the node's source text: a parenthesized
        // literal's span covers the parentheses, which are not part of it.
        const char *str = Match(ast, Int)->str;
        return str ? Text$from_str(str) : Int$value_as_text(Match(ast, Int)->i);
    }
    /*inline*/ case Num: {
        const char *str = Match(ast, Num)->str;
        return str ? Text$from_str(str) : Text$from_str(number_to_symbolic(Match(ast, Num)->n));
    }
    /*inline*/ case Var:
        return Text$from_str(Match(ast, Var)->name);
    /*inline*/ case FunctionCall: {
        DeclareMatch(call, ast, FunctionCall);
        // The function being called is a suffix's receiver like any other, so
        // it goes through termify_inline(): `(if c then f else g)(x)` printed
        // bare came back as `if c then f else g(x)`.
        return Texts(must(termify_inline(call->fn, comments)), "(", must(format_inline_args(call->args, comments)),
                     ")");
    }
    /*inline*/ case RecordLiteral: {
        DeclareMatch(record, ast, RecordLiteral);
        return Texts(fmt_inline(record->type, comments), "{", must(format_inline_args(record->args, comments)), "}");
    }
    /*inline*/ case MethodCall: {
        DeclareMatch(call, ast, MethodCall);
        // termify_inline(), the same as every other suffix takes its receiver
        // through and the same as the multi-line case below: a hand-rolled
        // list here went stale, and left `(if c then a else b).f()` and
        // `(@x).f()` printing as `if c then a else b.f()` and `@x.f()`.
        Text_t self = must(termify_inline(call->self, comments));
        return Texts(self, ".", Text$from_str(call->name), "(", must(format_inline_args(call->args, comments)), ")");
    }
    /*inline*/ case BINOP_CASES:
        // The spacing is the whole expression's answer, so it is settled here,
        // at the operator the expression splits on, and handed down.
        return format_binop_inline(ast, comments, expression_spacing(ast));
    /*inline*/ case Use: {
        DeclareMatch(use, ast, Use);
        // `name := use ./module.tm` binds the module to a variable; dropping
        // that half left every reference to the module unresolvable.
        Text_t code = Texts("use ", use->path);
        return use->var ? Texts(fmt_inline(use->var, comments), " := ", code) : code;
    }
    /*inline*/ case ExplicitlyTyped:
        fail("Explicitly typed AST nodes are only meant to be used internally.");
    default: {
        fail("Formatting not implemented for: ", ast_to_sexp(ast));
    }
    }
}

Text_t format_code_at(ast_t *ast, Table_t comments, Text_t indent, int64_t column) {
    OptionalText_t inlined = format_inline_code(ast, comments);
    bool inlined_fits = (inlined.tag != TEXT_NONE && column + inlined.length <= MAX_WIDTH);

    switch (ast->tag) {
    /*multiline*/ case Unknown:
        fail("Invalid AST");
    /*multiline*/ case Block: {
        // A statement whose formatted form nests two levels deep needs a blank
        // line after it, so that the code following it isn't mistaken for part
        // of the inner body:
        Text_t double_indent = Texts("\n", indent, single_indent, single_indent);
        Text_t code = EMPTY_TEXT;
        const char *comment_pos = ast->start;
        ast_list_t *prev = NULL;
        bool prev_was_double_indented = false;
        for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
            Text_t comment_code = comment_range(&comment_pos, stmt->ast->start, indent, comments);
            int64_t target_newlines =
                prev == NULL ? 0
                             : 1 + MAX(prev_was_double_indented ? 1 : 0, suggested_blank_lines(prev->ast, stmt->ast));

            int64_t newlines = 0;
            for (int64_t i = code.length - 1; i >= 0; i--) {
                if (Text$get_grapheme(code, i) != '\n') break;
                newlines += 1;
            }
            for (; newlines < target_newlines; newlines++)
                code = Text$concat(code, Text("\n"));

            if (comment_code.length > 0) {
                if (code.length > 0 && !Text$ends_with(code, indent, NULL)) code = Text$concat(code, indent);
                code = Text$concat(code, comment_code, Text("\n"));
            }

            if (code.length > 0 && !Text$ends_with(code, indent, NULL)) code = Text$concat(code, indent);
            Text_t stmt_code;
            if (stmt->ast->tag == Block) {
                stmt_code =
                    Texts("do\n", indent, single_indent, fmt(stmt->ast, comments, Texts(indent, single_indent)));
            } else {
                stmt_code = fmt(stmt->ast, comments, indent);
            }
            code = Text$concat(code, stmt_code);
            prev_was_double_indented = Text$has(stmt_code, double_indent);
            comment_pos = stmt->ast->end;
            const char *eol = stmt->ast->end;
            while (eol < stmt->ast->file->text + stmt->ast->file->len && *eol != '\n')
                eol++;
            // A comment on the line this statement ends on. The cursor moves
            // past it either way, so that the next statement doesn't collect
            // it as one of its own, but a statement that ends inside an
            // indented block has already written it out on that block's last
            // line: this is the same line, seen from one level further out,
            // and claiming it again is what made `>> 1 # c` grow another
            // `# c` on every pass.
            Text_t line_comment = comment_range(&comment_pos, eol, indent, comments);
            if (line_comment.length > 0 && !ends_deeper_than(stmt_code, indent)) {
                code = Text$concat(code, Text(" "), line_comment);
            }
            prev = stmt;
        }

        Text_t comment_code = comment_range(&comment_pos, ast->end, indent, comments);
        if (comment_code.length > 0) {
            if (code.length > 0) code = Text$concat(code, Text("\n"), indent);
            code = Text$concat(code, comment_code);
        }
        return code;
    }
    /*multiline*/ case If: {
        DeclareMatch(if_, ast, If);
        Text_t code =
            if_->condition->tag == Not
                ? Texts("unless ", bounded_at(Match(if_->condition, Not)->value, comments, indent, column + 7))
                : Texts("if ", bounded_at(if_->condition, comments, indent, column + 3));

        Text_t body = fmt(if_->body, comments, Texts(indent, single_indent));
        if (if_->postfix && if_->else_body == NULL && !Text$has(body, Text("\n"))) {
            return Texts(body, " ", code);
        }

        code = Texts(code, "\n", indent, single_indent, body);
        if (if_->else_body) {
            const char *gap = if_->body->end;
            code = Texts(code, gap_comments(&gap, if_->else_body->start, indent, comments));
            if (if_->else_body->tag != If) {
                code = Texts(code, "\n", indent, "else\n", indent, single_indent,
                             fmt(if_->else_body, comments, Texts(indent, single_indent)));
            } else {
                code = Texts(code, "\n", indent, "else ", fmt_at(if_->else_body, comments, indent, indent.length + 5));
            }
        }
        return code;
    }
    /*multiline*/ case Match: {
        DeclareMatch(match, ast, Match);
        Text_t code = Texts("match ", bounded_at(match->subject, comments, indent, column + 6));
        // A comment after the subject or before a `case` sits in a gap of this
        // statement, which no block scans:
        const char *gap = match->subject->end;
        for (match_clause_t *clause = match->clauses; clause; clause = clause->next) {
            code = Texts(code, gap_comments(&gap, clause->pattern->start, indent, comments));
            code = Texts(code, "\n", indent, "case ", fmt_at(clause->pattern, comments, indent, indent.length + 5));
            while (clause->next && clause->next->body == clause->body) {
                clause = clause->next;
                code = Texts(code, ", ", fmt_at(clause->pattern, comments, indent, column_after(column, code) + 2));
            }
            code = Texts(code, format_namespace(clause->body, comments, indent));
            gap = clause->body->end;
        }
        if (match->else_body) {
            code = Texts(code, gap_comments(&gap, match->else_body->start, indent, comments));
            code = Texts(code, "\n", indent, "else", format_namespace(match->else_body, comments, indent));
        }
        return code;
    }
    /*multiline*/ case Repeat: {
        return Texts("repeat\n", indent, single_indent,
                     fmt(Match(ast, Repeat)->body, comments, Texts(indent, single_indent)));
    }
    /*multiline*/ case While: {
        DeclareMatch(loop, ast, While);
        return Texts("while ", bounded_at(loop->condition, comments, indent, column + 6), "\n", indent, single_indent,
                     fmt(loop->body, comments, Texts(indent, single_indent)));
    }
    /*multiline*/ case For: {
        DeclareMatch(loop, ast, For);
        Text_t code = Text("for ");
        for (ast_list_t *var = loop->vars; var; var = var->next) {
            code = Texts(code, fmt_at(var->ast, comments, indent, column_after(column, code)));
            if (var->next) code = Texts(code, ", ");
        }
        if (loop->at)
            code = Texts(code, " at ", bounded_at(loop->at, comments, indent, column_after(column, code) + 4));
        code = Texts(code, " in ");
        for (ast_list_t *iter = loop->iters; iter; iter = iter->next) {
            code = Texts(code, bounded_at(iter->ast, comments, indent, column_after(column, code)));
            if (iter->next) code = Texts(code, ", ");
        }
        code = Texts(code, format_namespace(loop->body, comments, indent));
        if (loop->empty) code = Texts(code, "\n", indent, "else", format_namespace(loop->empty, comments, indent));
        return code;
    }
    /*multiline*/ case Comprehension: {
        if (inlined_fits) return inlined;
        DeclareMatch(comp, ast, Comprehension);
        // An expression that spans lines can't share the opening parenthesis's
        // line: constructs like `match` need their continuation lines at their
        // own starting column, which is one past `indent` there. Give it a line
        // of its own instead.
        Text_t inner_indent = Texts(indent, single_indent);
        Text_t block_expr = fmt(comp->expr, comments, inner_indent);
        bool block_layout = Text$has(block_expr, Text("\n"));
        Text_t body_indent = block_layout ? inner_indent : indent;

        // A comment written between the expression and its `for` belongs to
        // this gap and to nothing else; it goes at the end of the expression's
        // line, which the `for` then starts a new one after.
        const char *gap = comp->expr->end;
        Text_t before_for = comment_range(&gap, comp->vars ? comp->vars->ast->start : ast->end, body_indent, comments);

        Text_t code;
        if (block_layout) {
            code = Texts("(\n", inner_indent, block_expr, before_for.length > 0 ? Texts(" ", before_for) : EMPTY_TEXT,
                         "\n", inner_indent, "for ");
        } else {
            code = Texts("(", fmt_at(comp->expr, comments, indent, column + 1));
            if (before_for.length > 0) code = Texts(code, " ", before_for, "\n", indent, "for ");
            else if (code.length >= MAX_WIDTH) code = Texts(code, "\n", indent, "for ");
            else code = Texts(code, " for ");
        }

        for (ast_list_t *var = comp->vars; var; var = var->next) {
            code = Texts(code, fmt_at(var->ast, comments, body_indent, column_after(column, code)));
            if (var->next) code = Texts(code, ", ");
        }
        if (comp->at)
            code = Texts(code, " at ", bounded_at(comp->at, comments, body_indent, column_after(column, code) + 4));

        code = Texts(code, " in ");
        for (ast_list_t *iter = comp->iters; iter; iter = iter->next) {
            code = Texts(code, bounded_at(iter->ast, comments, body_indent, column_after(column, code)));
            if (iter->next) code = Texts(code, ", ");
        }

        if (comp->filter) {
            if (block_layout) code = Texts(code, "\n", inner_indent, "if ");
            else if (code.length >= MAX_WIDTH) code = Texts(code, "\n", indent, "if ");
            else code = Texts(code, " if ");
            code = Texts(code, bounded_at(comp->filter, comments, body_indent, column_after(column, code)));
        }
        // The closing parenthesis was missing entirely: a comprehension that
        // didn't fit on one line came out unparseable.
        return Texts(code, block_layout ? Texts("\n", indent, ")") : Text(")"));
    }
    /*multiline*/ case FunctionDef: {
        DeclareMatch(func, ast, FunctionDef);
        Text_t code = Texts("func ", fmt_at(func->name, comments, indent, column + 5));
        code = Texts(code, format_signature(func->args, func->ret_type, func->cache, func->is_inline, comments, indent,
                                            column_after(column, code)));
        code = Texts(code, "\n", indent, single_indent, fmt(func->body, comments, Texts(indent, single_indent)));
        return Texts(code);
    }
    /*multiline*/ case Lambda: {
        if (inlined_fits) return inlined;
        DeclareMatch(lambda, ast, Lambda);
        Text_t code =
            Texts("func", format_signature(lambda->args, lambda->ret_type, NULL, false, comments, indent, column + 4));
        code = Texts(code, "\n", indent, single_indent, fmt(lambda->body, comments, Texts(indent, single_indent)));
        return Texts(code);
    }
    /*multiline*/ case ConvertDef: {
        DeclareMatch(convert, ast, ConvertDef);
        Text_t code = Texts("convert ", format_signature(convert->args, convert->ret_type, convert->cache,
                                                         convert->is_inline, comments, indent, column + 8));
        code = Texts(code, "\n", indent, single_indent, fmt(convert->body, comments, Texts(indent, single_indent)));
        return Texts(code);
    }
    /*multiline*/ case StructDef: {
        DeclareMatch(def, ast, StructDef);
        flag_list_t flags = {};
        add_flag(&flags, def->secret, Text("secret"));
        add_flag(&flags, def->packed_bools, Text("packed_bools"));
        add_flag(&flags, def->external, Text("external"));
        add_flag(&flags, def->opaque, Text("opaque"));
        Text_t code = Texts("struct ", Text$from_str(def->name));
        code = Texts(code, format_bracketed_args(def->fields, EMPTY_TEXT, flags, comments, indent,
                                                 column_after(column, code), "{", "}"));
        // Comments inside the field list are emitted with their field, so pick
        // up only what comes after it:
        const char *comment_pos = ast->start;
        for (arg_ast_t *field = def->fields; field; field = field->next)
            comment_pos = field->end;
        Text_t comment_code =
            comment_range(&comment_pos, def->namespace->start, Texts(indent, single_indent), comments);
        if (comment_code.length > 0) code = Texts(code, "\n", indent, single_indent, comment_code);
        return Texts(code, format_namespace(def->namespace, comments, indent));
    }
    /*multiline*/ case EnumDef: {
        DeclareMatch(def, ast, EnumDef);
        Text_t code = Texts("enum ", Text$from_str(def->name), "(");
        Text_t tags = format_tags_at(def->tags, comments, indent, column_after(column, code) + 1);
        code = Texts(code, tags, Text$has(tags, Text("\n")) ? Texts("\n", indent, ")") : Text(")"));
        // As in StructDef: a variant's field comments travel with the field.
        const char *comment_pos = ast->start;
        for (tag_ast_t *tag = def->tags; tag; tag = tag->next)
            for (arg_ast_t *field = tag->fields; field; field = field->next)
                comment_pos = field->end;
        Text_t comment_code =
            comment_range(&comment_pos, def->namespace->start, Texts(indent, single_indent), comments);
        if (comment_code.length > 0) code = Texts(code, "\n", indent, single_indent, comment_code);
        return Texts(code, format_namespace(def->namespace, comments, indent));
    }
    /*multiline*/ case LangDef: {
        DeclareMatch(def, ast, LangDef);
        Text_t code = Texts("lang ", Text$from_str(def->name));
        const char *comment_pos = ast->start;
        Text_t comment_code =
            comment_range(&comment_pos, def->namespace->start, Texts(indent, single_indent), comments);
        if (comment_code.length > 0) code = Texts(code, "\n", indent, single_indent, comment_code);
        return Texts(code, format_namespace(def->namespace, comments, indent));
    }
    /*multiline*/ case Defer:
        return Texts("defer", format_namespace(Match(ast, Defer)->body, comments, indent));
    /*multiline*/ case List: {
        if (inlined_fits) return inlined;
        ast_list_t *items = Match(ast, List)->items;
        Text_t code = Text("[");
        const char *comment_pos = ast->start;
        ast_t *prev = NULL;
        for (ast_list_t *item = items; item; item = item->next) {
            Text_t item_comments =
                comment_range(&comment_pos, item->ast->start, Texts(indent, single_indent), comments);
            // On a line of its own: concatenated where the last item stopped,
            // it ran onto the back of that item's comma.
            if (item_comments.length > 0) add_line(&code, item_comments, Texts(indent, single_indent));
            Text_t item_text = bounded(item->ast, comments, Texts(indent, single_indent));
            // An item that trails off into an indented block (a lambda's body)
            // has no line of its own left to end with a comma: the comma would
            // read as part of that block, so the newline separates instead,
            // which is what format_args() does with the same shape.
            Text_t comma = ends_deeper_than(item_text, Texts(indent, single_indent)) ? EMPTY_TEXT : Text(",");
            if (Text$ends_with(code, Text(","), NULL) && prev
                && get_line_number(prev->file, prev->end) == get_line_number(item->ast->file, item->ast->start)) {
                if (!Text$has(item_text, Text("\n")) && trailing_line_len(code) + 1 + item_text.length + 1 <= MAX_WIDTH)
                    code = Texts(code, " ", item_text, comma);
                else code = Texts(code, "\n", indent, single_indent, item_text, comma);
            } else {
                add_line(&code, Texts(item_text, comma), Texts(indent, single_indent));
            }
            // Past the item, not merely up to where it started: what is
            // written inside it is the item's own to place, and scanning it
            // again here would write it a second time.
            comment_pos = item->ast->end;
            prev = item->ast;
        }
        // A comment left over at the end goes on a line of its own: appended
        // where the last item stopped, it ran onto the back of its comma.
        Text_t trailing = comment_range(&comment_pos, ast->end, Texts(indent, single_indent), comments);
        if (trailing.length > 0) add_line(&code, trailing, Texts(indent, single_indent));
        return Texts(code, "\n", indent, "]");
    }
    /*multiline*/ case Table: {
        if (inlined_fits) return inlined;
        DeclareMatch(table, ast, Table);
        Text_t code = Texts("{");
        const char *comment_pos = ast->start;
        for (ast_list_t *entry = table->entries; entry; entry = entry->next) {
            Text_t entry_comments =
                comment_range(&comment_pos, entry->ast->start, Texts(indent, single_indent), comments);
            if (entry_comments.length > 0) add_line(&code, entry_comments, Texts(indent, single_indent));

            Text_t entry_text = fmt(entry->ast, comments, Texts(indent, single_indent));
            // As with a list item above, an entry that ends inside an indented
            // block is separated by the newline rather than a comma.
            Text_t comma = ends_deeper_than(entry_text, Texts(indent, single_indent)) ? EMPTY_TEXT : Text(",");
            if (Text$ends_with(code, Text(","), NULL)) {
                if (!Text$has(entry_text, Text("\n"))
                    && trailing_line_len(code) + 1 + entry_text.length + 1 <= MAX_WIDTH)
                    code = Texts(code, " ", entry_text, comma);
                else code = Texts(code, "\n", indent, single_indent, entry_text, comma);
            } else {
                add_line(&code, Texts(entry_text, comma), Texts(indent, single_indent));
            }
            // As with a list item above: the entry's interior is its own.
            comment_pos = entry->ast->end;
        }
        // A comment left over at the end goes on a line of its own: appended
        // where the last item stopped, it ran onto the back of its comma.
        Text_t trailing = comment_range(&comment_pos, ast->end, Texts(indent, single_indent), comments);
        if (trailing.length > 0) add_line(&code, trailing, Texts(indent, single_indent));

        if (table->fallback)
            code =
                Texts(code, ";\n", indent, single_indent,
                      "fallback=", fmt_at(table->fallback, comments, indent, indent.length + single_indent.length + 9));

        if (table->default_value)
            code = Texts(code, ";\n", indent, single_indent, "default=",
                         fmt_at(table->default_value, comments, indent, indent.length + single_indent.length + 8));

        return Texts(code, "\n", indent, "}");
    }
    /*multiline*/ case TableEntry: {
        if (inlined_fits) return inlined;
        DeclareMatch(entry, ast, TableEntry);
        if (entry->value) {
            Text_t key = bounded_at(entry->key, comments, indent, column);
            return Texts(key, ": ", bounded_at(entry->value, comments, indent, column_after(column, key) + 2));
        } else return bounded_at(entry->key, comments, indent, column);
    }
    /*multiline*/ case Declare: {
        if (inlined_fits) return inlined;
        DeclareMatch(decl, ast, Declare);
        Text_t code = fmt_at(decl->var, comments, indent, column);
        if (decl->type) code = Texts(code, " : ", format_type(decl->type));
        if (decl->value) {
            Text_t assignment = decl->type ? Text(" = ") : Text(" := ");
            code = Texts(code, assignment,
                         fmt_at(decl->value, comments, indent, column_after(column, code) + assignment.length));
        }
        return code;
    }
    /*multiline*/ case Assign: {
        if (inlined_fits) return inlined;
        DeclareMatch(assign, ast, Assign);
        Text_t code = EMPTY_TEXT;
        for (ast_list_t *target = assign->targets; target; target = target->next) {
            code = Texts(code, fmt_at(target->ast, comments, indent, column_after(column, code)));
            if (target->next) code = Texts(code, ", ");
        }
        code = Texts(code, " = ");
        for (ast_list_t *value = assign->values; value; value = value->next) {
            code = Texts(code, fmt_at(value->ast, comments, indent, column_after(column, code)));
            if (value->next) code = Texts(code, ", ");
        }
        return code;
    }
    /*multiline*/ case Pass:
        return Text("pass");
    /*multiline*/ case Return: {
        if (inlined_fits) return inlined;
        ast_t *value = Match(ast, Return)->value;
        // `return` takes a plain expression, not a block-form `if`/`match`, so
        // one has to keep the parentheses it was written with.
        return value ? Texts("return ", bounded_at(value, comments, indent, column + 7)) : Text("return");
    }
    /*multiline*/ case Not: {
        if (inlined_fits) return inlined;
        ast_t *val = Match(ast, Not)->value;
        // A multi-clause `if`/`match` has to be parenthesized to stay an
        // operand: written bare, its later clauses land outside the `not`,
        // and `not if ...` doesn't parse at all.
        if (is_binary_operation(val) || val->tag == If || val->tag == Match)
            return Texts("not ", termify_at(val, comments, indent, column + 4));
        else return Texts("not ", fmt_at(val, comments, indent, column + 4));
    }
    /*multiline*/ case Negative: {
        if (inlined_fits) return inlined;
        ast_t *val = Match(ast, Negative)->value;
        // See the inline case above for which operands keep their parentheses.
        if (negation_needs_parens(val)) return Texts("-(", fmt_at(val, comments, indent, column + 2), ")");
        // An `if`/`match` keeps its parentheses here for the same reason it
        // does as a binary operand below: spread over several lines, its
        // later clauses would otherwise fall outside the `-`.
        if ((is_binary_operation(val) && !absorbs_rhs(Negative, expr_tightness(val))) || val->tag == If
            || val->tag == Match)
            return Texts("-", termify_at(val, comments, indent, column + 1));
        else return Texts("-", fmt_at(val, comments, indent, column + 1));
    }
    /*multiline*/ case HeapAllocate: {
        if (inlined_fits) return inlined;
        ast_t *val = Match(ast, HeapAllocate)->value;
        if (has_nonoptional_suffix(val)) return Texts("@(", fmt_at(val, comments, indent, column + 2), ")");
        return Texts("@", termify_at(val, comments, indent, column + 1));
    }
    /*multiline*/ case StackReference: {
        if (inlined_fits) return inlined;
        ast_t *val = Match(ast, StackReference)->value;
        if (has_nonoptional_suffix(val)) return Texts("&(", fmt_at(val, comments, indent, column + 2), ")");
        return Texts("&", termify_at(val, comments, indent, column + 1));
    }
    /*multiline*/ case NonOptional: {
        if (inlined_fits) return inlined;
        ast_t *val = Match(ast, NonOptional)->value;
        return Texts(termify_at(val, comments, indent, column), "!");
    }
    /*multiline*/ case FieldAccess: {
        if (inlined_fits) return inlined;
        DeclareMatch(access, ast, FieldAccess);
        return Texts(termify_at(access->fielded, comments, indent, column), ".", Text$from_str(access->field));
    }
    /*multiline*/ case Index: {
        if (inlined_fits) return inlined;
        DeclareMatch(index, ast, Index);
        Text_t indexed = termify_at(index->indexed, comments, indent, column);
        if (index->index)
            return Texts(indexed, "[",
                         format_subscript(index->index, comments, indent, column_after(column, indexed) + 1), "]");
        else return Texts(indexed, "[]");
    }
    /*multiline*/ case TextJoin: {
        if (inlined_fits) return inlined;

        ast_list_t *children = Match(ast, TextJoin)->children;
        text_opts_t opts = choose_text_options(children);
        if (Text$equal_values(opts.quote, Text("`"))) {
            // Prefer double quotes over backticks, since a block's text needs
            // no escaping of either.
            opts.quote = Text("\"");
            opts.unquote = Text("\"");
        }
        Text_t ret = format_text(opts, children, comments, indent);
        type_ast_t *lang = Match(ast, TextJoin)->lang;
        return lang ? Texts("$", format_type(lang), ret) : ret;
    }
    /*multiline*/ case InlineCCode: {
        DeclareMatch(c_code, ast, InlineCCode);
        // `type` is filled in by the compiler, not the parser, so it is always
        // NULL here: testing it sent every inline C expression multiline, even
        // one that fits. Anything that must span lines can't be inlined anyway,
        // since format_inline_text() refuses a verbatim chunk holding a newline.
        if (inlined_fits) return inlined;
        Text_t code = c_code->type_ast ? Texts("C_code:", format_type(c_code->type_ast)) : Text("C_code");
        text_opts_t opts = {.quote = Text("`"), .unquote = Text("`"), .interp = Text("@"), .verbatim = true};
        return Texts(code, format_text(opts, Match(ast, InlineCCode)->chunks, comments, indent));
    }
    /*multiline*/ case TextLiteral: { fail("Something went wrong, we shouldn't be formatting text literals directly"); }
    /*multiline*/ case Path: {
        assert(inlined.length > 0);
        return inlined;
    }
    /*multiline*/ case Embed: {
        assert(inlined.length > 0);
        return inlined;
    }
    /*multiline*/ case Serialize: {
        if (inlined_fits) return inlined;
        return Texts("serialize(", fmt_at(Match(ast, Serialize)->value, comments, indent, column + 10), ")");
    }
    /*multiline*/ case Deserialize: {
        if (inlined_fits) return inlined;
        DeclareMatch(deserialize, ast, Deserialize);
        Text_t type = format_type(deserialize->type_ast);
        return Texts("deserialize:", type, "(", fmt_at(deserialize->value, comments, indent, column + 13 + type.length),
                     ")");
    }
    /*multiline*/ case Min:
    /*multiline*/ case Max: {
        if (inlined_fits) return inlined;
        ast_t *lhs_ast = ast->tag == Min ? Match(ast, Min)->lhs : Match(ast, Max)->lhs;
        ast_t *rhs_ast = ast->tag == Min ? Match(ast, Min)->rhs : Match(ast, Max)->rhs;
        Text_t lhs = operand(fmt_at(lhs_ast, comments, indent, column), lhs_ast, ast->tag, true, indent, TIGHTEN_NONE);
        ast_t *key = ast->tag == Min ? Match(ast, Min)->key : Match(ast, Max)->key;
        Text_t op = key ? fmt_at(key, comments, indent, column_after(column, lhs) + 1)
                        : (ast->tag == Min ? Text("_min_") : Text("_max_"));
        Text_t before_rhs = Texts(lhs, " ", op, " ");
        Text_t rhs = operand(fmt_at(rhs_ast, comments, indent, column_after(column, before_rhs)), rhs_ast, ast->tag,
                             false, indent, TIGHTEN_NONE);
        return Texts(before_rhs, rhs);
    }
    /*multiline*/ case Reduction: {
        if (inlined_fits) return inlined;
        DeclareMatch(reduction, ast, Reduction);
        if (reduction->key) {
            Text_t key = fmt_at(reduction->key, comments, Texts(indent, single_indent), column + 1);
            return Texts(
                "(", key, ": ",
                fmt_at(reduction->iter, comments, Texts(indent, single_indent), column_after(column + 1, key) + 2),
                ")");
        } else {
            const char *op = binop_info[reduction->op].operator;
            return Texts(
                "(", op, ": ",
                fmt_at(reduction->iter, comments, Texts(indent, single_indent), column + 3 + (int64_t)strlen(op)), ")");
        }
    }
    /*multiline*/ case Break:
    /*multiline*/ case Continue:
    /*multiline*/ case None:
    /*multiline*/ case Bool:
    /*multiline*/ case Int:
    /*multiline*/ case Num:
    /*multiline*/ case Var: {
        assert(inlined.tag != TEXT_NONE);
        return inlined;
    }
    /*multiline*/ case FunctionCall: {
        if (inlined_fits) return inlined;
        DeclareMatch(call, ast, FunctionCall);
        Text_t fn = termify_at(call->fn, comments, indent, column);
        return Texts(fn, format_fncall_at(call->args, comments, indent, column_after(column, fn)));
    }
    /*multiline*/ case RecordLiteral: {
        if (inlined_fits) return inlined;
        DeclareMatch(record, ast, RecordLiteral);
        Text_t type = fmt_at(record->type, comments, indent, column);
        return Texts(type, format_record_literal_at(record->args, comments, indent, column_after(column, type)));
    }
    /*multiline*/ case MethodCall: {
        if (inlined_fits) return inlined;
        DeclareMatch(call, ast, MethodCall);
        Text_t self = Texts(termify_at(call->self, comments, indent, column), ".", Text$from_str(call->name));
        return Texts(self, format_fncall_at(call->args, comments, indent, column_after(column, self)));
    }
    /*multiline*/ case DebugLog: {
        DeclareMatch(debug, ast, DebugLog);
        Text_t code = Texts(">> ");
        for (ast_list_t *value = debug->values; value; value = value->next) {
            // Only a value with another after it has a comma to give back; the
            // last one ends the statement and needs no parentheses.
            int64_t at = column_after(column, code);
            Text_t expr =
                value->next ? bounded_at(value->ast, comments, indent, at) : fmt_at(value->ast, comments, indent, at);
            code = Texts(code, expr);
            if (value->next) code = Texts(code, ", ");
        }
        return code;
    }
    /*multiline*/ case Test: {
        DeclareMatch(test, ast, Test);
        Text_t code = Texts("test ", quoted_label(test->label), "\n", indent, single_indent,
                            fmt(test->body, comments, Texts(indent, single_indent)));
        // At most one outcome clause, dedented back to the `test` keyword's own
        // indentation (that's where the parser looks for it):
        const char *clause = test->expected_compile_error ? "fails_compile" : (test->expected_failure ? "fails" : NULL);
        if (clause) {
            const char *expected = test->expected_compile_error ?: test->expected_failure;
            code = Texts(code, "\n", indent, Text$from_str(clause));
            // An empty expectation is a bare clause meaning "any failure will do":
            if (expected[0] != '\0') code = Texts(code, " ", quoted_label(expected));
        }
        return code;
    }
    /*multiline*/ case Assert: {
        DeclareMatch(assert, ast, Assert);
        Text_t expr = assert->message ? bounded_at(assert->expr, comments, indent, column + 7)
                                      : fmt_at(assert->expr, comments, indent, column + 7);
        if (!assert->message) return Texts("assert ", expr);
        Text_t code = Texts("assert ", expr, ", ");
        return Texts(code, fmt_at(assert->message, comments, indent, column_after(column, code)));
    }
    /*multiline*/ case BINOP_CASES: {
        if (inlined_fits) return inlined;
        return format_binop(ast, comments, indent, column, expression_spacing(ast));
    }
    /*multiline*/ case Use: {
        assert(inlined.length > 0);
        return inlined;
    }
    /*multiline*/ case ExplicitlyTyped:
        fail("Explicitly typed AST nodes are only meant to be used internally.");
    default: {
        if (inlined_fits) return inlined;
        fail("Formatting not implemented for: ", ast_to_sexp(ast));
    }
    }
}

Text_t format_file(const char *path) {
    file_t *file = load_file(path);
    if (!file) return EMPTY_TEXT;
    return format_source(file, NULL);
}

// Format an already-loaded file. `formatted` (when given) reports whether the
// source actually made it through: every early return here hands back the
// original text unchanged, which is the right thing for `tomo fmt` but is
// indistinguishable from a no-op reformat, and `tomo fmt --check` has to tell
// those apart.
public
Text_t format_source(file_t *file, bool *formatted) {
    if (formatted) *formatted = false;
    if (!file) return EMPTY_TEXT;

    // The error is taken as a value and reported here rather than from inside
    // the parser, because the unwind below is what stops the parser reporting
    // it itself, and an unformattable file is worth a diagnostic. It lives on
    // the heap because a modified automatic local of this frame would be
    // indeterminate after the longjmp:
    parse_error_t *parse_err = new (parse_error_t);
    jmp_buf on_err;
    if (setjmp(on_err) != 0) {
        if (parse_err->message) print_parse_error(*parse_err);
        return Text$from_str(file->text);
    }
    parse_ctx_t ctx = {
        .file = file,
        .on_err = &on_err,
        .error = parse_err,
        .comments = {},
    };

    const char *pos = file->text;
    if (match(&pos, "#!")) // shebang
        some_not(&pos, "\r\n");

    whitespace(&ctx, &pos);
    ast_t *ast = parse_file_body(&ctx, pos);
    if (!ast) return Text$from_str(file->text);
    pos = ast->end;
    whitespace(&ctx, &pos);
    if (pos < file->text + file->len && *pos != '\0') {
        return Text$from_str(file->text);
    }

    const char *fmt_pos = file->text;
    Text_t code = comment_range(&fmt_pos, ast->start, EMPTY_TEXT, ctx.comments);
    if (code.length > 0) code = Texts(code, "\n");
    // Special case: allow blank lines between comments and code at the top of
    // the file.
    for (const char *p = fmt_pos + 1; p < ast->start; p++) {
        if (*p == '\n') {
            code = Text$concat(code, Text("\n"));
            break;
        }
    }
    code = Texts(code, fmt(ast, ctx.comments, EMPTY_TEXT));
    fmt_pos = ast->end;
    code = Text$concat(code, comment_range(&fmt_pos, file->text + file->len, EMPTY_TEXT, ctx.comments));
    if (!Text$ends_with(code, Text("\n"), NULL)) {
        code = Text$concat(code, Text("\n"));
    }
    if (formatted) *formatted = true;
    return code;
}
