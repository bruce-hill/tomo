// Logic for formatting arguments and argument lists

#include "../ast.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/optionals.h"
#include "../stdlib/text.h"
#include "formatter.h"
#include "types.h"
#include "utils.h"

OptionalText_t format_inline_arg(arg_ast_t *arg, Table_t comments) {
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
        code = Texts(code, "\n", indent, single_indent);
        while (arg->name && arg->type && arg->next && arg->type == arg->next->type && arg->value == arg->next->value) {
            code = Texts(code, Text$from_str(arg->name), ", ");
            arg = arg->next;
        }
        code = Texts(code, format_arg(arg, comments, Texts(indent, single_indent)), ",");
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
        return Texts(open, format_arg(args, comments, indent), close);
    }

    return Texts(open, format_args(args, comments, indent), "\n", indent, close);
}

Text_t format_fncall(arg_ast_t *args, Table_t comments, Text_t indent) {
    return format_delimited_args(args, comments, indent, "(", ")");
}

Text_t format_record_literal(arg_ast_t *args, Table_t comments, Text_t indent) {
    return format_delimited_args(args, comments, indent, "{", "}");
}
