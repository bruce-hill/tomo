#define INTX_H__INT_BITS 64
#define I64(i) (int64_t)(i)
#include "intX.h" // IWYU pragma: export
#define NONE_INT64 ((OptionalInt64_t){.has_value = false})
