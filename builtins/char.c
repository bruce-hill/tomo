
#include <gc.h>
#include <gc/cord.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/param.h>
#include <err.h>

#include "array.h"
#include "string.h"
#include "types.h"

static CORD Char_cord(const char *c, bool colorize, const TypeInfo *type) {
    (void)type;
    CORD cord = 0;
    switch (*c) {
    case '\a': return "\\a";
    case '\b': return "\\b";
    case '\x1b': return "\\e";
    case '\f': return "\\f";
    case '\n': return "\\n";
    case '\t': return "\\t";
    case '\r': return "\\r";
    case '\v': return "\\v";
    case '\\': return "\\\\";
    case '"': return "\\\"";
    default: {
        if (!isprint(*c))
            CORD_sprintf(&cord, "\\x%02X", (int)*c);
        else
            cord = CORD_cat_char(0, *c);
    }
    }
    if (colorize) {
        if (CORD_len(cord) > 1)
            CORD_sprintf(&cord, "\x1b[34m%r\x1b[m", cord);
        else
            CORD_sprintf(&cord, "\x1b[35m%r\x1b[m", cord);
    }
    return cord;
}

// For some reason, the C functions from ctypes.h return integers instead of
// booleans, and what's worse, the integers are not limited to [0-1]. So,
// it's necessary to cast them to bools to clamp them to those values.
#define BOOLIFY(fn) public bool Char__ ## fn(char c) { return (bool)fn(c); }
BOOLIFY(isalnum)
BOOLIFY(isalpha)
BOOLIFY(iscntrl)
BOOLIFY(isdigit)
BOOLIFY(isgraph)
BOOLIFY(islower)
BOOLIFY(isprint)
BOOLIFY(ispunct)
BOOLIFY(isspace)
BOOLIFY(isupper)
BOOLIFY(isxdigit)
BOOLIFY(isascii)
BOOLIFY(isblank)

typedef bool (*char_pred_t)(char);
typedef char (*char_map_t)(char);

public struct {
    TypeInfo type;
    char_pred_t isalnum, isalpha, iscntrl, isdigit, isgraph, islower, isprint, ispunct,
                isspace, isupper, isxdigit, isascii, isblank;
    char_map_t tolower, toupper;
} Char_type = {
    .type={
        .name="Char",
        .size=sizeof(char),
        .align=alignof(char),
        .tag=CustomInfo,
        .CustomInfo={.cord=(void*)Char_cord},
    },
    .isalnum=Char__isalnum, .isalpha=Char__isalpha, .iscntrl=Char__iscntrl, .isdigit=Char__isdigit, .isgraph=Char__isgraph,
    .islower=Char__islower, .isprint=Char__isprint, .ispunct=Char__ispunct, .isspace=Char__isspace, .isupper=Char__isupper,
    .isxdigit=Char__isxdigit, .isascii=Char__isascii, .isblank=Char__isblank,
    .tolower=(char_map_t)(void*)tolower, .toupper=(char_map_t)(void*)toupper,
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
