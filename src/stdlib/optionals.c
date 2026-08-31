// Optional types

#include <math.h>

#include "bools.h"
#include "datatypes.h"
#include "floats.h"
#include "metamethods.h"
#include "optionals.h"
#include "text.h"
#include "util.h"

public
PUREFUNC bool is_none(const void *obj, const TypeInfo_t *non_optional_type) {
    if (non_optional_type->metamethods.is_none) return non_optional_type->metamethods.is_none(obj, non_optional_type);

    const bool *has_value = (const bool *)(obj + non_optional_type->size);
    return !(*has_value);
}

public
void set_none(void *obj, const TypeInfo_t *optional_type) {
    const TypeInfo_t *nonnull = optional_type->OptionalInfo.type;
    // Zeroed bytes are `none` for most types (empty list/table data, a NULL
    // pointer, a zero enum tag), and for the types with no in-band `none` at
    // all they clear the has_value byte that stands in for one. See is_none().
    memset(obj, 0, (size_t)optional_type->size);
    // The few whose `none` is a specific bit pattern write it themselves:
    if (nonnull->metamethods.set_none) nonnull->metamethods.set_none(obj, nonnull);
}

PUREFUNC public uint64_t Optional$hash(const void *obj, const TypeInfo_t *type) {
    return is_none(obj, type->OptionalInfo.type) ? 0 : generic_hash(obj, type->OptionalInfo.type);
}

PUREFUNC public int32_t Optional$compare(const void *x, const void *y, const TypeInfo_t *type) {
    if (x == y) return 0;
    bool x_is_null = is_none(x, type->OptionalInfo.type);
    bool y_is_null = is_none(y, type->OptionalInfo.type);
    if (x_is_null && y_is_null) return 0;
    else if (x_is_null != y_is_null) return (int32_t)y_is_null - (int32_t)x_is_null;
    else return generic_compare(x, y, type->OptionalInfo.type);
}

PUREFUNC public bool Optional$equal(const void *x, const void *y, const TypeInfo_t *type) {
    if (x == y) return true;

    bool x_is_null = is_none(x, type->OptionalInfo.type);
    bool y_is_null = is_none(y, type->OptionalInfo.type);
    if (x_is_null && y_is_null) return true;
    else if (x_is_null != y_is_null) return false;
    else return generic_equal(x, y, type->OptionalInfo.type);
}

public
Text_t Optional$as_text(const void *obj, bool colorize, const TypeInfo_t *type) {
    if (!obj) return Text$concat(generic_as_text(obj, colorize, type->OptionalInfo.type), Text("?"));

    if (is_none(obj, type->OptionalInfo.type)) return colorize ? Text("\x1b[31mnone\x1b[m") : Text("none");
    return generic_as_text(obj, colorize, type->OptionalInfo.type);
}

public
void Optional$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type) {
    bool has_value = !is_none(obj, type->OptionalInfo.type);
    assert(fputc((int)has_value, out) != EOF);
    if (has_value) _serialize(obj, out, pointers, type->OptionalInfo.type);
}

public
void Optional$deserialize(FILE *in, void *outval, List_t *pointers, const TypeInfo_t *type) {
    int flag = fgetc(in);
    if (flag != 0 && flag != 1) deserialization_failed();
    bool has_value = (bool)flag;
    const TypeInfo_t *nonnull = type->OptionalInfo.type;
    memset(outval, 0, (size_t)type->size);
    if (has_value) {
        _deserialize(in, outval, pointers, nonnull);
        // Types with no `is_none` metamethod (structs, sized ints, bytes)
        // carry an explicit `has_value` flag right after the value, which
        // the value's own deserializer doesn't write. See `is_none()`.
        if (!nonnull->metamethods.is_none) *(bool *)(outval + nonnull->size) = true;
    } else {
        set_none(outval, type);
    }
}
