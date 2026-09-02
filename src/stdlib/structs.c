// Metamethods for structs

#include <stdint.h>
#include <string.h>

#include "bool.h"
#include "metamethods.h"
#include "siphash.h"
#include "structs.h"
#include "text.h"
#include "util.h"

// In a `packed_bools` struct, a `Bool` field takes one bit and a `Bool?` field
// takes two (`no`, `yes`, and NONE_BOOL=2 all fit), so those fields have no
// byte of their own to point at. Every other field is byte-addressed at its
// natural alignment, exactly as it would be without the flag.
//
// Walking that layout has to mirror what the C compiler does with the `:1` and
// `:2` bitfields compile_struct_header() emits, which is two rules: a
// bit-packed field never straddles a byte boundary, and a partially-filled
// byte is finished off before the next byte-addressed field begins.
typedef struct {
    ptrdiff_t byte_offset;
    int bit_offset;
} layout_cursor_t;

// Where one field lives. `bits` is 0 for a byte-addressed field, in which case
// only `byte_offset` matters.
typedef struct {
    ptrdiff_t byte_offset;
    int bit_offset, bits;
} field_location_t;

PUREFUNC static int packed_bit_width(NamedType_t field, bool is_packed_bools) {
    if (!is_packed_bools) return 0;
    if (field.type == &Bool$info) return 1;
    if (field.type->tag == OptionalInfo && field.type->OptionalInfo.type == &Bool$info) return 2;
    return 0;
}

// Locate `field` and leave the cursor pointing just past it.
static field_location_t next_field(layout_cursor_t *cursor, NamedType_t field, bool is_packed_bools) {
    int bits = packed_bit_width(field, is_packed_bools);
    if (bits > 0) {
        if (cursor->bit_offset + bits > 8) { // Bit-packed fields don't straddle bytes
            cursor->byte_offset += 1;
            cursor->bit_offset = 0;
        }
        field_location_t loc = {.byte_offset = cursor->byte_offset, .bit_offset = cursor->bit_offset, .bits = bits};
        cursor->bit_offset += bits;
        if (cursor->bit_offset >= 8) {
            cursor->byte_offset += 1;
            cursor->bit_offset = 0;
        }
        return loc;
    }

    if (cursor->bit_offset > 0) {
        cursor->byte_offset += 1;
        cursor->bit_offset = 0;
    }
    if (field.type->align && cursor->byte_offset % field.type->align > 0)
        cursor->byte_offset += field.type->align - (cursor->byte_offset % field.type->align);

    field_location_t loc = {.byte_offset = cursor->byte_offset, .bit_offset = 0, .bits = 0};
    cursor->byte_offset += field.type->size;
    return loc;
}

// Read a bit-packed field into a byte-sized value. A one-bit `Bool` comes back
// as 0 or 1 and a two-bit `Bool?` as 0, 1, or NONE_BOOL, which is what
// `Bool$info` and `Optional$info(.., &Bool$info)` expect to be handed.
PUREFUNC static uint8_t read_packed(const void *obj, field_location_t loc) {
    uint8_t byte = *(const uint8_t *)(obj + loc.byte_offset);
    return (uint8_t)((byte >> loc.bit_offset) & ((1u << loc.bits) - 1u));
}

