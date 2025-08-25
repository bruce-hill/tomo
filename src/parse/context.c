// A context parameter that gets passed around during parsing.

#include "../stdlib/memory.h"
#include "../stdlib/pointers.h"
#include "../stdlib/tables.h"
#include "../stdlib/types.h"

TypeInfo_t *parse_comments_info = Table$info(Pointer$info("@", &Memory$info), Pointer$info("@", &Memory$info));
