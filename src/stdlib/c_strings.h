// Type info and methods for CString datatype, which represents C's `char*`

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "types.h"
#include "util.h"

Text_t CString$as_text(const char **str, bool colorize, const TypeInfo_t *info);
List_t CString$bytes(const char *str);
PUREFUNC int CString$compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool CString$equal(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC uint64_t CString$hash(const void *str, const TypeInfo_t *type);
PUREFUNC bool CString$is_none(const void *c_str, const TypeInfo_t *info);
const char *CString$join(const char *glue, List_t strings);
void CString$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *info);
void CString$deserialize(FILE *in, void *out, List_t *pointers, const TypeInfo_t *info);

extern const TypeInfo_t CString$info;
