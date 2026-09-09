// Logic for formatting enums and enum tags

#pragma once

#include "../ast.h"
#include "../stdlib/datatypes.h"

OptionalText_t format_inline_tag(tag_ast_t *tag, Table_t comments);
OptionalText_t format_inline_tags(tag_ast_t *tags, Table_t comments);

// These take the column they start at as their last argument; see utils.h.
Text_t format_tag_at(tag_ast_t *tag, Table_t comments, Text_t indent, int64_t column);
Text_t format_tags_at(tag_ast_t *tags, Table_t comments, Text_t indent, int64_t column);
