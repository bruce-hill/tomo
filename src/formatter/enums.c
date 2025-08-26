// Logic for formatting enums and enum tags

#include "../ast.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/optionals.h"
#include "../stdlib/text.h"
#include "args.h"
#include "formatter.h"
#include "types.h"

OptionalText_t format_inline_tag(tag_ast_t *tag, Table_t comments) {
    if (range_has_comment(tag->start, tag->end, comments)) return NONE_TEXT;
    Text_t code = Texts(Text$from_str(tag->name), "(", must(format_inline_args(tag->fields, comments)));
    if (tag->secret) code = Texts(code, "; secret");
    return Texts(code, ")");
}

Text_t format_tag(tag_ast_t *tag, Table_t comments, Text_t indent) {
    OptionalText_t inline_tag = format_inline_tag(tag, comments);
    if (inline_tag.length >= 0) return inline_tag;
    Text_t code =
        Texts(Text$from_str(tag->name), "(", format_args(tag->fields, comments, Texts(indent, single_indent)));
    if (tag->secret) code = Texts(code, "; secret");
    return Texts(code, ")");
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

Text_t format_tags(tag_ast_t *tags, Table_t comments, Text_t indent) {
    OptionalText_t inline_tags = format_inline_tags(tags, comments);
    if (inline_tags.length >= 0) return inline_tags;
    Text_t code = EMPTY_TEXT;
    for (; tags; tags = tags->next) {
        add_line(&code, Texts(format_tag(tags, comments, indent), ","), indent);
    }
    return code;
}
