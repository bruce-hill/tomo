// This code defines functions for transforming ASTs back into Tomo source text

#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>

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
    /*inline*/ case Extend:
    /*inline*/ case FunctionDef:
    /*inline*/ case DocTest:
        return NONE_TEXT;
    /*inline*/ case If: {
        DeclareMatch(if_, ast, If);

        if (if_->else_body == NULL && if_->condition->tag != Declare) {
            ast_t *stmt = unwrap_block(if_->body);
            switch (stmt->tag) {
            case Return:
            case Skip:
            case Stop: return Texts(fmt_inline(stmt, comments), " if ", fmt_inline(if_->condition, comments));
            default: break;
            }
        }

        Text_t code = Texts("if ", fmt_inline(if_->condition, comments), " then ", fmt_inline(if_->body, comments));
        if (if_->else_body) code = Texts(code, " else ", fmt_inline(if_->else_body, comments));
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
    /*inline*/ case List:
    /*inline*/ case Set: {
        ast_list_t *items = ast->tag == List ? Match(ast, List)->items : Match(ast, Set)->items;
        Text_t code = EMPTY_TEXT;
        for (ast_list_t *item = items; item; item = item->next) {
            code = Texts(code, fmt_inline(item->ast, comments));
            if (item->next) code = Texts(code, ", ");
        }
        return ast->tag == List ? Texts("[", code, "]") : Texts("|", code, "|");
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
    /*inline*/ case HeapAllocate: {
        ast_t *val = Match(ast, HeapAllocate)->value;
        return Texts("@", fmt_inline(val, comments));
    }
    /*inline*/ case StackReference: {
        ast_t *val = Match(ast, StackReference)->value;
        return Texts("&", fmt_inline(val, comments));
    }
    /*inline*/ case Optional: {
        ast_t *val = Match(ast, Optional)->value;
        return Texts(fmt_inline(val, comments), "?");
    }
    /*inline*/ case NonOptional: {
        ast_t *val = Match(ast, NonOptional)->value;
        return Texts(fmt_inline(val, comments), "!");
    }
    /*inline*/ case FieldAccess: {
        DeclareMatch(access, ast, FieldAccess);
        return Texts(fmt_inline(access->fielded, comments), ".", Text$from_str(access->field));
    }
    /*inline*/ case Index: {
        DeclareMatch(index, ast, Index);
        if (index->index)
            return Texts(fmt_inline(index->indexed, comments), "[", fmt_inline(index->index, comments), "]");
        else return Texts(fmt_inline(index->indexed, comments), "[]");
    }
    /*inline*/ case TextJoin: {
        // TODO: choose quotation mark more smartly
        Text_t source = Text$from_strn(ast->start, (int64_t)(ast->end - ast->start));
        Text_t quote = Text$to(source, I(1));
        const char *lang = Match(ast, TextJoin)->lang;
        Text_t code = lang ? Texts("$", Text$from_str(lang), quote) : quote;
        for (ast_list_t *chunk = Match(ast, TextJoin)->children; chunk; chunk = chunk->next) {
            if (chunk->ast->tag == TextLiteral) {
                Text_t literal = Match(chunk->ast, TextLiteral)->text;
                code = Texts(code, Text$slice(Text$quoted(literal, false, quote), I(2), I(-2)));
            } else {
                code = Texts(code, "$(", fmt_inline(chunk->ast, comments), ")");
            }
        }
        return Texts(code, quote);
    }
    /*inline*/ case TextLiteral: { fail("Something went wrong, we shouldn't be formatting text literals directly"); }
    /*inline*/ case Stop: {
        const char *target = Match(ast, Stop)->target;
        return target ? Texts("stop ", Text$from_str(target)) : Text("stop");
    }
    /*inline*/ case Skip: {
        const char *target = Match(ast, Skip)->target;
        return target ? Texts("skip ", Text$from_str(target)) : Text("skip");
    }
    /*inline*/ case None:
    /*inline*/ case Bool:
    /*inline*/ case Int:
    /*inline*/ case Num:
    /*inline*/ case Var: {
        Text_t code = Text$from_strn(ast->start, (int64_t)(ast->end - ast->start));
        if (Text$has(code, Text("\n"))) return NONE_TEXT;
        return code;
    }
    /*inline*/ case FunctionCall: {
        DeclareMatch(call, ast, FunctionCall);
        return Texts(fmt_inline(call->fn, comments), "(", must(format_inline_args(call->args, comments)), ")");
    }
    /*inline*/ case MethodCall: {
        DeclareMatch(call, ast, MethodCall);
        return Texts(fmt_inline(call->self, comments), ".", Text$from_str(call->name), "(",
                     must(format_inline_args(call->args, comments)), ")");
    }
    /*inline*/ case BINOP_CASES: {
        binary_operands_t operands = BINARY_OPERANDS(ast);
        const char *op = binop_tomo_operator(ast->tag);

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
        return Texts(lhs, space, Text$from_str(binop_tomo_operator(ast->tag)), space, rhs);
    }
    default: {
        fail("Formatting not implemented for: ", ast_to_sexp(ast));
    }
    }
}

