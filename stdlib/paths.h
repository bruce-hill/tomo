#pragma once

// A lang for filesystem paths

#include <stdbool.h>
#include <stdint.h>

#include "types.h"
#include "datatypes.h"

#define Path_t Text_t
#define Path(text) ((Path_t)Text(text))
#define Paths(...) Path$_concat(sizeof((Path_t[]){__VA_ARGS__})/sizeof(Path_t), (Path_t[]){__VA_ARGS__})

Path_t Path$cleanup(Path_t path);
Path_t Path$_concat(int n, Path_t items[n]);
#define Path$concat(a, b) Paths(a, Path("/"), b)
PUREFUNC Path_t Path$escape_text(Text_t text);
PUREFUNC Path_t Path$escape_path(Text_t path);
Path_t Path$resolved(Path_t path, Path_t relative_to);
Path_t Path$relative(Path_t path, Path_t relative_to);
bool Path$exists(Path_t path);
bool Path$is_file(Path_t path, bool follow_symlinks);
bool Path$is_directory(Path_t path, bool follow_symlinks);
bool Path$is_pipe(Path_t path, bool follow_symlinks);
bool Path$is_socket(Path_t path, bool follow_symlinks);
bool Path$is_symlink(Path_t path);
void Path$write(Path_t path, Text_t text, int permissions);
void Path$write_bytes(Path_t path, Array_t bytes, int permissions);
void Path$append(Path_t path, Text_t text, int permissions);
void Path$append_bytes(Path_t path, Array_t bytes, int permissions);
OptionalText_t Path$read(Path_t path);
OptionalArray_t Path$read_bytes(Path_t path);
void Path$remove(Path_t path, bool ignore_missing);
void Path$create_directory(Path_t path, int permissions);
Array_t Path$children(Path_t path, bool include_hidden);
Array_t Path$files(Path_t path, bool include_hidden);
Array_t Path$subdirectories(Path_t path, bool include_hidden);
Path_t Path$unique_directory(Path_t path);
Text_t Path$write_unique(Path_t path, Text_t text);
Text_t Path$write_unique_bytes(Path_t path, Array_t bytes);
Path_t Path$parent(Path_t path);
Text_t Path$base_name(Path_t path);
Text_t Path$extension(Path_t path, bool full);
Closure_t Path$by_line(Path_t path);

#define Path$hash Text$hash
#define Path$compare Text$compare
#define Path$equal Text$equal

extern const TypeInfo Path$info;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0

