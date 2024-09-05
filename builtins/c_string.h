#pragma once

// Type info and methods for CString datatype, which represents C's `char*`

#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

#include "types.h"

Text_t CString$as_text(const void *str, bool colorize, const TypeInfo *info);
int CString$compare(const char **x, const char **y);
bool CString$equal(const char **x, const char **y);
uint64_t CString$hash(const char **str);

extern const TypeInfo CString$info;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
