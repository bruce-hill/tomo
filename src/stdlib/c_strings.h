#pragma once

// Type info and methods for CString datatype, which represents C's `char*`

#include <stdbool.h>
#include <stdint.h>

#include "types.h"
#include "util.h"

Text_t CString$as_text(const char **str, bool colorize, const TypeInfo_t *info);
PUREFUNC int CString$compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool CString$equal(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC uint64_t CString$hash(const void *str, const TypeInfo_t *type);

extern const TypeInfo_t CString$info;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
