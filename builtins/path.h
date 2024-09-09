#pragma once

// A lang for filesystem paths

#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

#include "types.h"
#include "datatypes.h"

#define Path_t Text_t
#define Path(text) ((Path_t)Text(text))
#define Paths(...) ((Path_t)Texts(__VA_ARGS__))

PUREFUNC Path_t Path$concat(Path_t a, Path_t b);
PUREFUNC Path_t Path$escape_text(Text_t text);
Path_t Path$resolved(Path_t path, Path_t relative_to);
Path_t Path$relative(Path_t path, Path_t relative_to);
bool Path$exists(Path_t path);
bool Path$is_file(Path_t path, bool follow_symlinks);
bool Path$is_directory(Path_t path, bool follow_symlinks);
bool Path$is_pipe(Path_t path, bool follow_symlinks);
bool Path$is_socket(Path_t path, bool follow_symlinks);
bool Path$is_symlink(Path_t path);
void Path$write(Path_t path, Text_t text, int permissions);
void Path$append(Path_t path, Text_t text, int permissions);
Text_t Path$read(Path_t path);
void Path$remove(Path_t path, bool ignore_missing);
void Path$create_directory(Path_t path, int permissions);
Array_t Path$children(Path_t path, bool include_hidden);
Array_t Path$files(Path_t path, bool include_hidden);
Array_t Path$subdirectories(Path_t path, bool include_hidden);
Path_t Path$unique_directory(Path_t path);
Text_t Path$write_unique(Path_t path, Text_t text);

extern const TypeInfo Path$info;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0

