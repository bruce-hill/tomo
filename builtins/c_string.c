// Type info and methods for CString datatype (char*)
#include <ctype.h>
#include <err.h>
#include <gc.h>
#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "functions.h"
#include "halfsiphash.h"
#include "text.h"
#include "types.h"
#include "util.h"

public Text_t CString$as_text(const void *c_string, bool colorize, const TypeInfo *info)
{
    (void)info;
    if (!c_string) return Text$from_str("CString");
    Text_t text = Text$from_str(*(char**)c_string);
    return Text$concat(Text$from_str(colorize ? "\x1b[34mCString\x1b[m(" : "CString("), Text$quoted(text, colorize), Text$from_str(")"));
}

public int CString$compare(const char **x, const char **y)
{
    if (!*x != !*y)
        return (!*y) - (!*x);
    return strcmp(*x, *y);
}

public bool CString$equal(const char **x, const char **y)
{
    return CString$compare(x, y) == 0;
}

public uint32_t CString$hash(const char **c_str)
{
    if (!*c_str) return 0;

    uint32_t hash;
    halfsiphash(*c_str, strlen(*c_str), TOMO_HASH_KEY, (uint8_t*)&hash, sizeof(hash));
    return hash;
}

public const TypeInfo $CString = {
    .size=sizeof(char*),
    .align=__alignof__(char*),
    .tag=CustomInfo,
    .CustomInfo={.as_text=(void*)CString$as_text, .compare=(void*)CString$compare, .equal=(void*)CString$equal, .hash=(void*)CString$hash},
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
