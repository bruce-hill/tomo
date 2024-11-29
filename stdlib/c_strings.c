// Type info and methods for CString datatype (char*)
#include <ctype.h>
#include <err.h>
#include <gc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "text.h"
#include "siphash.h"
#include "util.h"

public Text_t CString$as_text(const void *c_string, bool colorize, const TypeInfo_t *info)
{
    (void)info;
    if (!c_string) return Text("CString");
    Text_t text = Text$from_str(*(char**)c_string);
    return Text$concat(colorize ? Text("\x1b[34mCString\x1b[m(") : Text("CString("), Text$quoted(text, colorize), Text(")"));
}

public Text_t CString$as_text_simple(const char *str)
{
    return Text$format("%s", str);
}

PUREFUNC public int32_t CString$compare(const void *x, const void *y, const TypeInfo_t*)
{
    if (x == y)
        return 0;

    if (!*(char**)x != !*(char**)y)
        return (!*(char**)y) - (!*(char**)x);

    return strcmp(*(char**)x, *(char**)y);
}

PUREFUNC public bool CString$equal(const void *x, const void *y, const TypeInfo_t *type)
{
    return CString$compare(x, y, type) == 0;
}

PUREFUNC public uint64_t CString$hash(const void *c_str, const TypeInfo_t*)
{
    if (!*(char**)c_str) return 0;
    return siphash24(*(void**)c_str, strlen(*(char**)c_str));
}

PUREFUNC public bool CString$is_none(const void *c_str, const TypeInfo_t*)
{
    return *(char**)c_str == NULL;
}

static void CString$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t*)
{
    const char *str = *(const char **)obj;
    int64_t len = (int64_t)strlen(str);
    Int64$serialize(&len, out, pointers, &Int64$info);
    fwrite(str, sizeof(char), (size_t)len, out);
}

static void CString$deserialize(FILE *in, void *out, Array_t *pointers, const TypeInfo_t *)
{
    int64_t len = -1;
    Int64$deserialize(in, &len, pointers, &Int64$info);
    char *str = GC_MALLOC_ATOMIC((size_t)len+1);
    fread(str, sizeof(char), (size_t)len, in);
    str[len+1] = '\0';
    *(char**)out = str;
}

public const TypeInfo_t CString$info = {
    .size=sizeof(char*),
    .align=__alignof__(char*),
    .metamethods={
        .hash=CString$hash,
        .compare=CString$compare,
        .equal=CString$equal,
        .as_text=CString$as_text,
        .is_none=CString$is_none,
        .serialize=CString$serialize,
        .deserialize=CString$deserialize,
    },
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
