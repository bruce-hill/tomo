// Type info and methods for CString datatype, which represents C's `char*`

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "types.h"
#include "util.h"

Text_t CString$as_text(const char **str, bool colorize, const TypeInfo_t *info);
PUREFUNC int CString$compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool CString$equal(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC uint64_t CString$hash(const void *str, const TypeInfo_t *type);
const char *CString$join(const char *glue, List_t strings);

extern const TypeInfo_t CString$info;
