#include "../stdlib/datatypes.h"
#include "../types.h"

Text_t optional_into_nonnone(type_t *t, Text_t value);
Text_t promote_to_optional(type_t *t, Text_t code);
Text_t compile_none(type_t *t);
Text_t check_none(type_t *t, Text_t value);
