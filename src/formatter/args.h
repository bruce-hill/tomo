// Logic for formatting arguments and argument lists

#pragma once

#include "../ast.h"
#include "../stdlib/datatypes.h"

OptionalText_t format_inline_arg(arg_ast_t *arg, Table_t comments);
OptionalText_t format_inline_args(arg_ast_t *args, Table_t comments);

// These take the column they start at as their last argument; see utils.h.
Text_t format_arg_at(arg_ast_t *arg, Table_t comments, Text_t indent, int64_t column);
Text_t format_args_at(arg_ast_t *args, Table_t comments, Text_t indent, int64_t column);
Text_t format_bracketed_args(arg_ast_t *args, Text_t ret_type, Text_t flags, Table_t comments, Text_t indent,
                             int64_t column, const char *open, const char *close);
Text_t format_fncall_at(arg_ast_t *args, Table_t comments, Text_t indent, int64_t column);
Text_t format_record_literal_at(arg_ast_t *args, Table_t comments, Text_t indent, int64_t column);

static inline Text_t format_arg(arg_ast_t *arg, Table_t comments, Text_t indent) {
    return format_arg_at(arg, comments, indent, (int64_t)indent.length);
}

static inline Text_t format_args(arg_ast_t *args, Table_t comments, Text_t indent) {
    return format_args_at(args, comments, indent, (int64_t)indent.length);
}

static inline Text_t format_fncall(arg_ast_t *args, Table_t comments, Text_t indent) {
    return format_fncall_at(args, comments, indent, (int64_t)indent.length);
}

static inline Text_t format_record_literal(arg_ast_t *args, Table_t comments, Text_t indent) {
    return format_record_literal_at(args, comments, indent, (int64_t)indent.length);
}
