// Logic for formatting arguments and argument lists

#include <string.h>

#include "../ast.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/optionals.h"
#include "../stdlib/text.h"
#include "formatter.h"
#include "types.h"
#include "utils.h"

// A parameter can carry a short alias for the command line (`force|f:Bool`),
// which is part of how the parameter is written, so it has to be printed back
// out with the name: dropping it silently deletes a subcommand's short flag.
static Text_t arg_name(arg_ast_t *arg) {
    Text_t name = Text$from_str(arg->name);
    if (arg->alias) name = Texts(name, "|", Text$from_str(arg->alias));
    return name;
}

OptionalText_t format_inline_arg(arg_ast_t *arg, Table_t comments) {
    // A comment sitting in front of this argument (parse_args() hands it over
    // in `arg->comment`) can only be written on a line of its own, so this
    // argument list has to go multi-line rather than silently drop it.
    if (arg->comment.length > 0 || arg->trailing_comment.length > 0) return NONE_TEXT;
    if (range_has_comment(arg->start, arg->end, comments)) return NONE_TEXT;
    if (arg->name == NULL && arg->value) return must(bounded_inline(arg->value, comments));
    Text_t code = arg_name(arg);
    if (arg->type) code = Texts(code, ":", must(format_type(arg->type)));
    if (arg->value) code = Texts(code, "=", must(bounded_inline(arg->value, comments)));
    return code;
}

Text_t format_arg_at(arg_ast_t *arg, Table_t comments, Text_t indent, int64_t column) {
    OptionalText_t inline_arg = format_inline_arg(arg, comments);
    if (inline_arg.tag != TEXT_NONE && column + inline_arg.length <= MAX_WIDTH) return inline_arg;
    if (arg->name == NULL && arg->value) return bounded_at(arg->value, comments, indent, column);
    Text_t code = arg_name(arg);
    if (arg->type) code = Texts(code, ":", format_type(arg->type));
    if (arg->value) code = Texts(code, "=", bounded_at(arg->value, comments, indent, column + code.length + 1));
    return code;
}

OptionalText_t format_inline_args(arg_ast_t *args, Table_t comments) {
    Text_t code = EMPTY_TEXT;
    for (arg_ast_t *arg = args; arg; arg = arg->next) {
        // Names sharing a type (`x, y:Int`) are separated like any other pair
        // of arguments; only the type they share is held back until the last
        // of them. format_args() below writes them the same way.
        if (arg->name && arg->next && arg->type == arg->next->type && arg->value == arg->next->value) {
            code = Texts(code, arg_name(arg), ", ");
        } else {
            code = Texts(code, must(format_inline_arg(arg, comments)));
            if (arg->next) code = Texts(code, ", ");
        }
        if (arg->next && range_has_comment(arg->end, arg->next->start, comments)) return NONE_TEXT;
    }
    return code;
}

// Whether these arguments are a plain series of values -- a record literal's
// fields written in order, a call's positional arguments -- rather than the
// named, typed, defaulted list a definition takes. A series of values reads
// like a list, so it wraps like one, filling each line: a line per value says
// there is something to say about each, and ten lines of `no,` say it ten
// times.
PUREFUNC static bool is_positional_series(arg_ast_t *args) {
    if (args == NULL || args->next == NULL) return false;
    for (arg_ast_t *arg = args; arg; arg = arg->next) {
        if (arg->name != NULL || arg->type != NULL || arg->value == NULL) return false;
        if (arg->comment.length > 0 || arg->trailing_comment.length > 0) return false;
    }
    return true;
}

Text_t format_args_at(arg_ast_t *args, Table_t comments, Text_t indent, int64_t column) {
    OptionalText_t inline_args = format_inline_args(args, comments);
    if (inline_args.tag != TEXT_NONE && column + inline_args.length <= MAX_WIDTH) return inline_args;

    if (is_positional_series(args)) {
        Text_t code = EMPTY_TEXT;
        Text_t arg_indent = Texts(indent, single_indent);
        bool prev_wrapped = false;
        for (arg_ast_t *arg = args; arg; arg = arg->next) {
            Text_t arg_code = format_arg_at(arg, comments, arg_indent, (int64_t)arg_indent.length);
            bool wrapped = Text$has(arg_code, Text("\n"));
            // As for a list item: an argument that ends inside an indented
            // block is separated by the newline rather than by a comma.
            Text_t comma = ends_deeper_than(arg_code, arg_indent) ? EMPTY_TEXT : Text(",");
            // Neither this argument nor the one before it may have taken more
            // than a line. Filling in after one that did leaves the value
            // trailing off the delimiter that closed it, as in `}, 3 * x,`.
            if (Text$ends_with(code, Text(","), NULL) && !wrapped && !prev_wrapped
                && trailing_line_len(code) + 1 + arg_code.length + 1 <= MAX_WIDTH)
                code = Texts(code, " ", arg_code, comma);
            else code = Texts(code, "\n", arg_indent, arg_code, comma);
            prev_wrapped = wrapped;
        }
        return code;
    }

    Text_t code = EMPTY_TEXT;
    for (arg_ast_t *arg = args; arg; arg = arg->next) {
        // Arguments that share a type and default (`x, y: Int`) are written on
        // one line, so their comments are gathered onto the line above it:
        Text_t comment = arg->comment;
        Text_t names = EMPTY_TEXT;
        while (arg->name && arg->type && arg->next && arg->type == arg->next->type && arg->value == arg->next->value) {
            names = Texts(names, arg_name(arg), ", ");
            arg = arg->next;
            if (arg->comment.length > 0)
                comment = comment.length > 0 ? Texts(comment, " ", arg->comment) : arg->comment;
        }
        code = Texts(code, "\n", indent, single_indent);
        if (comment.length > 0) code = Texts(code, "# ", comment, "\n", indent, single_indent);
        code = Texts(code, names);
        Text_t arg_indent = Texts(indent, single_indent);
        // Names sharing a type sit in front of the argument on its line.
        Text_t arg_code = format_arg_at(arg, comments, arg_indent, arg_indent.length + names.length);
        // The separating comma goes on the same line as the end of the
        // argument, which only works if that line is the argument's own level;
        // when the argument ends inside an indented block, the newline is the
        // separator instead.
        code = Texts(code, arg_code, ends_deeper_than(arg_code, arg_indent) ? EMPTY_TEXT : Text(","));
        if (arg->trailing_comment.length > 0) code = Texts(code, " # ", arg->trailing_comment);
    }
    return code;
}

