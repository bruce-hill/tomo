// This code defines functions for transforming ASTs back into Tomo source text

#include <assert.h>
#include <setjmp.h>

#include "ast.h"
#include "formatter.h"
#include "parse/context.h"
#include "parse/files.h"
#include "parse/utils.h"
#include "stdlib/datatypes.h"
#include "stdlib/integers.h"
#include "stdlib/optionals.h"
#include "stdlib/stdlib.h"
#include "stdlib/tables.h"
#include "stdlib/text.h"

#define MAX_WIDTH 100

#define must(expr)                                                                                                     \
    ({                                                                                                                 \
        OptionalText_t _expr = expr;                                                                                   \
        if (_expr.length < 0) return NONE_TEXT;                                                                        \
        (Text_t) _expr;                                                                                                \
    })

const Text_t single_indent = Text("    ");

static void add_line(Text_t *code, Text_t line, Text_t indent) {
    if (code->length == 0) {
        *code = line;
    } else {
        if (line.length > 0) *code = Texts(*code, "\n", indent, line);
        else *code = Texts(*code, "\n");
    }
}

OptionalText_t next_comment(Table_t comments, const char **pos, const char *end) {
    for (const char *p = *pos; p < end; p++) {
        const char **comment_end = Table$get(comments, &p, parse_comments_info);
        if (comment_end) {
            *pos = *comment_end;
            return Text$from_strn(p, (int64_t)(*comment_end - p));
        }
    }
    return NONE_TEXT;
}

static bool range_has_comment(const char *start, const char *end, Table_t comments) {
    OptionalText_t comment = next_comment(comments, &start, end);
    return (comment.length >= 0);
}

static bool should_have_blank_line(ast_t *ast) {
    switch (ast->tag) {
    case If:
    case When:
    case Repeat:
    case While:
    case For:
    case Block:
    case FunctionDef:
    case StructDef:
    case EnumDef:
    case LangDef:
    case ConvertDef:
    case Extend: return true;
    default: return false;
    }
}

static Text_t indent_code(Text_t code) {
    if (code.length <= 0) return code;
    return Texts(single_indent, Text$replace(code, Text("\n"), Texts("\n", single_indent)));
}

static Text_t parenthesize(Text_t code, Text_t indent) {
    if (Text$has(code, Text("\n"))) return Texts("(\n", indent, indent_code(code), "\n", indent, ")");
    else return Texts("(", code, ")");
}

static OptionalText_t format_inline_type(type_ast_t *type, Table_t comments) {
    if (range_has_comment(type->start, type->end, comments)) return NONE_TEXT;
    switch (type->tag) {
    default: {
        Text_t code = Text$from_strn(type->start, (int64_t)(type->end - type->start));
        if (Text$has(code, Text("\n"))) return NONE_TEXT;
        return Text$replace(code, Text("\t"), single_indent);
    }
    }
}

static Text_t format_type(type_ast_t *type, Table_t comments, Text_t indent) {
    (void)comments, (void)indent;
    switch (type->tag) {
    default: {
        OptionalText_t inline_type = format_inline_type(type, comments);
        if (inline_type.length >= 0) return inline_type;
        Text_t code = Text$from_strn(type->start, (int64_t)(type->end - type->start));
        return Text$replace(code, Text("\t"), single_indent);
    }
    }
}

static OptionalText_t format_inline_arg(arg_ast_t *arg, Table_t comments) {
    if (range_has_comment(arg->start, arg->end, comments)) return NONE_TEXT;
    if (arg->name == NULL && arg->value) return must(format_inline_code(arg->value, comments));
    Text_t code = Text$from_str(arg->name);
    if (arg->type) code = Texts(code, ":", must(format_inline_type(arg->type, comments)));
    if (arg->value) code = Texts(code, " = ", must(format_inline_code(arg->value, comments)));
    return code;
}

static Text_t format_arg(arg_ast_t *arg, Table_t comments, Text_t indent) {
    OptionalText_t inline_arg = format_inline_arg(arg, comments);
    if (inline_arg.length >= 0 && inline_arg.length <= MAX_WIDTH) return inline_arg;
    if (arg->name == NULL && arg->value) return format_code(arg->value, comments, indent);
    Text_t code = Text$from_str(arg->name);
    if (arg->type) code = Texts(code, ":", format_type(arg->type, comments, indent));
    if (arg->value) code = Texts(code, " = ", format_code(arg->value, comments, indent));
    return code;
}

