// Logic for formatting enums and enum tags

#include "../ast.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/optionals.h"
#include "../stdlib/text.h"
#include "args.h"
#include "utils.h"

OptionalText_t format_inline_tag(tag_ast_t *tag, Table_t comments) {
    if (range_has_comment(tag->start, tag->end, comments)) return NONE_TEXT;
    Text_t code = Text$from_str(tag->name);
    if (tag->fields || tag->secret || tag->packed_bools) {
        flag_list_t flags = {};
        add_flag(&flags, tag->secret, Text("secret"));
        add_flag(&flags, tag->packed_bools, Text("packed_bools"));
        code = Texts(code, "{", must(format_inline_args(tag->fields, comments)), inline_flags(flags), "}");
    }
    return code;
}

// One variant. Its fields are a bracketed list like any other, so a variant
// wide enough to need it breaks the same way the struct it resembles does.
Text_t format_tag_at(tag_ast_t *tag, Table_t comments, Text_t indent, int64_t column) {
    OptionalText_t inline_tag = format_inline_tag(tag, comments);
    if (inline_tag.tag != TEXT_NONE && column + inline_tag.length <= MAX_WIDTH) return inline_tag;
    Text_t code = Text$from_str(tag->name);
    if (!tag->fields && !tag->secret && !tag->packed_bools) return code;
    flag_list_t flags = {};
    add_flag(&flags, tag->secret, Text("secret"));
    add_flag(&flags, tag->packed_bools, Text("packed_bools"));
    return Texts(code, format_bracketed_args(tag->fields, EMPTY_TEXT, flags, EMPTY_TEXT, comments, indent,
                                             column + code.length, "{", "}"));
}

OptionalText_t format_inline_tags(tag_ast_t *tags, Table_t comments) {
    Text_t code = EMPTY_TEXT;
    for (; tags; tags = tags->next) {
        code = Texts(code, must(format_inline_tag(tags, comments)));
        if (tags->next) code = Texts(code, ", ");
        if (tags->next && range_has_comment(tags->end, tags->next->start, comments)) return NONE_TEXT;
    }
    return code;
}

Text_t format_tags_at(tag_ast_t *tags, Table_t comments, Text_t indent, int64_t column) {
    OptionalText_t inline_tags = format_inline_tags(tags, comments);
    if (inline_tags.tag != TEXT_NONE && column + inline_tags.length <= MAX_WIDTH) return inline_tags;
    Text_t tag_indent = Texts(indent, single_indent);
    Text_t code = EMPTY_TEXT;
    for (; tags; tags = tags->next)
        code =
            Texts(code, "\n", tag_indent, format_tag_at(tags, comments, tag_indent, (int64_t)tag_indent.length), ",");
    return code;
}
