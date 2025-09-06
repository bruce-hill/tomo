// Logic for formatting enums and enum tags

#pragma once

#include "../ast.h"
#include "../stdlib/datatypes.h"

OptionalText_t format_inline_tag(tag_ast_t *tag, Table_t comments);
Text_t format_tag(tag_ast_t *tag, Table_t comments, Text_t indent);
OptionalText_t format_inline_tags(tag_ast_t *tags, Table_t comments);
Text_t format_tags(tag_ast_t *tags, Table_t comments, Text_t indent);