// Everything between a definition's delimiters: the arguments, the return type
// if it has one, and the flags if it has any.
//
// Whether the arguments fit on one line is a question about all of that, so the
// two of those and both delimiters come out of the same budget: a definition
// can break on its return type or its flags alone, with room to spare on its
// arguments.
//
// When it does break, the return type and the flags each take a line of their
// own. Left on the last argument's line they would sit after the comma that
// separates that argument from the next, and read as more arguments.
Text_t format_bracketed_args(arg_ast_t *args, Text_t ret_type, flag_list_t flags, Text_t tail_comment, Table_t comments,
                             Text_t indent, int64_t column, const char *open, const char *close) {
    // A return type stands apart from the arguments it follows; a flag carries
    // the `;` that separates it already.
    Text_t flat_flags = inline_flags(flags);
    bool gap = ret_type.length > 0 && args != NULL;
    int64_t around = ret_type.length + flat_flags.length + (int64_t)(strlen(open) + strlen(close)) + (gap ? 1 : 0);
    // A comment written here has no line of its own on the one-line form, so
    // carrying one is itself a reason to break -- and the parameters have to
    // break with it, or the first of them is left on the opening delimiter's
    // line with nothing to separate it from the return type below.
    bool commented = Text$has(ret_type, Text("#")) || tail_comment.length > 0;
    Text_t arg_code = format_args_at(args, comments, indent, commented ? MAX_WIDTH : column + around);
    if (Text$has(arg_code, Text("\n")) || commented) {
        Text_t inner_indent = Texts(indent, single_indent);
        if (ret_type.length > 0) arg_code = Texts(arg_code, "\n", inner_indent, ret_type);
        for (int i = 0; i < flags.count; i++)
            arg_code = Texts(arg_code, "\n", inner_indent, "; ", flags.items[i]);
        if (tail_comment.length > 0) arg_code = Texts(arg_code, "\n", inner_indent, tail_comment);
        return Texts(open, arg_code, "\n", indent, close);
    }
    return Texts(open, arg_code, gap ? Text(" ") : EMPTY_TEXT, ret_type, flat_flags, close);
}

// Shared by parenthesized calls and braced record literals, which differ only
// in their delimiters.
static Text_t format_delimited_args(arg_ast_t *args, Table_t comments, Text_t indent, int64_t column, const char *open,
                                    const char *close) {
    // Written on one line this is `open`, the arguments, and `close`, so the
    // two delimiters take a column each. The paths below pass that same budget
    // on, since what they measure against it is the one-line form too.
    OptionalText_t inline_args = format_inline_args(args, comments);
    if (inline_args.tag != TEXT_NONE && column + 2 + inline_args.length <= MAX_WIDTH)
        return Texts(open, inline_args, close);

    // A lone argument normally hugs the delimiters, but not when it carries
    // comments: only format_args() below writes those out, so hugging here
    // would drop them.
    if (args && args->next == NULL && args->comment.length == 0 && args->trailing_comment.length == 0) {
        Text_t arg_code = format_arg_at(args, comments, indent, column + 2);
        // It can't hug either when its last line sits deeper than the call
        // itself (a lambda body, an `if`): a closing paren tacked onto the end
        // of an indented block reads as a second statement on that line and
        // doesn't parse.
        if (!ends_deeper_than(arg_code, indent)) return Texts(open, arg_code, close);
    }

    return Texts(open, format_args_at(args, comments, indent, column + 2), "\n", indent, close);
}

Text_t format_fncall_at(arg_ast_t *args, Table_t comments, Text_t indent, int64_t column) {
    return format_delimited_args(args, comments, indent, column, "(", ")");
}

Text_t format_record_literal_at(arg_ast_t *args, Table_t comments, Text_t indent, int64_t column) {
    return format_delimited_args(args, comments, indent, column, "{", "}");
}