Text_t format_code(ast_t *ast, Table_t comments, Text_t indent) {
    OptionalText_t inlined = format_inline_code(ast, comments);
    bool inlined_fits = (inlined.length >= 0 && indent.length + inlined.length <= MAX_WIDTH);

    switch (ast->tag) {
    /*multiline*/ case Unknown:
        fail("Invalid AST");
    /*multiline*/ case Block: {
        Text_t code = EMPTY_TEXT;
        bool gap_before_comment = false;
        const char *comment_pos = ast->start;
        for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
            if (should_have_blank_line(stmt->ast)) add_line(&code, Text(""), indent);

            for (OptionalText_t comment;
                 (comment = next_comment(comments, &comment_pos, stmt->ast->start)).length > 0;) {
                if (gap_before_comment) {
                    add_line(&code, Text(""), indent);
                    gap_before_comment = false;
                }
                add_line(&code, Text$trim(comment, Text(" \t\r\n"), false, true), indent);
            }

            add_line(&code, fmt(stmt->ast, comments, indent), indent);
            comment_pos = stmt->ast->end;

            if (should_have_blank_line(stmt->ast) && stmt->next && !should_have_blank_line(stmt->next->ast))
                add_line(&code, Text(""), indent);
            else gap_before_comment = true;
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
        if (inlined_fits && if_->else_body == NULL) return inlined;

        Text_t code = Texts("if ", fmt(if_->condition, comments, indent), "\n", indent, single_indent,
                            fmt(if_->body, comments, Texts(indent, single_indent)));
        if (if_->else_body)
            code = Texts(code, "\n", indent, "else \n", indent, single_indent,
                         fmt(if_->else_body, comments, Texts(indent, single_indent)));
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
        // ast_t *name;
        // arg_ast_t *args;
        // type_ast_t *ret_type;
        // ast_t *body;
        // ast_t *cache;
        // bool is_inline;
        Text_t code = Texts("func ", fmt(func->name, comments, indent), "(", format_args(func->args, comments, indent));
        if (func->ret_type) code = Texts(code, func->args ? Text(" -> ") : Text("-> "), format_type(func->ret_type));
        if (func->cache) code = Texts(code, "; cache=", fmt(func->cache, comments, indent));
        if (func->is_inline) code = Texts(code, "; inline");
        code = Texts(code, ")\n", indent, single_indent, fmt(func->body, comments, Texts(indent, single_indent)));
        return Texts(code);
    }
    /*multiline*/ case StructDef: {
        DeclareMatch(def, ast, StructDef);
        Text_t code = Texts("struct ", Text$from_str(def->name), "(", format_args(def->fields, comments, indent));
        if (def->secret) code = Texts(code, "; secret");
        if (def->external) code = Texts(code, "; external");
        if (def->opaque) code = Texts(code, "; opaque");
        return Texts(code, ")", format_namespace(def->namespace, comments, indent));
    }
    /*multiline*/ case EnumDef: {
        DeclareMatch(def, ast, EnumDef);
        Text_t code = Texts("enum ", Text$from_str(def->name), "(", format_tags(def->tags, comments, indent));
        return Texts(code, ")", format_namespace(def->namespace, comments, indent));
    }
    /*multiline*/ case LangDef: {
        DeclareMatch(def, ast, LangDef);
        return Texts("lang ", Text$from_str(def->name), format_namespace(def->namespace, comments, indent));
    }
    /*multiline*/ case Extend: {
        DeclareMatch(extend, ast, Extend);
        return Texts("lang ", Text$from_str(extend->name), format_namespace(extend->body, comments, indent));
    }
    /*multiline*/ case List:
    /*multiline*/ case Set: {
        if (inlined_fits) return inlined;
        ast_list_t *items = ast->tag == List ? Match(ast, List)->items : Match(ast, Set)->items;
        Text_t code = EMPTY_TEXT;
        const char *comment_pos = ast->start;
        for (ast_list_t *item = items; item; item = item->next) {
            for (OptionalText_t comment;
                 (comment = next_comment(comments, &comment_pos, item->ast->start)).length > 0;) {
                add_line(&code, Text$trim(comment, Text(" \t\r\n"), false, true), Texts(indent, single_indent));
            }
            add_line(&code, Texts(fmt(item->ast, comments, Texts(indent, single_indent)), ","),
                     Texts(indent, single_indent));
        }
        for (OptionalText_t comment; (comment = next_comment(comments, &comment_pos, ast->end)).length > 0;) {
            add_line(&code, Text$trim(comment, Text(" \t\r\n"), false, true), Texts(indent, single_indent));
        }
        return ast->tag == List ? Texts("[\n", indent, single_indent, code, "\n", indent, "]")
                                : Texts("|\n", indent, single_indent, code, "\n", indent, "|");
    }
    /*multiline*/ case Table: {
        if (inlined_fits) return inlined;
        DeclareMatch(table, ast, Table);
        Text_t code = EMPTY_TEXT;
        const char *comment_pos = ast->start;
        for (ast_list_t *entry = table->entries; entry; entry = entry->next) {
            for (OptionalText_t comment;
                 (comment = next_comment(comments, &comment_pos, entry->ast->start)).length > 0;) {
                add_line(&code, Text$trim(comment, Text(" \t\r\n"), false, true), Texts(indent, single_indent));
            }
            add_line(&code, Texts(fmt(entry->ast, comments, Texts(indent, single_indent)), ","),
                     Texts(indent, single_indent));
        }
        for (OptionalText_t comment; (comment = next_comment(comments, &comment_pos, ast->end)).length > 0;) {
            add_line(&code, Text$trim(comment, Text(" \t\r\n"), false, true), Texts(indent, single_indent));
        }

        if (table->fallback)
            code = Texts(code, ";\n", indent, single_indent, "fallback=", fmt(table->fallback, comments, indent));

        if (table->default_value)
            code = Texts(code, ";\n", indent, single_indent, "default=", fmt(table->default_value, comments, indent));

        return Texts("{\n", indent, single_indent, code, "\n", indent, "}");
    }
    /*multiline*/ case TableEntry: {
        if (inlined_fits) return inlined;
        DeclareMatch(entry, ast, TableEntry);
        return Texts(fmt(entry->key, comments, indent), "=", fmt(entry->value, comments, indent));
    }
    /*multiline*/ case Declare: {
        DeclareMatch(decl, ast, Declare);
        Text_t code = fmt(decl->var, comments, indent);
        if (decl->type) code = Texts(code, " : ", format_type(decl->type));
        if (decl->value)
            code = Texts(code, decl->type ? Text(" = ") : Text(" := "), fmt(decl->value, comments, indent));
        return code;
    }
    /*multiline*/ case Assign: {
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
        ast_t *value = Match(ast, Return)->value;
        return value ? Texts("return ", fmt(value, comments, indent)) : Text("return");
    }
    /*multiline*/ case HeapAllocate: {
        if (inlined_fits) return inlined;
        ast_t *val = Match(ast, HeapAllocate)->value;
        return Texts("@(", fmt(val, comments, indent), ")");
    }
    /*multiline*/ case StackReference: {
        if (inlined_fits) return inlined;
        ast_t *val = Match(ast, StackReference)->value;
        return Texts("&(", fmt(val, comments, indent), ")");
    }
    /*multiline*/ case Optional: {
        if (inlined_fits) return inlined;
        ast_t *val = Match(ast, Optional)->value;
        return Texts("(", fmt(val, comments, indent), ")?");
    }
    /*multiline*/ case NonOptional: {
        if (inlined_fits) return inlined;
        ast_t *val = Match(ast, NonOptional)->value;
        return Texts("(", fmt(val, comments, indent), ")!");
    }
    /*multiline*/ case FieldAccess: {
        DeclareMatch(access, ast, FieldAccess);
        return Texts(fmt(access->fielded, comments, indent), ".", Text$from_str(access->field));
    }
    /*multiline*/ case Index: {
        DeclareMatch(index, ast, Index);
        if (index->index)
            return Texts(fmt(index->indexed, comments, indent), "[", fmt(index->index, comments, indent), "]");
        else return Texts(fmt(index->indexed, comments, indent), "[]");
    }
    /*multiline*/ case TextJoin: {
        if (inlined_fits) return inlined;
        // TODO: choose quotation mark more smartly
        Text_t source = Text$from_strn(ast->start, (int64_t)(ast->end - ast->start));
        Text_t quote = Text$to(source, I(1));
        const char *lang = Match(ast, TextJoin)->lang;
        Text_t code = EMPTY_TEXT;
        Text_t current_line = EMPTY_TEXT;
        for (ast_list_t *chunk = Match(ast, TextJoin)->children; chunk; chunk = chunk->next) {
            if (chunk->ast->tag == TextLiteral) {
                Text_t literal = Match(chunk->ast, TextLiteral)->text;
                List_t lines = Text$lines(literal);
                if (lines.length == 0) continue;
                current_line = Texts(current_line, *(Text_t *)lines.data);
                for (int64_t i = 1; i < lines.length; i += 1) {
                    add_line(&code, current_line, Texts(indent, single_indent));
                    current_line = *(Text_t *)(lines.data + i * lines.stride);
                }
            } else {
                code = Texts(code, "$(", fmt_inline(chunk->ast, comments), ")");
            }
        }
        add_line(&code, current_line, Texts(indent, single_indent));
        code = Texts(quote, "\n", indent, single_indent, code, "\n", indent, quote);
        if (lang) code = Texts("$", Text$from_str(lang), code);
        return code;
    }
    /*multiline*/ case TextLiteral: { fail("Something went wrong, we shouldn't be formatting text literals directly"); }
    /*multiline*/ case Stop:
    /*multiline*/ case Skip:
    /*multiline*/ case None:
    /*multiline*/ case Bool:
    /*multiline*/ case Int:
    /*multiline*/ case Num:
    /*multiline*/ case Var: {
        assert(inlined.length >= 0);
        return inlined;
    }
    /*multiline*/ case FunctionCall: {
        if (inlined_fits) return inlined;
        DeclareMatch(call, ast, FunctionCall);
        return Texts(fmt(call->fn, comments, indent), "(\n", indent, single_indent,
                     format_args(call->args, comments, Texts(indent, single_indent)), "\n", indent, ")");
    }
    /*multiline*/ case MethodCall: {
        if (inlined_fits) return inlined;
        DeclareMatch(call, ast, MethodCall);
        return Texts(fmt(call->self, comments, indent), ".", Text$from_str(call->name), "(\n", indent, single_indent,
                     format_args(call->args, comments, Texts(indent, single_indent)), "\n", indent, ")");
    }
    /*multiline*/ case DocTest: {
        DeclareMatch(test, ast, DocTest);
        Text_t expr = fmt(test->expr, comments, indent);
        Text_t code = Texts(">> ", Text$replace(expr, Texts("\n", indent), Texts("\n", indent, ".. ")));
        if (test->expected) {
            Text_t expected = fmt(test->expected, comments, indent);
            code = Texts(code, "\n", indent, "= ",
                         Text$replace(expected, Texts("\n", indent), Texts("\n", indent, ".. ")));
        }
        return code;
    }
    /*multiline*/ case BINOP_CASES: {
        if (inlined_fits) return inlined;
        binary_operands_t operands = BINARY_OPERANDS(ast);
        const char *op = binop_tomo_operator(ast->tag);
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
        return Texts(lhs, space, Text$from_str(binop_tomo_operator(ast->tag)), space, rhs);
    }
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
