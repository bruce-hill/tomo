// Metamethods are methods that all types share for hashing, equality, comparison, and textifying

#include <gc.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>

#include "datatypes/typeinfo.h"
#include "fail.h"
#include "lists.h"
#include "metamethods.h"
#include "siphash.h"
#include "tables.h"
#include "text.h"
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
    if (!type->metamethods.as_text) fail_text(Text("No text metamethod provided for type!"));

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

// Deserializers recurse into their nested values, and some encodings let one
// level use as few as two bytes (e.g. an empty table plus a "has a fallback" flag),
// so a small hostile input can otherwise nest deeply enough to cause a stack
// overflow. deserialization_failed() exists to turn that into `none` return value.
// Cap the nesting well below the shallowest stack Tomo runs on (a secondary thread
// on macOS gets 512KB). Data this deeply nested can't be produced by _serialize()
// either, which recurses the same way.
#define MAX_DESERIALIZATION_DEPTH 1000
static _Thread_local int deserialization_depth = 0;

public
void _deserialize(FILE *input, void *outval, List_t *pointers, const TypeInfo_t *type) {
    if (deserialization_depth >= MAX_DESERIALIZATION_DEPTH) deserialization_failed();
    deserialization_depth += 1;

    if (type->metamethods.deserialize) type->metamethods.deserialize(input, outval, pointers, type);
    else if (fread(outval, (size_t)type->size, 1, input) != 1) deserialization_failed();

    deserialization_depth -= 1;
}

// Where to jump when a deserializer discovers that its input isn't a
// well-formed encoding of the type being read. This is set for the duration of
// a `generic_deserialize()` call; outside of one (i.e. if a deserializer is
// invoked directly), a malformed input is still a hard failure.
static _Thread_local jmp_buf *deserialization_failure_handler = NULL;

public
_Noreturn void deserialization_failed(void) {
    if (deserialization_failure_handler) longjmp(*deserialization_failure_handler, 1);
    fail_text(Text("This data could not be deserialized"));
}

// How many bytes are left to be read. Deserializers use this to sanity-check
// length prefixes *before* allocating, so that corrupt data can't ask for an
// enormous allocation.
public
int64_t deserialization_bytes_remaining(FILE *in) {
    long pos = ftell(in);
    if (pos < 0 || fseek(in, 0, SEEK_END) != 0) return INT64_MAX;
    long end = ftell(in);
    if (end < 0 || fseek(in, pos, SEEK_SET) != 0) return INT64_MAX;
    return (int64_t)(end - pos);
}

public
bool generic_deserialize(List_t bytes, void *outval, const TypeInfo_t *type) {
    if (bytes.stride != 1) List$compact(&bytes, 1);

    FILE *input = fmemopen(bytes.data, (size_t)bytes.length, "r");
    if (input == NULL) return false;

    List_t pointers = EMPTY_LIST;
    jmp_buf *prev_handler = deserialization_failure_handler;
    int prev_depth = deserialization_depth;
    jmp_buf on_failure;
    bool success;
    if (setjmp(on_failure) == 0) {
        deserialization_failure_handler = &on_failure;
        _deserialize(input, outval, &pointers, type);
        // Leftover bytes mean this isn't an encoding of this type (or is an
        // encoding of something bigger), so don't call it a success:
        success = (fgetc(input) == EOF);
    } else {
        success = false;
    }
    // A longjmp out of a nested deserializer skips every pending decrement, so
    // restore the counter rather than leaving it stuck high for the next call:
    deserialization_depth = prev_depth;
    deserialization_failure_handler = prev_handler;
    fclose(input);
    return success;
}

__attribute__((noreturn)) public
void cannot_serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type) {
    (void)obj, (void)out, (void)pointers;
    Text_t type_text = generic_as_text(NULL, false, type);
    fail_text(Text$concat(Text("Values of type "), type_text, Text(" cannot be serialized or deserialized!")));
}

__attribute__((noreturn)) public
void cannot_deserialize(FILE *in, void *obj, List_t *pointers, const TypeInfo_t *type) {
    (void)obj, (void)in, (void)pointers;
    Text_t type_text = generic_as_text(NULL, false, type);
    fail_text(Text$concat(Text("Values of type "), type_text, Text(" cannot be serialized or deserialized!")));
}
