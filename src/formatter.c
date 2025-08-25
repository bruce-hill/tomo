// This code defines functions for transforming ASTs back into Tomo source text

#include <gc.h>
#include <setjmp.h>

#include "ast.h"
#include "formatter.h"
#include "parse/context.h"
#include "parse/files.h"
#include "parse/utils.h"
#include "stdlib/datatypes.h"
#include "stdlib/optionals.h"
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
    Text_t code = Text$from_str(arg->name);
    if (arg->type) code = Texts(code, ":", must(format_inline_type(arg->type, comments)));
    if (arg->value) code = Texts(code, " = ", must(format_inline_code(arg->value, comments)));
    return code;
}

static Text_t format_arg(arg_ast_t *arg, Table_t comments, Text_t indent) {
    (void)comments;
    Text_t code = Text$from_str(arg->name);
    if (arg->type) code = Texts(code, ":", format_type(arg->type, comments, indent));
    if (arg->value) code = Texts(code, " = ", format_code(arg->value, comments, indent));
    return code;
}

static OptionalText_t format_inline_args(arg_ast_t *args, Table_t comments) {
    Text_t code = EMPTY_TEXT;
    for (; args; args = args->next) {
        if (args->next && args->type == args->next->type && args->value == args->next->value) {
            code = Texts(code, must(Text$from_str(args->name)), ",");
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
    if (inline_args.length >= 0) return inline_args;
    Text_t code = EMPTY_TEXT;
    for (; args; args = args->next) {
        if (args->next && args->type == args->next->type && args->value == args->next->value) {
            code = Texts(code, must(Text$from_str(args->name)), ",");
        } else {
            add_line(&code, Texts(format_arg(args, comments, indent), ","), indent);
        }
    }
    return code;
}

ast_t *unwrap_block(ast_t *ast) {
    while (ast->tag == Block && Match(ast, Block)->statements && Match(ast, Block)->statements->next == NULL) {
        ast = Match(ast, Block)->statements->ast;
    }
    return ast;
}

OptionalText_t format_inline_code(ast_t *ast, Table_t comments) {
    if (range_has_comment(ast->start, ast->end, comments)) return NONE_TEXT;
    switch (ast->tag) {
    case Block: {
        ast_list_t *statements = Match(ast, Block)->statements;
        if (statements == NULL) return Text("pass");
        else if (statements->next == NULL) return format_inline_code(statements->ast, comments);
        else return NONE_TEXT;
    }
    case FunctionDef: return NONE_TEXT;
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
    default: {
        Text_t code = Text$from_strn(ast->start, (int64_t)(ast->end - ast->start));
        if (Text$has(code, Text("\n"))) return NONE_TEXT;
        return code;
    }
    }
}

Text_t format_code(ast_t *ast, Table_t comments, Text_t indent) {
    OptionalText_t inlined = format_inline_code(ast, comments);
    bool inlined_fits = (inlined.length >= 0 && indent.length + inlined.length <= MAX_WIDTH);

    switch (ast->tag) {
    case Block: {
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

            add_line(&code, format_code(stmt->ast, comments, indent), indent);
            comment_pos = stmt->ast->end;

            if (should_have_blank_line(stmt->ast) && stmt->next) add_line(&code, Text(""), indent);
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
        code = Texts(code, ")\n", single_indent, format_code(func->body, comments, Texts(indent, single_indent)));
        return Texts(code, "\n");
    }
    default: {
        if (inlined_fits) return inlined;
        Text_t code = Text$from_strn(ast->start, (int64_t)(ast->end - ast->start));
        return Text$replace(code, Text("\t"), single_indent);
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
