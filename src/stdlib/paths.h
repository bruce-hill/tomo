#pragma once

// A lang for filesystem paths

#include <stdbool.h>
#include <stdint.h>

#include "types.h"
#include "datatypes.h"
#include "optionals.h"

Path_t Pathヽfrom_str(const char *str);
Path_t Pathヽfrom_text(Text_t text);
// int Pathヽprint(FILE *f, Path_t path);
const char *Pathヽas_c_string(Path_t path);
#define Path(str) Pathヽfrom_str(str)
Path_t Pathヽ_concat(int n, Path_t items[n]);
#define Pathヽconcat(...) Pathヽ_concat((int)sizeof((Path_t[]){__VA_ARGS__})/sizeof(Path_t), ((Path_t[]){__VA_ARGS__}))
Path_t Pathヽresolved(Path_t path, Path_t relative_to);
Path_t Pathヽrelative_to(Path_t path, Path_t relative_to);
Path_t Pathヽexpand_home(Path_t path);
bool Pathヽexists(Path_t path);
bool Pathヽis_file(Path_t path, bool follow_symlinks);
bool Pathヽis_directory(Path_t path, bool follow_symlinks);
bool Pathヽis_pipe(Path_t path, bool follow_symlinks);
bool Pathヽis_socket(Path_t path, bool follow_symlinks);
bool Pathヽis_symlink(Path_t path);
bool Pathヽcan_read(Path_t path);
bool Pathヽcan_write(Path_t path);
bool Pathヽcan_execute(Path_t path);
OptionalInt64_t Pathヽmodified(Path_t path, bool follow_symlinks);
OptionalInt64_t Pathヽaccessed(Path_t path, bool follow_symlinks);
OptionalInt64_t Pathヽchanged(Path_t path, bool follow_symlinks);
void Pathヽwrite(Path_t path, Text_t text, int permissions);
void Pathヽwrite_bytes(Path_t path, List_t bytes, int permissions);
void Pathヽappend(Path_t path, Text_t text, int permissions);
void Pathヽappend_bytes(Path_t path, List_t bytes, int permissions);
OptionalText_t Pathヽread(Path_t path);
OptionalList_t Pathヽread_bytes(Path_t path, OptionalInt_t limit);
void Pathヽset_owner(Path_t path, OptionalText_t owner, OptionalText_t group, bool follow_symlinks);
OptionalText_t Pathヽowner(Path_t path, bool follow_symlinks);
OptionalText_t Pathヽgroup(Path_t path, bool follow_symlinks);
void Pathヽremove(Path_t path, bool ignore_missing);
void Pathヽcreate_directory(Path_t path, int permissions);
List_t Pathヽchildren(Path_t path, bool include_hidden);
List_t Pathヽfiles(Path_t path, bool include_hidden);
List_t Pathヽsubdirectories(Path_t path, bool include_hidden);
Path_t Pathヽunique_directory(Path_t path);
Path_t Pathヽwrite_unique(Path_t path, Text_t text);
Path_t Pathヽwrite_unique_bytes(Path_t path, List_t bytes);
Path_t Pathヽparent(Path_t path);
Text_t Pathヽbase_name(Path_t path);
Text_t Pathヽextension(Path_t path, bool full);
bool Pathヽhas_extension(Path_t path, Text_t extension);
Path_t Pathヽchild(Path_t path, Text_t name);
Path_t Pathヽsibling(Path_t path, Text_t name);
Path_t Pathヽwith_extension(Path_t path, Text_t extension, bool replace);
Path_t Pathヽcurrent_dir(void);
Closure_t Pathヽby_line(Path_t path);
List_t Pathヽglob(Path_t path);

uint64_t Pathヽhash(const void *obj, const TypeInfo_t*);
int32_t Pathヽcompare(const void *a, const void *b, const TypeInfo_t *type);
bool Pathヽequal(const void *a, const void *b, const TypeInfo_t *type);
bool Pathヽequal_values(Path_t a, Path_t b);
Text_t Pathヽas_text(const void *obj, bool color, const TypeInfo_t *type);
bool Pathヽis_none(const void *obj, const TypeInfo_t *type);
void Pathヽserialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type);
void Pathヽdeserialize(FILE *in, void *obj, List_t *pointers, const TypeInfo_t *type);

extern const TypeInfo_t Pathヽinfo;
extern const TypeInfo_t PathTypeヽinfo;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0

