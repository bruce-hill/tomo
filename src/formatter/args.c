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
    OptionalText_t inline_arg = format_inline_arg(arg, comments);
    if (inline_arg.length >= 0 && inline_arg.length <= MAX_WIDTH) return inline_arg;
    if (arg->name == NULL && arg->value) return format_code(arg->value, comments, indent);
    Text_t code = Text$from_str(arg->name);
    if (arg->type) code = Texts(code, ":", format_type(arg->type));
    if (arg->value) code = Texts(code, "=", format_code(arg->value, comments, indent));
    return code;
}

OptionalText_t format_inline_args(arg_ast_t *args, Table_t comments) {
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

Text_t format_args(arg_ast_t *args, Table_t comments, Text_t indent) {
    OptionalText_t inline_args = format_inline_args(args, comments);
    if (inline_args.length >= 0 && inline_args.length <= MAX_WIDTH) return inline_args;
    Text_t code = EMPTY_TEXT;
    for (; args; args = args->next) {
        if (args->name && args->next && args->type == args->next->type && args->value == args->next->value) {
            code = Texts(code, Text$from_str(args->name), ",");
        } else {
            code = Texts(code, "\n", indent, single_indent,
                         format_arg(args, comments, Texts(indent, single_indent, single_indent)), ",");
        }
    }
    return code;
}
