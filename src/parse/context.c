// A context parameter that gets passed around during parsing.

#include "../stdlib/datatypes/typeinfo.h"
#include "../stdlib/memory.h"
#include "../stdlib/pointers.h"
#include "../stdlib/table.h"

const TypeInfo_t *parse_comments_info = Table$info(Pointer$info("@", &Memory$info), Pointer$info("@", &Memory$info));
