// A lang for filesystem paths

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "datatypes.h"
#include "integers.h"
#include "types.h"

Path_t Path$from_str(const char *str);
Path_t Path$from_text(Text_t text);
// int Path$print(FILE *f, Path_t path);
// UNSAFE: this works because each type of path has a .components in the same place
#define Path$components(path) ((path).components)
// END UNSAFE
const char *Path$as_c_string(Path_t path);
#define Path(str) Path$from_str(str)
Path_t Path$_concat(int n, Path_t items[n]);
#define Path$concat(...) Path$_concat((int)sizeof((Path_t[]){__VA_ARGS__}) / sizeof(Path_t), ((Path_t[]){__VA_ARGS__}))
Path_t Path$resolved(Path_t path, Path_t relative_to);
Path_t Path$relative_to(Path_t path, Path_t relative_to);
Path_t Path$expand_home(Path_t path);
bool Path$exists(Path_t path);
bool Path$is_file(Path_t path, bool follow_symlinks);
bool Path$is_directory(Path_t path, bool follow_symlinks);
bool Path$is_pipe(Path_t path, bool follow_symlinks);
bool Path$is_socket(Path_t path, bool follow_symlinks);
bool Path$is_symlink(Path_t path);
bool Path$can_read(Path_t path);
bool Path$can_write(Path_t path);
bool Path$can_execute(Path_t path);
OptionalInt64_t Path$modified(Path_t path, bool follow_symlinks);
OptionalInt64_t Path$accessed(Path_t path, bool follow_symlinks);
OptionalInt64_t Path$changed(Path_t path, bool follow_symlinks);
Result_t Path$write(Path_t path, Text_t text, int permissions);
Result_t Path$write_bytes(Path_t path, List_t bytes, int permissions);
Result_t Path$append(Path_t path, Text_t text, int permissions);
Result_t Path$append_bytes(Path_t path, List_t bytes, int permissions);
OptionalText_t Path$read(Path_t path);
OptionalList_t Path$read_bytes(Path_t path, OptionalInt_t limit);
Result_t Path$set_owner(Path_t path, OptionalText_t owner, OptionalText_t group, bool follow_symlinks);
OptionalText_t Path$owner(Path_t path, bool follow_symlinks);
OptionalText_t Path$group(Path_t path, bool follow_symlinks);
Result_t Path$remove(Path_t path, bool ignore_missing);
Result_t Path$create_directory(Path_t path, int permissions, bool recursive);
List_t Path$children(Path_t path, bool include_hidden);
List_t Path$files(Path_t path, bool include_hidden);
List_t Path$subdirectories(Path_t path, bool include_hidden);
OptionalPath_t Path$unique_directory(Path_t path);
OptionalPath_t Path$write_unique(Path_t path, Text_t text);
OptionalPath_t Path$write_unique_bytes(Path_t path, List_t bytes);
OptionalPath_t Path$parent(Path_t path);
Text_t Path$base_name(Path_t path);
Text_t Path$extension(Path_t path, bool full);
bool Path$has_extension(Path_t path, Text_t extension);
Path_t Path$child(Path_t path, Text_t name);
Path_t Path$sibling(Path_t path, Text_t name);
Path_t Path$with_extension(Path_t path, Text_t extension, bool replace);
Path_t Path$current_dir(void);
Closure_t Path$by_line(Path_t path);
OptionalList_t Path$lines(Path_t path);
List_t Path$glob(Path_t path);

uint64_t Path$hash(const void *obj, const TypeInfo_t *);
int32_t Path$compare(const void *a, const void *b, const TypeInfo_t *type);
bool Path$equal(const void *a, const void *b, const TypeInfo_t *type);
bool Path$equal_values(Path_t a, Path_t b);
Text_t Path$as_text(const void *obj, bool color, const TypeInfo_t *type);
bool Path$is_none(const void *obj, const TypeInfo_t *type);
void Path$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type);
void Path$deserialize(FILE *in, void *obj, List_t *pointers, const TypeInfo_t *type);

extern const TypeInfo_t Path$AbsolutePath$$info;
extern const TypeInfo_t Path$RelativePath$$info;
extern const TypeInfo_t Path$HomePath$$info;
extern const TypeInfo_t Path$info;
