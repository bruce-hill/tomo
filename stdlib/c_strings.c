// Type info and methods for CString datatype (char*)
#include <ctype.h>
#include <err.h>
#include <gc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "text.h"
#include "siphash.h"
#include "util.h"

public Text_t CString$as_text(const char **c_string, bool colorize, const TypeInfo_t *info)
{
    (void)info;
    if (!c_string) return Text("CString");
    Text_t text = Text$from_str(*c_string);
    return Text$concat(colorize ? Text("\x1b[34mCString\x1b[m(") : Text("CString("), Text$quoted(text, colorize), Text(")"));
}

public Text_t CString$as_text_simple(const char *str)
{
    return Text$format("%s", str);
}

PUREFUNC public int32_t CString$compare(const char **x, const char **y)
{
    if (x == y)
        return 0;

    if (!*x != !*y)
        return (!*y) - (!*x);

    return strcmp(*x, *y);
}

PUREFUNC public bool CString$equal(const char **x, const char **y)
{
    return CString$compare(x, y) == 0;
}

PUREFUNC public uint64_t CString$hash(const char **c_str)
{
    if (!*c_str) return 0;
    return siphash24((void*)*c_str, strlen(*c_str));
}

public const TypeInfo_t CString$info = {
    .size=sizeof(char*),
    .align=__alignof__(char*),
    .tag=CStringInfo,
    .CustomInfo={.as_text=(void*)CString$as_text, .compare=(void*)CString$compare, .equal=(void*)CString$equal, .hash=(void*)CString$hash},
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
