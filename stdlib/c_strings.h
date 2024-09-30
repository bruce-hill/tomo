#pragma once

// Type info and methods for CString datatype, which represents C's `char*`

#include <stdbool.h>
#include <stdint.h>

#include "types.h"

Text_t CString$as_text(char **str, bool colorize, const TypeInfo_t *info);
Text_t CString$as_text_simple(const char *str);
PUREFUNC int CString$compare(const char **x, const char **y);
PUREFUNC bool CString$equal(const char **x, const char **y);
PUREFUNC uint64_t CString$hash(const char **str);

extern const TypeInfo_t CString$info;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
