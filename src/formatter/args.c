// Logic for formatting arguments and argument lists

#include "../ast.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/optionals.h"
#include "../stdlib/text.h"
#include "formatter.h"
#include "types.h"
#include "utils.h"

// Whether the final line of `code` is indented further than `indent`, i.e. the
// text ends inside an indented block rather than back at its own level.
PUREFUNC static bool ends_deeper_than(Text_t code, Text_t indent) {
    List_t lines = Text$lines(code);
    if (lines.length <= 1) return false;
    Text_t last = *(Text_t *)(lines.data + ((int64_t)lines.length - 1) * lines.stride);
    Text_t body = Text$trim(last, Text(" \t"), true, false);
    return (int64_t)last.length - (int64_t)body.length > (int64_t)indent.length;
}

OptionalText_t format_inline_arg(arg_ast_t *arg, Table_t comments) {
    // A comment sitting in front of this argument (parse_args() hands it over
    // in `arg->comment`) can only be written on a line of its own, so this
    // argument list has to go multi-line rather than silently drop it.
    if (arg->comment.length > 0) return NONE_TEXT;
    if (range_has_comment(arg->start, arg->end, comments)) return NONE_TEXT;
    if (arg->name == NULL && arg->value) return must(format_inline_code(arg->value, comments));
    Text_t code = Text$from_str(arg->name);
    if (arg->type) code = Texts(code, ":", must(format_type(arg->type)));
    if (arg->value) code = Texts(code, "=", must(format_inline_code(arg->value, comments)));
    return code;
}

Text_t format_arg(arg_ast_t *arg, Table_t comments, Text_t indent) {
    if (!arg->value || !requires_multiline(arg->value)) {
        OptionalText_t inline_arg = format_inline_arg(arg, comments);
        if (inline_arg.tag != TEXT_NONE && inline_arg.length <= MAX_WIDTH) return inline_arg;
    }
    if (arg->name == NULL && arg->value) return format_code(arg->value, comments, indent);
    Text_t code = Text$from_str(arg->name);
    if (arg->type) code = Texts(code, ":", format_type(arg->type));
    if (arg->value) code = Texts(code, "=", format_code(arg->value, comments, indent));
    return code;
}

OptionalText_t format_inline_args(arg_ast_t *args, Table_t comments) {
    Text_t code = EMPTY_TEXT;
    for (arg_ast_t *arg = args; arg; arg = arg->next) {
        if (arg->name && arg->next && arg->type == arg->next->type && arg->value == arg->next->value) {
            code = Texts(code, Text$from_str(arg->name), ",");
        } else {
            code = Texts(code, must(format_inline_arg(arg, comments)));
            if (arg->next) code = Texts(code, ", ");
        }
        if (arg->next && range_has_comment(arg->end, arg->next->start, comments)) return NONE_TEXT;
    }
    return code;
}

Text_t format_args(arg_ast_t *args, Table_t comments, Text_t indent) {
    bool multiline_required = false;
    for (arg_ast_t *arg = args; arg && !multiline_required; arg = arg->next) {
        if (arg->value) {
            multiline_required = requires_multiline(arg->value);
        }
    }

    if (!multiline_required) {
        OptionalText_t inline_args = format_inline_args(args, comments);
        if (inline_args.tag != TEXT_NONE && inline_args.length <= MAX_WIDTH) return inline_args;
    }

    Text_t code = EMPTY_TEXT;
    for (arg_ast_t *arg = args; arg; arg = arg->next) {
        // Arguments that share a type and default (`x, y: Int`) are written on
        // one line, so their comments are gathered onto the line above it:
        Text_t comment = arg->comment;
        Text_t names = EMPTY_TEXT;
        while (arg->name && arg->type && arg->next && arg->type == arg->next->type && arg->value == arg->next->value) {
            names = Texts(names, Text$from_str(arg->name), ", ");
            arg = arg->next;
            if (arg->comment.length > 0)
                comment = comment.length > 0 ? Texts(comment, " ", arg->comment) : arg->comment;
        }
        code = Texts(code, "\n", indent, single_indent);
        if (comment.length > 0) code = Texts(code, "# ", comment, "\n", indent, single_indent);
        code = Texts(code, names);
        Text_t arg_indent = Texts(indent, single_indent);
        Text_t arg_code = format_arg(arg, comments, arg_indent);
        // The separating comma goes on the same line as the end of the
        // argument, which only works if that line is the argument's own level;
        // when the argument ends inside an indented block, the newline is the
        // separator instead.
        code = Texts(code, arg_code, ends_deeper_than(arg_code, arg_indent) ? EMPTY_TEXT : Text(","));
    }
    return code;
}

// Shared by parenthesized calls and braced record literals, which differ only
// in their delimiters.
static Text_t format_delimited_args(arg_ast_t *args, Table_t comments, Text_t indent, const char *open,
                                    const char *close) {
    bool multiline_required = false;
    for (arg_ast_t *arg = args; arg && !multiline_required; arg = arg->next) {
        if (arg->value) {
            multiline_required = requires_multiline(arg->value);
        }
    }

    if (!multiline_required) {
        OptionalText_t inline_args = format_inline_args(args, comments);
        if (inline_args.tag != TEXT_NONE && inline_args.length <= MAX_WIDTH) return Texts(open, inline_args, close);
    }

    if (args && args->next == NULL) {
        Text_t arg_code = format_arg(args, comments, indent);
        // A lone argument normally hugs the delimiters. It can't when its last
        // line sits deeper than the call itself (a lambda body, an `if`): a
        // closing paren tacked onto the end of an indented block reads as a
        // second statement on that line and doesn't parse.
        if (!ends_deeper_than(arg_code, indent)) return Texts(open, arg_code, close);
    }

    return Texts(open, format_args(args, comments, indent), "\n", indent, close);
}

Text_t format_fncall(arg_ast_t *args, Table_t comments, Text_t indent) {
    return format_delimited_args(args, comments, indent, "(", ")");
}

Text_t format_record_literal(arg_ast_t *args, Table_t comments, Text_t indent) {
    return format_delimited_args(args, comments, indent, "{", "}");
}
