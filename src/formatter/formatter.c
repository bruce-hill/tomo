// This code defines functions for transforming ASTs back into Tomo source text

#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <unictype.h>

#include "../ast.h"
#include "../parse/context.h"
#include "../parse/files.h"
#include "../parse/utils.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/integers.h"
#include "../stdlib/optionals.h"
#include "../stdlib/stdlib.h"
#include "../stdlib/text.h"
#include "args.h"
#include "enums.h"
#include "formatter.h"
#include "types.h"
#include "utils.h"

#define fmt_inline(...) must(format_inline_code(__VA_ARGS__))
#define fmt(...) format_code(__VA_ARGS__)

Text_t format_namespace(ast_t *namespace, Table_t comments, Text_t indent) {
    if (unwrap_block(namespace) == NULL) return EMPTY_TEXT;
    return Texts("\n", indent, single_indent, fmt(namespace, comments, Texts(indent, single_indent)));
}

typedef struct {
    Text_t quote, unquote, interp;
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

static bool starts_with_id(Text_t text) {
    if (text.length <= 0) return false;
    List_t codepoints = Text$utf32(Text$slice(text, I_small(1), I_small(1)));
    if (codepoints.length <= 0 || codepoints.data == NULL) return false;
    return uc_is_property_xid_continue(*(ucs4_t *)codepoints.data);
}

static OptionalText_t format_inline_text(text_opts_t opts, ast_list_t *chunks, Table_t comments) {
    Text_t code = opts.quote;
    for (ast_list_t *chunk = chunks; chunk; chunk = chunk->next) {
        if (chunk->ast->tag == TextLiteral) {
            Text_t literal = Match(chunk->ast, TextLiteral)->text;
            Text_t segment = Text$escaped(literal, false, Texts(opts.unquote, opts.interp));
            code = Texts(code, segment);
        } else {
            if (chunk->ast->tag == Var
                && (!chunk->next || chunk->next->ast->tag != TextLiteral
                    || !starts_with_id(Match(chunk->next->ast, TextLiteral)->text))) {
                code = Texts(code, opts.interp, fmt_inline(chunk->ast, comments));
            } else {
                code = Texts(code, opts.interp, "(", fmt_inline(chunk->ast, comments), ")");
            }
        }
    }
    return Texts(code, opts.unquote);
}

static Text_t format_text(text_opts_t opts, ast_list_t *chunks, Table_t comments, Text_t indent) {
    Text_t code = EMPTY_TEXT;
    Text_t current_line = EMPTY_TEXT;
    for (ast_list_t *chunk = chunks; chunk; chunk = chunk->next) {
        if (chunk->ast->tag == TextLiteral) {
            Text_t literal = Match(chunk->ast, TextLiteral)->text;
            List_t lines = Text$lines(literal);
            if (lines.length == 0) continue;
            current_line = Texts(current_line, Text$escaped(*(Text_t *)lines.data, false, opts.interp));
            for (int64_t i = 1; i < (int64_t)lines.length; i += 1) {
                add_line(&code, current_line, Texts(indent, single_indent));
                current_line = Text$escaped(*(Text_t *)(lines.data + i * lines.stride), false, opts.interp);
            }
        } else {
            current_line = Texts(current_line, opts.interp, "(", fmt(chunk->ast, comments, indent), ")");
        }
    }
    add_line(&code, current_line, Texts(indent, single_indent));
    code = Texts(opts.quote, "\n", indent, single_indent, code, "\n", indent, opts.unquote);
    return code;
}

OptionalText_t format_inline_code(ast_t *ast, Table_t comments) {
    if (range_has_comment(ast->start, ast->end, comments)) return NONE_TEXT;
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
        Text_t expr = fmt_inline(assert->expr, comments);
        if (!assert->message) return Texts("assert ", expr);
        Text_t message = fmt_inline(assert->message, comments);
        return Texts("assert ", expr, ", ", message);
    }
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

        if (if_->else_body == NULL && if_->condition->tag != Declare) {
            ast_t *stmt = unwrap_block(if_->body);
            if (!stmt) return Texts("pass ", if_condition);
            switch (stmt->tag) {
            case Return:
            case Skip:
            case Stop: return Texts(fmt_inline(stmt, comments), " ", if_condition);
            default: break;
            }
        }

        Text_t code = Texts(if_condition, " then ", fmt_inline(if_->body, comments));
        if (if_->else_body) code = Texts(code, " else ", fmt_inline(if_->else_body, comments));
        return code;
    }
    /*inline*/ case When: {
        DeclareMatch(when, ast, When);
        Text_t code = Texts("when ", fmt_inline(when->subject, comments));
        for (when_clause_t *clause = when->clauses; clause; clause = clause->next) {
            code = Texts(code, " is ", fmt_inline(clause->pattern, comments));
            while (clause->next && clause->next->body == clause->body) {
                clause = clause->next;
                code = Texts(code, ", ", fmt_inline(clause->pattern, comments));
            }
            code = Texts(code, " then ", fmt_inline(clause->body, comments));
        }
        if (when->else_body) code = Texts(code, " else ", fmt_inline(when->else_body, comments));
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
        code = Texts(code, " in ", fmt_inline(loop->iter, comments), " do ", fmt_inline(loop->body, comments));
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
        code = Texts(code, " in ", fmt_inline(comp->iter, comments));
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
        return Texts(fmt_inline(entry->key, comments), "=", fmt_inline(entry->value, comments));
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
        return value ? Texts("return ", fmt_inline(value, comments)) : Text("return");
    }
    /*inline*/ case Not: {
        ast_t *val = Match(ast, Not)->value;
        return Texts("not ", must(termify_inline(val, comments)));
    }
    /*inline*/ case Negative: {
        ast_t *val = Match(ast, Negative)->value;
        return Texts("-", must(termify_inline(val, comments)));
    }
    /*inline*/ case HeapAllocate: {
        ast_t *val = Match(ast, HeapAllocate)->value;
        return Texts("@", must(termify_inline(val, comments)));
    }
    /*inline*/ case StackReference: {
        ast_t *val = Match(ast, StackReference)->value;
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
        if (index->index) return Texts(indexed, "[", fmt_inline(index->index, comments), "]");
        else return Texts(indexed, "[]");
    }
    /*inline*/ case TextJoin: {
        text_opts_t opts = choose_text_options(Match(ast, TextJoin)->children);
        Text_t ret = must(format_inline_text(opts, Match(ast, TextJoin)->children, comments));
        const char *lang = Match(ast, TextJoin)->lang;
        return lang ? Texts("$", Text$from_str(lang), ret) : ret;
    }
    /*inline*/ case InlineCCode: {
        DeclareMatch(c_code, ast, InlineCCode);
        Text_t code = c_code->type_ast ? Texts("C_code:", format_type(c_code->type_ast)) : Text("C_code");
        text_opts_t opts = {.quote = Text("`"), .unquote = Text("`"), .interp = Text("@")};
        return Texts(code, must(format_inline_text(opts, Match(ast, InlineCCode)->chunks, comments)));
    }
    /*inline*/ case TextLiteral: { fail("Something went wrong, we shouldn't be formatting text literals directly"); }
    /*inline*/ case Path: {
        return Texts("(", Text$escaped(Text$from_str(Match(ast, Path)->path), false, Text("()")), ")");
    }
    /*inline*/ case Stop: {
        const char *target = Match(ast, Stop)->target;
        return target ? Texts("stop ", Text$from_str(target)) : Text("stop");
    }
    /*inline*/ case Skip: {
        const char *target = Match(ast, Skip)->target;
        return target ? Texts("skip ", Text$from_str(target)) : Text("skip");
    }
    /*inline*/ case Min:
    /*inline*/ case Max: {
        Text_t lhs = fmt_inline(ast->tag == Min ? Match(ast, Min)->lhs : Match(ast, Max)->lhs, comments);
        Text_t rhs = fmt_inline(ast->tag == Min ? Match(ast, Min)->rhs : Match(ast, Max)->rhs, comments);
        ast_t *key = ast->tag == Min ? Match(ast, Min)->key : Match(ast, Max)->key;
        return Texts(lhs, key ? fmt_inline(key, comments) : (ast->tag == Min ? Text(" _min_ ") : Text(" _max_ ")), rhs);
    }
    /*inline*/ case Reduction: {
        DeclareMatch(reduction, ast, Reduction);
        if (reduction->key) {
            return Texts("(", fmt_inline(reduction->key, comments), ": ", fmt_inline(reduction->iter, comments));
        } else {
            return Texts("(", Text$from_str(binop_info[reduction->op].operator), ": ",
                         fmt_inline(reduction->iter, comments));
        }
    }
    /*inline*/ case None:
        return Text("none");
    /*inline*/ case Bool:
        return Match(ast, Bool)->b ? Text("yes") : Text("no");
    /*inline*/ case Int: {
        OptionalText_t source = ast_source(ast);
        return source.length > 0 ? source : Text$from_str(Match(ast, Int)->str);
    }
    /*inline*/ case Num: {
        OptionalText_t source = ast_source(ast);
        return source.length > 0 ? source : Text$from_str(String(Match(ast, Num)->n));
    }
    /*inline*/ case Var:
        return Text$from_str(Match(ast, Var)->name);
    /*inline*/ case FunctionCall: {
        DeclareMatch(call, ast, FunctionCall);
        return Texts(fmt_inline(call->fn, comments), "(", must(format_inline_args(call->args, comments)), ")");
    }
    /*inline*/ case MethodCall: {
        DeclareMatch(call, ast, MethodCall);
        Text_t self = fmt_inline(call->self, comments);
        if (is_binary_operation(call->self) || call->self->tag == Negative || call->self->tag == Not)
            self = parenthesize(self, EMPTY_TEXT);
        return Texts(self, ".", Text$from_str(call->name), "(", must(format_inline_args(call->args, comments)), ")");
    }
    /*inline*/ case BINOP_CASES: {
        binary_operands_t operands = BINARY_OPERANDS(ast);
        const char *op = binop_info[ast->tag].operator;

        Text_t lhs = fmt_inline(operands.lhs, comments);
        Text_t rhs = fmt_inline(operands.rhs, comments);

        if (is_update_assignment(ast)) {
            return Texts(lhs, " ", Text$from_str(op), " ", rhs);
        }

        if (is_binary_operation(operands.lhs) && op_tightness[operands.lhs->tag] < op_tightness[ast->tag])
            lhs = parenthesize(lhs, EMPTY_TEXT);
        if (is_binary_operation(operands.rhs) && op_tightness[operands.rhs->tag] < op_tightness[ast->tag])
            rhs = parenthesize(rhs, EMPTY_TEXT);

        Text_t space = op_tightness[ast->tag] >= op_tightness[Multiply] ? EMPTY_TEXT : Text(" ");
        return Texts(lhs, space, Text$from_str(binop_info[ast->tag].operator), space, rhs);
    }
    /*inline*/ case Deserialize: {
        DeclareMatch(deserialize, ast, Deserialize);
        return Texts("deserialize(", fmt_inline(deserialize->value, comments), " -> ", format_type(deserialize->type),
                     ")");
    }
    /*inline*/ case Use: {
        DeclareMatch(use, ast, Use);
        // struct {
        //     ast_t *var;
        //     const char *path;
        //     enum { USE_LOCAL, USE_MODULE, USE_SHARED_OBJECT, USE_HEADER, USE_C_CODE, USE_ASM } what;
        // } Use;
        return Texts("use ", use->path);
    }
    /*inline*/ case ExplicitlyTyped:
        fail("Explicitly typed AST nodes are only meant to be used internally.");
    default: {
        fail("Formatting not implemented for: ", ast_to_sexp(ast));
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

Text_t format_code(ast_t *ast, Table_t comments, Text_t indent) {
    OptionalText_t inlined = format_inline_code(ast, comments);
    bool inlined_fits = (inlined.tag != TEXT_NONE && indent.length + inlined.length <= MAX_WIDTH);

    switch (ast->tag) {
    /*multiline*/ case Unknown:
        fail("Invalid AST");
    /*multiline*/ case Block: {
        Text_t code = EMPTY_TEXT;
        bool gap_before_comment = false;
        const char *comment_pos = ast->start;
        for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
            for (OptionalText_t comment;
                 (comment = next_comment(comments, &comment_pos, stmt->ast->start)).length > 0;) {
                if (gap_before_comment) {
                    add_line(&code, Text(""), indent);
                    gap_before_comment = false;
                }
                add_line(&code, Text$trim(comment, Text(" \t\r\n"), false, true), indent);
            }

            if (stmt->ast->tag == Block) {
                add_line(&code,
                         Texts("do\n", indent, single_indent, fmt(stmt->ast, comments, Texts(indent, single_indent))),
                         indent);
            } else {
                add_line(&code, fmt(stmt->ast, comments, indent), indent);
            }
            comment_pos = stmt->ast->end;

            if (stmt->next) {
                int suggested_blanks = suggested_blank_lines(stmt->ast, stmt->next->ast);
                for (int blanks = suggested_blanks; blanks > 0; blanks--)
                    add_line(&code, Text(""), indent);
                gap_before_comment = (suggested_blanks == 0);
            } else gap_before_comment = true;
        }

        for (OptionalText_t comment; (comment = next_comment(comments, &comment_pos, ast->end)).length > 0;) {
            if (gap_before_comment) {
                add_line(&code, Text(""), indent);
                gap_before_comment = false;
            }
            add_line(&code, Text$trim(comment, Text(" \t\r\n"), false, true), indent);
        }
        return code;
    }
    /*multiline*/ case If: {
        DeclareMatch(if_, ast, If);
        Text_t code = if_->condition->tag == Not
                          ? Texts("unless ", fmt(Match(if_->condition, Not)->value, comments, indent))
                          : Texts("if ", fmt(if_->condition, comments, indent));

        code = Texts(code, "\n", indent, single_indent, fmt(if_->body, comments, Texts(indent, single_indent)));
        if (if_->else_body) {
            if (if_->else_body->tag != If) {
                code = Texts(code, "\n", indent, "else\n", indent, single_indent,
                             fmt(if_->else_body, comments, Texts(indent, single_indent)));
            } else {
                code = Texts(code, "\n", indent, "else ", fmt(if_->else_body, comments, indent));
            }
        }
        return code;
    }
    /*multiline*/ case When: {
        DeclareMatch(when, ast, When);
        Text_t code = Texts("when ", fmt(when->subject, comments, indent));
        for (when_clause_t *clause = when->clauses; clause; clause = clause->next) {
            code = Texts(code, "\n", indent, "is ", fmt(clause->pattern, comments, indent));
            while (clause->next && clause->next->body == clause->body) {
                clause = clause->next;
                code = Texts(code, ", ", fmt(clause->pattern, comments, indent));
            }
            code = Texts(code, format_namespace(clause->body, comments, indent));
        }
        if (when->else_body)
            code = Texts(code, "\n", indent, "else", format_namespace(when->else_body, comments, indent));
        return code;
    }
    /*multiline*/ case Repeat: {
        return Texts("repeat\n", indent, single_indent,
                     fmt(Match(ast, Repeat)->body, comments, Texts(indent, single_indent)));
    }
    /*multiline*/ case While: {
        DeclareMatch(loop, ast, While);
        return Texts("while ", fmt(loop->condition, comments, indent), "\n", indent, single_indent,
                     fmt(loop->body, comments, Texts(indent, single_indent)));
    }
    /*multiline*/ case For: {
        DeclareMatch(loop, ast, For);
        Text_t code = Text("for ");
        for (ast_list_t *var = loop->vars; var; var = var->next) {
            code = Texts(code, fmt(var->ast, comments, indent));
            if (var->next) code = Texts(code, ", ");
        }
        code = Texts(code, " in ", fmt(loop->iter, comments, indent), format_namespace(loop->body, comments, indent));
        if (loop->empty) code = Texts(code, "\n", indent, "else", format_namespace(loop->empty, comments, indent));
        return code;
    }
    /*multiline*/ case Comprehension: {
        if (inlined_fits) return inlined;
        DeclareMatch(comp, ast, Comprehension);
        Text_t code = Texts("(", fmt(comp->expr, comments, indent));
        if (code.length >= MAX_WIDTH) code = Texts(code, "\n", indent, "for ");
        else code = Texts(code, " for ");

        for (ast_list_t *var = comp->vars; var; var = var->next) {
            code = Texts(code, fmt(var->ast, comments, indent));
            if (var->next) code = Texts(code, ", ");
        }

        code = Texts(code, " in ", fmt(comp->iter, comments, indent));

        if (comp->filter) {
            if (code.length >= MAX_WIDTH) code = Texts(code, "\n", indent, "if ");
            else code = Texts(code, " if ");
            code = Texts(code, fmt(comp->filter, comments, indent));
        }
        return code;
    }
    /*multiline*/ case FunctionDef: {
        DeclareMatch(func, ast, FunctionDef);
        Text_t code = Texts("func ", fmt(func->name, comments, indent), "(", format_args(func->args, comments, indent));
        if (func->ret_type) code = Texts(code, func->args ? Text(" -> ") : Text("-> "), format_type(func->ret_type));
        if (func->cache) code = Texts(code, "; cache=", fmt(func->cache, comments, indent));
        if (func->is_inline) code = Texts(code, "; inline");
        code = Texts(code, Text$has(code, Text("\n")) ? Texts("\n", indent, ")") : Text(")"), "\n", indent,
                     single_indent, fmt(func->body, comments, Texts(indent, single_indent)));
        return Texts(code);
    }
    /*multiline*/ case Lambda: {
        if (inlined_fits) return inlined;
        DeclareMatch(lambda, ast, Lambda);
        Text_t code = Texts("func(", format_args(lambda->args, comments, indent));
        if (lambda->ret_type)
            code = Texts(code, lambda->args ? Text(" -> ") : Text("-> "), format_type(lambda->ret_type));
        code = Texts(code, Text$has(code, Text("\n")) ? Texts("\n", indent, ")") : Text(")"), "\n", indent,
                     single_indent, fmt(lambda->body, comments, Texts(indent, single_indent)));
        return Texts(code);
    }
    /*multiline*/ case ConvertDef: {
        DeclareMatch(convert, ast, ConvertDef);
        Text_t code = Texts("convert (", format_args(convert->args, comments, indent));
        if (convert->ret_type)
            code = Texts(code, convert->args ? Text(" -> ") : Text("-> "), format_type(convert->ret_type));
        if (convert->cache) code = Texts(code, "; cache=", fmt(convert->cache, comments, indent));
        if (convert->is_inline) code = Texts(code, "; inline");
        code = Texts(code, Text$has(code, Text("\n")) ? Texts("\n", indent, ")") : Text(")"), "\n", indent,
                     single_indent, fmt(convert->body, comments, Texts(indent, single_indent)));
        return Texts(code);
    }
    /*multiline*/ case StructDef: {
        DeclareMatch(def, ast, StructDef);
        Text_t args = format_args(def->fields, comments, indent);
        Text_t code = Texts("struct ", Text$from_str(def->name), "(", args);
        if (def->secret) code = Texts(code, "; secret");
        if (def->external) code = Texts(code, "; external");
        if (def->opaque) code = Texts(code, "; opaque");
        code = Texts(code, Text$has(code, Text("\n")) ? Texts("\n", indent, ")") : Text(")"));
        return Texts(code, format_namespace(def->namespace, comments, indent));
    }
    /*multiline*/ case EnumDef: {
        DeclareMatch(def, ast, EnumDef);
        Text_t code = Texts("enum ", Text$from_str(def->name), "(", format_tags(def->tags, comments, indent));
        return Texts(code, Text$has(code, Text("\n")) ? Texts("\n", indent, ")") : Text(")"),
                     format_namespace(def->namespace, comments, indent));
    }
    /*multiline*/ case LangDef: {
        DeclareMatch(def, ast, LangDef);
        return Texts("lang ", Text$from_str(def->name), format_namespace(def->namespace, comments, indent));
    }
    /*multiline*/ case Defer:
        return Texts("defer ", format_namespace(Match(ast, Defer)->body, comments, indent));
    /*multiline*/ case List: {
        if (inlined_fits) return inlined;
        ast_list_t *items = Match(ast, List)->items;
        Text_t code = Text("[");
        const char *comment_pos = ast->start;
        for (ast_list_t *item = items; item; item = item->next) {
            for (OptionalText_t comment;
                 (comment = next_comment(comments, &comment_pos, item->ast->start)).length > 0;) {
                add_line(&code, Text$trim(comment, Text(" \t\r\n"), false, true), Texts(indent, single_indent));
            }
            Text_t item_text = fmt(item->ast, comments, Texts(indent, single_indent));
            if (Text$ends_with(code, Text(","), NULL)) {
                if (!Text$has(item_text, Text("\n")) && trailing_line_len(code) + 1 + item_text.length + 1 <= MAX_WIDTH)
                    code = Texts(code, " ", item_text, ",");
                else code = Texts(code, "\n", indent, single_indent, item_text, ",");
            } else {
                add_line(&code, Texts(item_text, ","), Texts(indent, single_indent));
            }
        }
        for (OptionalText_t comment; (comment = next_comment(comments, &comment_pos, ast->end)).length > 0;) {
            add_line(&code, Text$trim(comment, Text(" \t\r\n"), false, true), Texts(indent, single_indent));
        }
        return Texts(code, "\n", indent, "]");
    }
    /*multiline*/ case Table: {
        if (inlined_fits) return inlined;
        DeclareMatch(table, ast, Table);
        Text_t code = Texts("{");
        const char *comment_pos = ast->start;
        for (ast_list_t *entry = table->entries; entry; entry = entry->next) {
            for (OptionalText_t comment;
                 (comment = next_comment(comments, &comment_pos, entry->ast->start)).length > 0;) {
                add_line(&code, Text$trim(comment, Text(" \t\r\n"), false, true), Texts(indent, single_indent));
            }

            Text_t entry_text = fmt(entry->ast, comments, Texts(indent, single_indent));
            if (Text$ends_with(code, Text(","), NULL)) {
                if (!Text$has(entry_text, Text("\n"))
                    && trailing_line_len(code) + 1 + entry_text.length + 1 <= MAX_WIDTH)
                    code = Texts(code, " ", entry_text, ",");
                else code = Texts(code, "\n", indent, single_indent, entry_text, ",");
            } else {
                add_line(&code, Texts(entry_text, ","), Texts(indent, single_indent));
            }

            add_line(&code, Texts(entry_text, ","), Texts(indent, single_indent));
        }
        for (OptionalText_t comment; (comment = next_comment(comments, &comment_pos, ast->end)).length > 0;) {
            add_line(&code, Text$trim(comment, Text(" \t\r\n"), false, true), Texts(indent, single_indent));
        }

        if (table->fallback)
            code = Texts(code, ";\n", indent, single_indent, "fallback=", fmt(table->fallback, comments, indent));

        if (table->default_value)
            code = Texts(code, ";\n", indent, single_indent, "default=", fmt(table->default_value, comments, indent));

        return Texts(code, "\n", indent, "}");
    }
    /*multiline*/ case TableEntry: {
        if (inlined_fits) return inlined;
        DeclareMatch(entry, ast, TableEntry);
        return Texts(fmt(entry->key, comments, indent), ": ", fmt(entry->value, comments, indent));
    }
    /*multiline*/ case Declare: {
        if (inlined_fits) return inlined;
        DeclareMatch(decl, ast, Declare);
        Text_t code = fmt(decl->var, comments, indent);
        if (decl->type) code = Texts(code, " : ", format_type(decl->type));
        if (decl->value)
            code = Texts(code, decl->type ? Text(" = ") : Text(" := "), fmt(decl->value, comments, indent));
        return code;
    }
    /*multiline*/ case Assign: {
        if (inlined_fits) return inlined;
        DeclareMatch(assign, ast, Assign);
        Text_t code = EMPTY_TEXT;
        for (ast_list_t *target = assign->targets; target; target = target->next) {
            code = Texts(code, fmt(target->ast, comments, indent));
            if (target->next) code = Texts(code, ", ");
        }
        code = Texts(code, " = ");
        for (ast_list_t *value = assign->values; value; value = value->next) {
            code = Texts(code, fmt(value->ast, comments, indent));
            if (value->next) code = Texts(code, ", ");
        }
        return code;
    }
    /*multiline*/ case Pass:
        return Text("pass");
    /*multiline*/ case Return: {
        if (inlined_fits) return inlined;
        ast_t *value = Match(ast, Return)->value;
        return value ? Texts("return ", fmt(value, comments, indent)) : Text("return");
    }
    /*inline*/ case Not: {
        if (inlined_fits) return inlined;
        ast_t *val = Match(ast, Not)->value;
        if (is_binary_operation(val)) return Texts("not ", termify(val, comments, indent));
        else return Texts("not ", fmt(val, comments, indent));
    }
    /*inline*/ case Negative: {
        if (inlined_fits) return inlined;
        ast_t *val = Match(ast, Negative)->value;
        if (is_binary_operation(val)) return Texts("-", termify(val, comments, indent));
        else return Texts("-", fmt(val, comments, indent));
    }
    /*multiline*/ case HeapAllocate: {
        if (inlined_fits) return inlined;
        ast_t *val = Match(ast, HeapAllocate)->value;
        return Texts("@", termify(val, comments, indent), "");
    }
    /*multiline*/ case StackReference: {
        if (inlined_fits) return inlined;
        ast_t *val = Match(ast, StackReference)->value;
        return Texts("&(", termify(val, comments, indent), ")");
    }
    /*multiline*/ case NonOptional: {
        if (inlined_fits) return inlined;
        ast_t *val = Match(ast, NonOptional)->value;
        return Texts(termify(val, comments, indent), "!");
    }
    /*multiline*/ case FieldAccess: {
        if (inlined_fits) return inlined;
        DeclareMatch(access, ast, FieldAccess);
        return Texts(termify(access->fielded, comments, indent), ".", Text$from_str(access->field));
    }
    /*multiline*/ case Index: {
        if (inlined_fits) return inlined;
        DeclareMatch(index, ast, Index);
        if (index->index)
            return Texts(termify(index->indexed, comments, indent), "[", fmt(index->index, comments, indent), "]");
        else return Texts(termify(index->indexed, comments, indent), "[]");
    }
    /*multiline*/ case TextJoin: {
        if (inlined_fits) return inlined;

        text_opts_t opts = choose_text_options(Match(ast, TextJoin)->children);
        Text_t ret = format_text(opts, Match(ast, TextJoin)->children, comments, indent);
        const char *lang = Match(ast, TextJoin)->lang;
        return lang ? Texts("$", Text$from_str(lang), ret) : ret;
    }
    /*multiline*/ case InlineCCode: {
        DeclareMatch(c_code, ast, InlineCCode);
        if (inlined_fits && c_code->type != NULL) return inlined;
        Text_t code = c_code->type_ast ? Texts("C_code:", format_type(c_code->type_ast)) : Text("C_code");
        text_opts_t opts = {.quote = Text("`"), .unquote = Text("`"), .interp = Text("@")};
        return Texts(code, format_text(opts, Match(ast, InlineCCode)->chunks, comments, indent));
    }
    /*multiline*/ case TextLiteral: { fail("Something went wrong, we shouldn't be formatting text literals directly"); }
    /*multiline*/ case Path: {
        assert(inlined.length > 0);
        return inlined;
    }
    /*multiline*/ case Min:
    /*multiline*/ case Max: {
        if (inlined_fits) return inlined;
        Text_t lhs = termify(ast->tag == Min ? Match(ast, Min)->lhs : Match(ast, Max)->lhs, comments, indent);
        Text_t rhs = termify(ast->tag == Min ? Match(ast, Min)->rhs : Match(ast, Max)->rhs, comments, indent);
        ast_t *key = ast->tag == Min ? Match(ast, Min)->key : Match(ast, Max)->key;
        Text_t op = key ? fmt(key, comments, indent) : (ast->tag == Min ? Text("_min_") : Text("_max_"));
        return Texts(lhs, " ", op, " ", rhs);
    }
    /*multiline*/ case Reduction: {
        if (inlined_fits) return inlined;
        DeclareMatch(reduction, ast, Reduction);
        if (reduction->key) {
            return Texts("(", fmt(reduction->key, comments, Texts(indent, single_indent)), ": ",
                         fmt(reduction->iter, comments, Texts(indent, single_indent)));
        } else {
            return Texts("(", binop_info[reduction->op].operator, ": ",
                         fmt(reduction->iter, comments, Texts(indent, single_indent)));
        }
    }
    /*multiline*/ case Stop:
    /*multiline*/ case Skip:
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
        Text_t args = format_args(call->args, comments, indent);
        return Texts(fmt(call->fn, comments, indent), "(", args,
                     Text$has(args, Text("\n")) ? Texts("\n", indent) : EMPTY_TEXT, ")");
    }
    /*multiline*/ case MethodCall: {
        if (inlined_fits) return inlined;
        DeclareMatch(call, ast, MethodCall);
        Text_t args = format_args(call->args, comments, indent);
        return Texts(termify(call->self, comments, indent), ".", Text$from_str(call->name), "(", args,
                     Text$has(args, Text("\n")) ? Texts("\n", indent) : EMPTY_TEXT, ")");
    }
    /*multiline*/ case DebugLog: {
        DeclareMatch(debug, ast, DebugLog);
        Text_t code = Texts(">> ");
        for (ast_list_t *value = debug->values; value; value = value->next) {
            Text_t expr = fmt(value->ast, comments, indent);
            code = Texts(code, expr);
            if (value->next) code = Texts(code, ", ");
        }
        return code;
    }
    /*multiline*/ case Assert: {
        DeclareMatch(assert, ast, Assert);
        Text_t expr = fmt(assert->expr, comments, indent);
        if (!assert->message) return Texts("assert ", expr);
        Text_t message = fmt(assert->message, comments, indent);
        return Texts("assert ", expr, ", ", message);
    }
    /*multiline*/ case BINOP_CASES: {
        if (inlined_fits) return inlined;
        binary_operands_t operands = BINARY_OPERANDS(ast);
        const char *op = binop_info[ast->tag].operator;
        Text_t lhs = fmt(operands.lhs, comments, indent);
        Text_t rhs = fmt(operands.rhs, comments, indent);

        if (is_update_assignment(ast)) {
            return Texts(lhs, " ", Text$from_str(op), " ", rhs);
        }

        if (is_binary_operation(operands.lhs) && op_tightness[operands.lhs->tag] < op_tightness[ast->tag])
            lhs = parenthesize(lhs, indent);
        if (is_binary_operation(operands.rhs) && op_tightness[operands.rhs->tag] < op_tightness[ast->tag])
            rhs = parenthesize(rhs, indent);

        Text_t space = op_tightness[ast->tag] >= op_tightness[Multiply] ? EMPTY_TEXT : Text(" ");
        return Texts(lhs, space, Text$from_str(binop_info[ast->tag].operator), space, rhs);
    }
    /*multiline*/ case Deserialize: {
        if (inlined_fits) return inlined;
        DeclareMatch(deserialize, ast, Deserialize);
        return Texts("deserialize(", fmt(deserialize->value, comments, indent), " -> ", format_type(deserialize->type),
                     ")");
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

    jmp_buf on_err;
    if (setjmp(on_err) != 0) {
        return Text$from_str(file->text);
    }
    parse_ctx_t ctx = {
        .file = file,
        .on_err = &on_err,
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
    Text_t code = EMPTY_TEXT;
    for (OptionalText_t comment; (comment = next_comment(ctx.comments, &fmt_pos, ast->start)).length > 0;) {
        code = Texts(code, Text$trim(comment, Text(" \t\r\n"), false, true), "\n");
    }
    code = Texts(code, fmt(ast, ctx.comments, EMPTY_TEXT));
    for (OptionalText_t comment; (comment = next_comment(ctx.comments, &fmt_pos, ast->start)).length > 0;) {
        code = Texts(code, Text$trim(comment, Text(" \t\r\n"), false, true), "\n");
    }
    return code;
}
