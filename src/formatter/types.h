// Logic for formatting types

#pragma once

#include "../ast.h"
#include "../stdlib/datatypes.h"

OptionalText_t format_inline_type(type_ast_t *type, Table_t comments);
Text_t format_type(type_ast_t *type, Table_t comments, Text_t indent);
