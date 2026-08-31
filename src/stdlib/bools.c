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

// Accepts "true", "TRUE", and "True", but not scrambled casings like "tRuE".
// `target` is the lowercase form; the uppercase and capitalized forms are
// derived from it, so all three stay in sync from one literal.
//
// This does its own ASCII comparison rather than calling
// Text$equal_ignoring_case(), which does full Unicode case folding. That would
// link libunistring's case-mapping tables (~32KB of tries, plus special-casing
// and soft-dotted data) into every binary to match seven-bit words, since
// Bool's metamethods keep Bool$parse() reachable. Its language-sensitive rules
// are wrong here anyway: these are keywords, not user text, so Turkish dotless
// "\u0131" should not parse as a `1`.
static bool try_parse(Text_t text, const char *target, uint64_t target_len, bool target_value, Text_t *remainder,
                      bool *result) {
    if (text.length < target_len) return false;
    if (remainder == NULL && text.length > target_len) return false;
    bool lower = true, upper = true, capitalized = true;
    TextIter_t state = NEW_TEXT_ITER_STATE(text);
    for (uint64_t i = 0; i < target_len; i++) {
        int32_t g = Text$get_grapheme_fast(&state, (int64_t)i);
        // A negative grapheme is a multi-codepoint cluster, which never equals
        // a lone ASCII character:
        if (g < 0) return false;
        int32_t lo = (int32_t)target[i];
        int32_t up = (lo >= 'a' && lo <= 'z') ? lo - ('a' - 'A') : lo;
        lower = lower && g == lo;
        upper = upper && g == up;
        capitalized = capitalized && g == (i == 0 ? up : lo);
        if (!lower && !upper && !capitalized) return false;
    }
    if (remainder) *remainder = Text$from(text, Int$from_int64((int64_t)target_len + 1));
    *result = target_value;
    return true;
}

#define TRY_PARSE(str, value) try_parse(text, str, sizeof(str) - 1, value, remainder, &result)

public
OptionalBool_t Bool$parse(Text_t text, Text_t *remainder) {
    bool result;
    if (TRY_PARSE("yes", true) || TRY_PARSE("true", true) || TRY_PARSE("on", true) || TRY_PARSE("1", true)
        || TRY_PARSE("no", false) || TRY_PARSE("false", false) || TRY_PARSE("off", false) || TRY_PARSE("0", false))
        return result;
    else return NONE_BOOL;
}

#undef TRY_PARSE

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
