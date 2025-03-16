#pragma once

// A lang for filesystem paths

#include <stdbool.h>
#include <stdint.h>

#include "types.h"
#include "datatypes.h"
#include "optionals.h"

Path_t Path$from_str(const char *str);
Path_t Path$from_text(Text_t text);
const char *Path$as_c_string(Path_t path);
#define Path(str) Path$from_str(str)
Path_t Path$_concat(int n, Path_t items[n]);
#define Path$concat(...) Path$_concat((int)sizeof((Path_t[]){__VA_ARGS__})/sizeof(Path_t), ((Path_t[]){__VA_ARGS__}))
Path_t Path$resolved(Path_t path, Path_t relative_to);
Path_t Path$relative(Path_t path, Path_t relative_to);
Path_t Path$relative_to(Path_t path, Path_t relative_to);
bool Path$exists(Path_t path);
bool Path$is_file(Path_t path, bool follow_symlinks);
bool Path$is_directory(Path_t path, bool follow_symlinks);
bool Path$is_pipe(Path_t path, bool follow_symlinks);
bool Path$is_socket(Path_t path, bool follow_symlinks);
bool Path$is_symlink(Path_t path);
bool Path$can_read(Path_t path);
bool Path$can_write(Path_t path);
bool Path$can_execute(Path_t path);
OptionalMoment_t Path$modified(Path_t path, bool follow_symlinks);
OptionalMoment_t Path$accessed(Path_t path, bool follow_symlinks);
OptionalMoment_t Path$changed(Path_t path, bool follow_symlinks);
void Path$write(Path_t path, Text_t text, int permissions);
void Path$write_bytes(Path_t path, Array_t bytes, int permissions);
void Path$append(Path_t path, Text_t text, int permissions);
void Path$append_bytes(Path_t path, Array_t bytes, int permissions);
OptionalText_t Path$read(Path_t path);
OptionalArray_t Path$read_bytes(Path_t path, OptionalInt_t limit);
void Path$set_owner(Path_t path, OptionalText_t owner, OptionalText_t group, bool follow_symlinks);
OptionalText_t Path$owner(Path_t path, bool follow_symlinks);
OptionalText_t Path$group(Path_t path, bool follow_symlinks);
void Path$remove(Path_t path, bool ignore_missing);
void Path$create_directory(Path_t path, int permissions);
Array_t Path$children(Path_t path, bool include_hidden);
Array_t Path$files(Path_t path, bool include_hidden);
Array_t Path$subdirectories(Path_t path, bool include_hidden);
Path_t Path$unique_directory(Path_t path);
Path_t Path$write_unique(Path_t path, Text_t text);
Path_t Path$write_unique_bytes(Path_t path, Array_t bytes);
Path_t Path$parent(Path_t path);
Text_t Path$base_name(Path_t path);
Text_t Path$extension(Path_t path, bool full);
Path_t Path$with_component(Path_t path, Text_t component);
Path_t Path$with_extension(Path_t path, Text_t extension, bool replace);
Closure_t Path$by_line(Path_t path);
Array_t Path$glob(Path_t path);

uint64_t Path$hash(const void *obj, const TypeInfo_t*);
int32_t Path$compare(const void *a, const void *b, const TypeInfo_t *type);
bool Path$equal(const void *a, const void *b, const TypeInfo_t *type);
bool Path$equal_values(Path_t a, Path_t b);
Text_t Path$as_text(const void *obj, bool color, const TypeInfo_t *type);
bool Path$is_none(const void *obj, const TypeInfo_t *type);
void Path$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type);
void Path$deserialize(FILE *in, void *obj, Array_t *pointers, const TypeInfo_t *type);

extern const TypeInfo_t Path$info;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0