static void write_packed(void *obj, field_location_t loc, uint8_t value) {
    uint8_t *byte = (uint8_t *)(obj + loc.byte_offset);
    int shift = loc.bit_offset;
    // Zero the byte as it is first written to. Only the field's own bits are
    // touched below, and `obj` is caller-supplied storage that may hold stale
    // ones -- Table$deserialize reuses a single stack buffer for every entry --
    // so this is what keeps the bits nobody owns deterministically zero.
    if (loc.bit_offset == 0) *byte = 0;
    uint8_t mask = (uint8_t)(((1u << loc.bits) - 1u) << shift);
    *byte = (uint8_t)((*byte & (uint8_t)~mask) | ((uint8_t)(value << shift) & mask));
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wstack-protector"
PUREFUNC public uint64_t Struct$hash(const void *obj, const TypeInfo_t *type) {
    if (type->StructInfo.num_fields == 0) return 0;

    // A lone field is the whole struct, so its own hash will do -- unless it is
    // bit-packed, where the bits it doesn't own would come along for the ride.
    if (type->StructInfo.num_fields == 1 && !type->StructInfo.is_packed_bools)
        return generic_hash(obj, type->StructInfo.fields[0].type);

    uint64_t field_hashes[type->StructInfo.num_fields];
    layout_cursor_t cursor = {0, 0};
    for (int i = 0; i < type->StructInfo.num_fields; i++) {
        NamedType_t field = type->StructInfo.fields[i];
        field_location_t loc = next_field(&cursor, field, type->StructInfo.is_packed_bools);
        if (loc.bits > 0) {
            uint8_t value = read_packed(obj, loc);
            field_hashes[i] = generic_hash(&value, field.type);
        } else {
            field_hashes[i] = generic_hash(obj + loc.byte_offset, field.type);
        }
    }
    return siphash24((void *)field_hashes, sizeof(field_hashes));
}
#pragma clang diagnostic pop

PUREFUNC public uint64_t PackedData$hash(const void *obj, const TypeInfo_t *type) {
    if (type->StructInfo.num_fields == 0) return 0;

    return siphash24(obj, (size_t)type->size);
}

PUREFUNC public int32_t Struct$compare(const void *x, const void *y, const TypeInfo_t *type) {
    if (x == y) return 0;

    layout_cursor_t cursor = {0, 0};
    for (int i = 0; i < type->StructInfo.num_fields; i++) {
        NamedType_t field = type->StructInfo.fields[i];
        field_location_t loc = next_field(&cursor, field, type->StructInfo.is_packed_bools);
        if (loc.bits > 0) {
            uint8_t x_value = read_packed(x, loc), y_value = read_packed(y, loc);
            int32_t cmp = generic_compare(&x_value, &y_value, field.type);
            if (cmp != 0) return cmp;
        } else {
            int32_t cmp = generic_compare(x + loc.byte_offset, y + loc.byte_offset, field.type);
            if (cmp != 0) return cmp;
        }
    }
    return 0;
}

PUREFUNC public bool Struct$equal(const void *x, const void *y, const TypeInfo_t *type) {
    if (x == y) return true;

    layout_cursor_t cursor = {0, 0};
    for (int i = 0; i < type->StructInfo.num_fields; i++) {
        NamedType_t field = type->StructInfo.fields[i];
        field_location_t loc = next_field(&cursor, field, type->StructInfo.is_packed_bools);
        if (loc.bits > 0) {
            uint8_t x_value = read_packed(x, loc), y_value = read_packed(y, loc);
            if (!generic_equal(&x_value, &y_value, field.type)) return false;
        } else {
            if (!generic_equal(x + loc.byte_offset, y + loc.byte_offset, field.type)) return false;
        }
    }
    return true;
}

PUREFUNC public bool PackedData$equal(const void *x, const void *y, const TypeInfo_t *type) {
    if (x == y) return true;
    return (memcmp(x, y, (size_t)type->size) == 0);
}

PUREFUNC public Text_t Struct$as_text(const void *obj, bool colorize, const TypeInfo_t *type) {
    if (!obj) return Text$from_str(type->StructInfo.name);

    Text_t name = Text$from_str(type->StructInfo.name);
    if (type->StructInfo.is_secret || type->StructInfo.is_opaque) {
        return colorize ? Text$concat(Text("\x1b[0;1m"), name, Text("\x1b[m{...}")) : Text$concat(name, Text("{...}"));
    }

    Text_t text = colorize ? Text$concat(Text("\x1b[0;1m"), name, Text("\x1b[m{")) : Text$concat(name, Text("{"));
    layout_cursor_t cursor = {0, 0};
    for (int i = 0; i < type->StructInfo.num_fields; i++) {
        NamedType_t field = type->StructInfo.fields[i];
        if (i > 0) text = Text$concat(text, Text(", "));

        if (type->StructInfo.num_fields > 1) text = Text$concat(text, Text$from_str(field.name), Text("="));

        field_location_t loc = next_field(&cursor, field, type->StructInfo.is_packed_bools);
        if (loc.bits > 0) {
            uint8_t value = read_packed(obj, loc);
            text = Text$concat(text, generic_as_text(&value, colorize, field.type));
        } else {
            text = Text$concat(text, generic_as_text(obj + loc.byte_offset, colorize, field.type));
        }
    }
    return Text$concat(text, Text("}"));
}

public

void Struct$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type) {
    layout_cursor_t cursor = {0, 0};
    for (int i = 0; i < type->StructInfo.num_fields; i++) {
        NamedType_t field = type->StructInfo.fields[i];
        field_location_t loc = next_field(&cursor, field, type->StructInfo.is_packed_bools);
        if (loc.bits > 0) {
            uint8_t value = read_packed(obj, loc);
            _serialize(&value, out, pointers, field.type);
        } else {
            _serialize(obj + loc.byte_offset, out, pointers, field.type);
        }
    }
}

public
void Struct$deserialize(FILE *in, void *outval, List_t *pointers, const TypeInfo_t *type) {
    layout_cursor_t cursor = {0, 0};
    for (int i = 0; i < type->StructInfo.num_fields; i++) {
        NamedType_t field = type->StructInfo.fields[i];
        field_location_t loc = next_field(&cursor, field, type->StructInfo.is_packed_bools);
        if (loc.bits > 0) {
            uint8_t value = 0;
            _deserialize(in, &value, pointers, field.type);
            write_packed(outval, loc, value);
        } else {
            _deserialize(in, outval + loc.byte_offset, pointers, field.type);
        }
    }
}