static OptionalText_t format_inline_args(arg_ast_t *args, Table_t comments) {
    Text_t code = EMPTY_TEXT;
    for (; args; args = args->next) {
        if (args->name && args->next && args->type == args->next->type && args->value == args->next->value) {
            code = Texts(code, Text$from_str(args->name), ",");
        } else {
            code = Texts(code, must(format_inline_arg(args, comments)));
            if (args->next) code = Texts(code, ", ");
        }
        if (args->next && range_has_comment(args->end, args->next->start, comments)) return NONE_TEXT;
    }
    return code;
}

static Text_t format_args(arg_ast_t *args, Table_t comments, Text_t indent) {
    OptionalText_t inline_args = format_inline_args(args, comments);
    if (inline_args.length >= 0 && inline_args.length <= MAX_WIDTH) return inline_args;
    Text_t code = EMPTY_TEXT;
    for (; args; args = args->next) {
        if (args->name && args->next && args->type == args->next->type && args->value == args->next->value) {
            code = Texts(code, Text$from_str(args->name), ",");
        } else {
            add_line(&code, Texts(format_arg(args, comments, indent), ","), indent);
        }
    }
    return code;
}

static OptionalText_t format_inline_tag(tag_ast_t *tag, Table_t comments) {
    if (range_has_comment(tag->start, tag->end, comments)) return NONE_TEXT;
    Text_t code = Texts(Text$from_str(tag->name), "(", must(format_inline_args(tag->fields, comments)));
    if (tag->secret) code = Texts(code, "; secret");
    return Texts(code, ")");
}

static Text_t format_tag(tag_ast_t *tag, Table_t comments, Text_t indent) {
    OptionalText_t inline_tag = format_inline_tag(tag, comments);
    if (inline_tag.length >= 0) return inline_tag;
    Text_t code =
        Texts(Text$from_str(tag->name), "(", format_args(tag->fields, comments, Texts(indent, single_indent)));
    if (tag->secret) code = Texts(code, "; secret");
    return Texts(code, ")");
}

static OptionalText_t format_inline_tags(tag_ast_t *tags, Table_t comments) {
    Text_t code = EMPTY_TEXT;
    for (; tags; tags = tags->next) {
        code = Texts(code, must(format_inline_tag(tags, comments)));
        if (tags->next) code = Texts(code, ", ");
        if (tags->next && range_has_comment(tags->end, tags->next->start, comments)) return NONE_TEXT;
    }
    return code;
}

static Text_t format_tags(tag_ast_t *tags, Table_t comments, Text_t indent) {
    OptionalText_t inline_tags = format_inline_tags(tags, comments);
    if (inline_tags.length >= 0) return inline_tags;
    Text_t code = EMPTY_TEXT;
    for (; tags; tags = tags->next) {
        add_line(&code, Texts(format_tag(tags, comments, indent), ","), indent);
    }
    return code;
}

static CONSTFUNC ast_t *unwrap_block(ast_t *ast) {
    if (ast == NULL) return NULL;
    while (ast->tag == Block && Match(ast, Block)->statements && Match(ast, Block)->statements->next == NULL) {
        ast = Match(ast, Block)->statements->ast;
    }
    return ast;
}

static Text_t format_namespace(ast_t *namespace, Table_t comments, Text_t indent) {
    if (unwrap_block(namespace) == NULL) return EMPTY_TEXT;
    return Texts("\n", indent, single_indent, format_code(namespace, comments, Texts(indent, single_indent)));
}

static CONSTFUNC const char *binop_tomo_operator(ast_e tag) {
    switch (tag) {
    case Power: return "^";
    case PowerUpdate: return "^=";
    case Concat: return "++";
    case ConcatUpdate: return "++=";
    case Multiply: return "*";
    case MultiplyUpdate: return "*=";
    case Divide: return "/";
    case DivideUpdate: return "/=";
    case Mod: return "mod";
    case ModUpdate: return "mod=";
    case Mod1: return "mod1";
    case Mod1Update: return "mod1=";
    case Plus: return "+";
    case PlusUpdate: return "+=";
    case Minus: return "-";
    case MinusUpdate: return "-=";
    case LeftShift: return "<<";
    case LeftShiftUpdate: return "<<=";
    case RightShift: return ">>";
    case RightShiftUpdate: return ">>=";
    case And: return "and";
    case AndUpdate: return "and=";
    case Or: return "or";
    case OrUpdate: return "or=";
    case Xor: return "xor";
    case XorUpdate: return "xor=";
    case Equals: return "==";
    case NotEquals: return "!=";
    case LessThan: return "<";
    case LessThanOrEquals: return "<=";
    case GreaterThan: return ">";
    case GreaterThanOrEquals: return ">=";
    default: return NULL;
    }
}

