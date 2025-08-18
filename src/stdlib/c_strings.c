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

public Text_t CStringヽas_text(const void *c_string, bool colorize, const TypeInfo_t *info)
{
    (void)info;
    if (!c_string) return Text("CString");
    Text_t text = Textヽfrom_str(*(const char**)c_string);
    return Textヽconcat(colorize ? Text("\x1b[34mCString\x1b[m(") : Text("CString("), Textヽquoted(text, colorize, Text("\"")), Text(")"));
}

PUREFUNC public int32_t CStringヽcompare(const void *x, const void *y, const TypeInfo_t *info)
{
    (void)info;
    if (x == y)
        return 0;

    if (!*(const char**)x != !*(const char**)y)
        return (!*(const char**)y) - (!*(const char**)x);

    return strcmp(*(const char**)x, *(const char**)y);
}

PUREFUNC public bool CStringヽequal(const void *x, const void *y, const TypeInfo_t *type)
{
    return CStringヽcompare(x, y, type) == 0;
}

PUREFUNC public uint64_t CStringヽhash(const void *c_str, const TypeInfo_t *info)
{
    (void)info;
    if (!*(const char**)c_str) return 0;
    return siphash24(*(void**)c_str, strlen(*(const char**)c_str));
}

PUREFUNC public bool CStringヽis_none(const void *c_str, const TypeInfo_t *info)
{
    (void)info;
    return *(const char**)c_str == NULL;
}

static void CStringヽserialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *info)
{
    (void)info;
    const char *str = *(const char **)obj;
    int64_t len = (int64_t)strlen(str);
    Int64ヽserialize(&len, out, pointers, &Int64ヽinfo);
    fwrite(str, sizeof(char), (size_t)len, out);
}

static void CStringヽdeserialize(FILE *in, void *out, List_t *pointers, const TypeInfo_t *info)
{
    (void)info;
    int64_t len = -1;
    Int64ヽdeserialize(in, &len, pointers, &Int64ヽinfo);
    char *str = GC_MALLOC_ATOMIC((size_t)len+1);
    if (fread(str, sizeof(char), (size_t)len, in) != (size_t)len)
        fail("Not enough data in stream to deserialize");
    str[len+1] = '\0';
    *(const char**)out = str;
}

public const TypeInfo_t CStringヽinfo = {
    .size=sizeof(const char*),
    .align=__alignof__(const char*),
    .metamethods={
        .hash=CStringヽhash,
        .compare=CStringヽcompare,
        .equal=CStringヽequal,
        .as_text=CStringヽas_text,
        .is_none=CStringヽis_none,
        .serialize=CStringヽserialize,
        .deserialize=CStringヽdeserialize,
    },
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
