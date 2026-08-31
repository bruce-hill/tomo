// Boolean methods/type info
#include <err.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/param.h>

#include "bools.h"
#include "integers.h"
#include "metamethods.h"
#include "optionals.h"
#include "text.h"
#include "util.h"

PUREFUNC public Text_t Bool$as_text(const void *b, bool colorize, const TypeInfo_t *info) {
    (void)info;
    if (!b) return Text("Bool");
    if (colorize) return *(Bool_t *)b ? Text("\x1b[35myes\x1b[m") : Text("\x1b[35mno\x1b[m");
    else return *(Bool_t *)b ? Text("yes") : Text("no");
}

static bool try_parse(Text_t text, Text_t target, bool target_value, Text_t *remainder, bool *result) {
    static const Text_t lang = Text("C");
    if (text.length < target.length) return false;
    Text_t prefix = Text$to(text, Int$from_int64(target.length));
    if (Text$equal_ignoring_case(prefix, target, lang)) {
        if (remainder) *remainder = Text$from(text, Int$from_int64(target.length + 1));
        else if (text.length > target.length) return false;
        *result = target_value;
        return true;
    } else {
        return false;
    }
}

PUREFUNC public OptionalBool_t Bool$parse(Text_t text, Text_t *remainder) {
    bool result;
    if (try_parse(text, Text("yes"), true, remainder, &result)
        || try_parse(text, Text("true"), true, remainder, &result)
        || try_parse(text, Text("on"), true, remainder, &result) || try_parse(text, Text("1"), true, remainder, &result)
        || try_parse(text, Text("no"), false, remainder, &result)
        || try_parse(text, Text("false"), false, remainder, &result)
        || try_parse(text, Text("off"), false, remainder, &result)
        || try_parse(text, Text("0"), false, remainder, &result))
        return result;
    else return NONE_BOOL;
}

static bool Bool$is_none(const void *b, const TypeInfo_t *info) {
    (void)info;
    return *(OptionalBool_t *)b == NONE_BOOL;
}

static void Bool$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *info) {
    (void)pointers, (void)info;
    fputc(*(bool *)obj ? 1 : 0, out);
}

static void Bool$deserialize(FILE *in, void *outval, List_t *pointers, const TypeInfo_t *info) {
    (void)pointers, (void)info;
    // Only 0 and 1 are booleans. Reading the byte raw would let, e.g., a `2`
    // through, which is the in-memory representation of a `none` `Bool?`.
    int c = fgetc(in);
    if (c != 0 && c != 1) deserialization_failed();
    *(bool *)outval = (bool)c;
}

static void Bool$set_none(void *dest, const TypeInfo_t *type) {
    (void)type;
    *(OptionalBool_t *)dest = NONE_BOOL;
}

public
const TypeInfo_t Bool$info = {
    .size = sizeof(bool),
    .align = __alignof__(bool),
    .metamethods =
        {
            .as_text = Bool$as_text,
            .is_none = Bool$is_none,
            .set_none = Bool$set_none,
            .serialize = Bool$serialize,
            .deserialize = Bool$deserialize,
        },
};