OptionalText_t format_inline_code(ast_t *ast, Table_t comments) {
    if (range_has_comment(ast->start, ast->end, comments)) return NONE_TEXT;
    switch (ast->tag) {
    case Unknown: fail("Invalid AST");
    case Block: {
        ast_list_t *statements = Match(ast, Block)->statements;
        if (statements == NULL) return Text("pass");
        else if (statements->next == NULL) return format_inline_code(statements->ast, comments);
        else return NONE_TEXT;
    }
    case StructDef:
    case EnumDef:
    case LangDef:
    case Extend:
    case FunctionDef:
    case DocTest: return NONE_TEXT;
    case If: {
        DeclareMatch(if_, ast, If);

        if (if_->else_body == NULL && if_->condition->tag != Declare) {
            ast_t *stmt = unwrap_block(if_->body);
            switch (stmt->tag) {
            case Return:
            case Skip:
            case Stop:
                return Texts(must(format_inline_code(stmt, comments)), " if ",
                             must(format_inline_code(if_->condition, comments)));
            default: break;
            }
        }

        Text_t code = Texts("if ", must(format_inline_code(if_->condition, comments)), " then ",
                            must(format_inline_code(if_->body, comments)));
        if (if_->else_body) code = Texts(code, " else ", must(format_inline_code(if_->else_body, comments)));
        return code;
    }
    case Repeat: return Texts("repeat ", must(format_inline_code(Match(ast, Repeat)->body, comments)));
    case While: {
        DeclareMatch(loop, ast, While);
        return Texts("while ", must(format_inline_code(loop->condition, comments)), " do ",
                     must(format_inline_code(loop->body, comments)));
    }
    case List:
    case Set: {
        ast_list_t *items = ast->tag == List ? Match(ast, List)->items : Match(ast, Set)->items;
        Text_t code = EMPTY_TEXT;
        for (ast_list_t *item = items; item; item = item->next) {
            code = Texts(code, must(format_inline_code(item->ast, comments)));
            if (item->next) code = Texts(code, ", ");
        }
        return ast->tag == List ? Texts("[", code, "]") : Texts("|", code, "|");
    }
    case Declare: {
        DeclareMatch(decl, ast, Declare);
        Text_t code = must(format_inline_code(decl->var, comments));
        if (decl->type) code = Texts(code, " : ", must(format_inline_type(decl->type, comments)));
        if (decl->value)
            code =
                Texts(code, decl->type ? Text(" = ") : Text(" := "), must(format_inline_code(decl->value, comments)));
        return code;
    }
    case Assign: {
        DeclareMatch(assign, ast, Assign);
        Text_t code = EMPTY_TEXT;
        for (ast_list_t *target = assign->targets; target; target = target->next) {
            code = Texts(code, must(format_inline_code(target->ast, comments)));
            if (target->next) code = Texts(code, ", ");
        }
        code = Texts(code, " = ");
        for (ast_list_t *value = assign->values; value; value = value->next) {
            code = Texts(code, must(format_inline_code(value->ast, comments)));
            if (value->next) code = Texts(code, ", ");
        }
        return code;
    }
    case Return: {
        ast_t *value = Match(ast, Return)->value;
        return value ? Texts("return ", must(format_inline_code(value, comments))) : Text("return");
    }
    case Optional: {
        ast_t *val = Match(ast, Optional)->value;
        return Texts(must(format_inline_code(val, comments)), "?");
    }
    case NonOptional: {
        ast_t *val = Match(ast, NonOptional)->value;
        return Texts(must(format_inline_code(val, comments)), "!");
    }
    case FieldAccess: {
        DeclareMatch(access, ast, FieldAccess);
        return Texts(must(format_inline_code(access->fielded, comments)), ".", Text$from_str(access->field));
    }
    case Index: {
        DeclareMatch(index, ast, Index);
        if (index->index)
            return Texts(must(format_inline_code(index->indexed, comments)), "[",
                         must(format_inline_code(index->index, comments)), "]");
        else return Texts(must(format_inline_code(index->indexed, comments)), "[]");
    }
    case TextJoin: {
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
                code = Texts(code, "$(", must(format_inline_code(chunk->ast, comments)), ")");
            }
        }
        return Texts(code, quote);
    }
    case TextLiteral: {
        fail("Something went wrong, we shouldn't be formatting text literals directly");
    }
    case Stop: {
        const char *target = Match(ast, Stop)->target;
        return target ? Texts("stop ", Text$from_str(target)) : Text("stop");
    }
    case Skip: {
        const char *target = Match(ast, Skip)->target;
        return target ? Texts("skip ", Text$from_str(target)) : Text("skip");
    }
    case None:
    case Bool:
    case Int:
    case Num:
    case Var: {
        Text_t code = Text$from_strn(ast->start, (int64_t)(ast->end - ast->start));
        if (Text$has(code, Text("\n"))) return NONE_TEXT;
        return code;
    }
    case FunctionCall: {
        DeclareMatch(call, ast, FunctionCall);
        return Texts(must(format_inline_code(call->fn, comments)), "(", must(format_inline_args(call->args, comments)),
                     ")");
    }
    case BINOP_CASES: {
        binary_operands_t operands = BINARY_OPERANDS(ast);
        const char *op = binop_tomo_operator(ast->tag);

        Text_t lhs = must(format_inline_code(operands.lhs, comments));
        Text_t rhs = must(format_inline_code(operands.rhs, comments));

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
    case Unknown: fail("Invalid AST");
    case Block: {
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

            add_line(&code, format_code(stmt->ast, comments, indent), indent);
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
    case If: {
        DeclareMatch(if_, ast, If);
        if (inlined_fits && if_->else_body == NULL) return inlined;

        Text_t code = Texts("if ", format_code(if_->condition, comments, indent), "\n", indent, single_indent,
                            format_code(if_->body, comments, Texts(indent, single_indent)));
        if (if_->else_body)
            code = Texts(code, "\n", indent, "else \n", indent, single_indent,
                         format_code(if_->else_body, comments, Texts(indent, single_indent)));
        return code;
    }
    case Repeat: {
        return Texts("repeat\n", indent, single_indent,
                     format_code(Match(ast, Repeat)->body, comments, Texts(indent, single_indent)));
    }
    case While: {
        DeclareMatch(loop, ast, While);
        return Texts("while ", format_code(loop->condition, comments, indent), "\n", indent, single_indent,
                     format_code(loop->body, comments, Texts(indent, single_indent)));
    }
    case FunctionDef: {
        DeclareMatch(func, ast, FunctionDef);
        // ast_t *name;
        // arg_ast_t *args;
        // type_ast_t *ret_type;
        // ast_t *body;
        // ast_t *cache;
        // bool is_inline;
        Text_t code =
            Texts("func ", format_code(func->name, comments, indent), "(", format_args(func->args, comments, indent));
        if (func->ret_type)
            code = Texts(code, func->args ? Text(" -> ") : Text("-> "), format_type(func->ret_type, comments, indent));
        if (func->cache) code = Texts(code, "; cache=", format_code(func->cache, comments, indent));
        if (func->is_inline) code = Texts(code, "; inline");
        code =
            Texts(code, ")\n", indent, single_indent, format_code(func->body, comments, Texts(indent, single_indent)));
        return Texts(code);
    }
    case StructDef: {
        DeclareMatch(def, ast, StructDef);
        Text_t code = Texts("struct ", Text$from_str(def->name), "(", format_args(def->fields, comments, indent));
        if (def->secret) code = Texts(code, "; secret");
        if (def->external) code = Texts(code, "; external");
        if (def->opaque) code = Texts(code, "; opaque");
        return Texts(code, ")", format_namespace(def->namespace, comments, indent));
    }
    case EnumDef: {
        DeclareMatch(def, ast, EnumDef);
        Text_t code = Texts("enum ", Text$from_str(def->name), "(", format_tags(def->tags, comments, indent));
        return Texts(code, ")", format_namespace(def->namespace, comments, indent));
    }
    case LangDef: {
        DeclareMatch(def, ast, LangDef);
        return Texts("lang ", Text$from_str(def->name), format_namespace(def->namespace, comments, indent));
    }
    case Extend: {
        DeclareMatch(extend, ast, Extend);
        return Texts("lang ", Text$from_str(extend->name), format_namespace(extend->body, comments, indent));
    }
    case List:
    case Set: {
        if (inlined_fits) return inlined;
        ast_list_t *items = ast->tag == List ? Match(ast, List)->items : Match(ast, Set)->items;
        Text_t code = EMPTY_TEXT;
        const char *comment_pos = ast->start;
        for (ast_list_t *item = items; item; item = item->next) {
            for (OptionalText_t comment;
                 (comment = next_comment(comments, &comment_pos, item->ast->start)).length > 0;) {
                add_line(&code, Text$trim(comment, Text(" \t\r\n"), false, true), Texts(indent, single_indent));
            }
            add_line(&code, Texts(format_code(item->ast, comments, Texts(indent, single_indent)), ","),
                     Texts(indent, single_indent));
        }
        for (OptionalText_t comment; (comment = next_comment(comments, &comment_pos, ast->end)).length > 0;) {
            add_line(&code, Text$trim(comment, Text(" \t\r\n"), false, true), Texts(indent, single_indent));
        }
        return ast->tag == List ? Texts("[\n", indent, single_indent, code, "\n", indent, "]")
                                : Texts("|\n", indent, single_indent, code, "\n", indent, "|");
    }
    case Declare: {
        DeclareMatch(decl, ast, Declare);
        Text_t code = format_code(decl->var, comments, indent);
        if (decl->type) code = Texts(code, " : ", format_type(decl->type, comments, indent));
        if (decl->value)
            code = Texts(code, decl->type ? Text(" = ") : Text(" := "), format_code(decl->value, comments, indent));
        return code;
    }
    case Assign: {
        DeclareMatch(assign, ast, Assign);
        Text_t code = EMPTY_TEXT;
        for (ast_list_t *target = assign->targets; target; target = target->next) {
            code = Texts(code, format_code(target->ast, comments, indent));
            if (target->next) code = Texts(code, ", ");
        }
        code = Texts(code, " = ");
        for (ast_list_t *value = assign->values; value; value = value->next) {
            code = Texts(code, format_code(value->ast, comments, indent));
            if (value->next) code = Texts(code, ", ");
        }
        return code;
    }
    case Return: {
        ast_t *value = Match(ast, Return)->value;
        return value ? Texts("return ", format_code(value, comments, indent)) : Text("return");
    }
    case Optional: {
        if (inlined_fits) return inlined;
        ast_t *val = Match(ast, Optional)->value;
        return Texts("(", format_code(val, comments, indent), ")?");
    }
    case NonOptional: {
        if (inlined_fits) return inlined;
        ast_t *val = Match(ast, NonOptional)->value;
        return Texts("(", format_code(val, comments, indent), ")!");
    }
    case FieldAccess: {
        DeclareMatch(access, ast, FieldAccess);
        return Texts(format_code(access->fielded, comments, indent), ".", Text$from_str(access->field));
    }
    case Index: {
        DeclareMatch(index, ast, Index);
        if (index->index)
            return Texts(format_code(index->indexed, comments, indent), "[",
                         format_code(index->index, comments, indent), "]");
        else return Texts(format_code(index->indexed, comments, indent), "[]");
    }
    case TextJoin: {
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
                code = Texts(code, "$(", must(format_inline_code(chunk->ast, comments)), ")");
            }
        }
        add_line(&code, current_line, Texts(indent, single_indent));
        code = Texts(quote, "\n", indent, single_indent, code, "\n", indent, quote);
        if (lang) code = Texts("$", Text$from_str(lang), code);
        return code;
    }
    case TextLiteral: {
        fail("Something went wrong, we shouldn't be formatting text literals directly");
    }
    case Stop:
    case Skip:
    case None:
    case Bool:
    case Int:
    case Num:
    case Var: {
        assert(inlined.length >= 0);
        return inlined;
    }
    case FunctionCall: {
        if (inlined_fits) return inlined;
        DeclareMatch(call, ast, FunctionCall);
        return Texts(format_code(call->fn, comments, indent), "(\n", indent, single_indent,
                     format_args(call->args, comments, Texts(indent, single_indent)), "\n", indent, ")");
    }
    case DocTest: {
        DeclareMatch(test, ast, DocTest);
        Text_t expr = format_code(test->expr, comments, indent);
        Text_t code = Texts(">> ", Text$replace(expr, Texts("\n", indent), Texts("\n", indent, ".. ")));
        if (test->expected) {
            Text_t expected = format_code(test->expected, comments, indent);
            code = Texts(code, "\n", indent, "= ",
                         Text$replace(expected, Texts("\n", indent), Texts("\n", indent, ".. ")));
        }
        return code;
    }
    case BINOP_CASES: {
        if (inlined_fits) return inlined;
        binary_operands_t operands = BINARY_OPERANDS(ast);
        const char *op = binop_tomo_operator(ast->tag);
        Text_t lhs = format_code(operands.lhs, comments, indent);
        Text_t rhs = format_code(operands.rhs, comments, indent);

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
    code = Texts(code, format_code(ast, ctx.comments, EMPTY_TEXT));
    for (OptionalText_t comment; (comment = next_comment(ctx.comments, &fmt_pos, ast->start)).length > 0;) {
        code = Texts(code, Text$trim(comment, Text(" \t\r\n"), false, true), "\n");
    }
    return code;
}
