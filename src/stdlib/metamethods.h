// Metamethods are methods that all types share:

#pragma once

#include <stdint.h>
#include <stdio.h>

#include "datatypes.h"
#include "types.h"
#include "util.h"

PUREFUNC uint64_t generic_hash(const void *obj, const TypeInfo_t *type);
PUREFUNC int32_t generic_compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool generic_equal(const void *x, const void *y, const TypeInfo_t *type);
Text_t generic_as_text(const void *obj, bool colorize, const TypeInfo_t *type);
void _serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type);
List_t generic_serialize(const void *x, const TypeInfo_t *type);
void _deserialize(FILE *input, void *outval, List_t *pointers, const TypeInfo_t *type);
// Returns whether `bytes` was a well-formed encoding of `type`. On failure,
// `outval` is left in an unspecified state and must not be used.
bool generic_deserialize(List_t bytes, void *outval, const TypeInfo_t *type);
// Called by a deserializer when its input isn't well-formed:
_Noreturn void deserialization_failed(void);
int64_t deserialization_bytes_remaining(FILE *in);
void cannot_serialize(const void *, FILE *, Table_t *, const TypeInfo_t *type);
void cannot_deserialize(FILE *, void *, List_t *, const TypeInfo_t *type);
