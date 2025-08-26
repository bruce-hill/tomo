// Logic for formatting types

#include "../ast.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/optionals.h"
#include "../stdlib/text.h"
#include "formatter.h"

OptionalText_t format_inline_type(type_ast_t *type, Table_t comments) {
    if (range_has_comment(type->start, type->end, comments)) return NONE_TEXT;
    switch (type->tag) {
    default: {
        Text_t code = Text$from_strn(type->start, (int64_t)(type->end - type->start));
        if (Text$has(code, Text("\n"))) return NONE_TEXT;
        return Text$replace(code, Text("\t"), single_indent);
    }
    }
}

Text_t format_type(type_ast_t *type, Table_t comments, Text_t indent) {
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
