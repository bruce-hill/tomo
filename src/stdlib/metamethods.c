// Metamethods are methods that all types share for hashing, equality, comparison, and textifying

#include <stdint.h>
#include <string.h>

#include "lists.h"
#include "metamethods.h"
#include "siphash.h"
#include "tables.h"
#include "types.h"
#include "util.h"

PUREFUNC public uint64_t generic_hash(const void *obj, const TypeInfo_t *type) {
    if (type->metamethods.hash) return type->metamethods.hash(obj, type);

    return siphash24((void *)obj, (size_t)(type->size));
}

PUREFUNC public int32_t generic_compare(const void *x, const void *y, const TypeInfo_t *type) {
    if (x == y) return 0;

    if (type->metamethods.compare) return type->metamethods.compare(x, y, type);

    return (int32_t)memcmp((void *)x, (void *)y, (size_t)(type->size));
}

PUREFUNC public bool generic_equal(const void *x, const void *y, const TypeInfo_t *type) {
    if (x == y) return true;

    if (type->metamethods.equal) return type->metamethods.equal(x, y, type);

    return (generic_compare(x, y, type) == 0);
}

public
Text_t generic_as_text(const void *obj, bool colorize, const TypeInfo_t *type) {
    if (!type->metamethods.as_text) fail("No text metamethod provided for type!");

    return type->metamethods.as_text(obj, colorize, type);
}

public
void _serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type) {
    if (type->metamethods.serialize) return type->metamethods.serialize(obj, out, pointers, type);

    fwrite(obj, (size_t)type->size, 1, out);
}

public
List_t generic_serialize(const void *x, const TypeInfo_t *type) {
    char *buf = NULL;
    size_t size = 0;
    FILE *stream = open_memstream(&buf, &size);
    Table_t pointers = EMPTY_TABLE;
    _serialize(x, stream, &pointers, type);
    fclose(stream);
    List_t bytes = {
        .data = GC_MALLOC_ATOMIC(size),
        .length = (uint64_t)size,
        .stride = 1,
        .atomic = 1,
    };
    memcpy(bytes.data, buf, size);
    free(buf);
    return bytes;
}

public
void _deserialize(FILE *input, void *outval, List_t *pointers, const TypeInfo_t *type) {
    if (type->metamethods.deserialize) {
        type->metamethods.deserialize(input, outval, pointers, type);
        return;
    }

    if (fread(outval, (size_t)type->size, 1, input) != 1) fail("Not enough data in stream to deserialize");
}

public
void generic_deserialize(List_t bytes, void *outval, const TypeInfo_t *type) {
    if (bytes.stride != 1) List$compact(&bytes, 1);

    FILE *input = fmemopen(bytes.data, (size_t)bytes.length, "r");
    List_t pointers = EMPTY_LIST;
    _deserialize(input, outval, &pointers, type);
    fclose(input);
}

public
int generic_print(const void *obj, bool colorize, const TypeInfo_t *type) {
    Text_t text = generic_as_text(obj, colorize, type);
    return Text$print(stdout, text) + fputc('\n', stdout);
}

__attribute__((noreturn)) public
void cannot_serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type) {
    (void)obj, (void)out, (void)pointers;
    Text_t typestr = generic_as_text(NULL, false, type);
    fail("Values of type ", typestr, " cannot be serialized or deserialized!");
}

__attribute__((noreturn)) public
void cannot_deserialize(FILE *in, void *obj, List_t *pointers, const TypeInfo_t *type) {
    (void)obj, (void)in, (void)pointers;
    Text_t typestr = generic_as_text(NULL, false, type);
    fail("Values of type ", typestr, " cannot be serialized or deserialized!");
}
