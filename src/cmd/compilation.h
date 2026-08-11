// The compilation engine driving transpiling, object compilation, executable
// linking, and package builds (shared by the CLI commands, not itself a
// command)

#pragma once

#include "../environment.h"
#include "../stdlib/datatypes.h"

typedef enum { COMPILE_C_FILES, COMPILE_OBJ, COMPILE_EXE } compile_mode_t;

void compile_files(env_t *env, List_t to_compile, List_t *object_files, List_t *extra_ldlibs, compile_mode_t mode);
Path_t compile_executable(env_t *base_env, Path_t path, Path_t exe_path, List_t object_files, List_t extra_ldlibs);
void compile_object_file(Path_t path);
void transpile_header(env_t *base_env, Path_t path);
void transpile_code(env_t *base_env, Path_t path);
void build_package(Path_t pkg_dir);
void build_package_archive(Path_t pkg_dir, List_t tm_files, Path_t archive);
void install_package(Path_t pkg_dir);
void build_file_dependency_graph(Table_t *build_info, Path_t path, Table_t *to_compile, Table_t *to_link);
void write_source_blob(env_t *env, Path_t main_file, Path_t blob_path);
void print_build_info(Path_t p);
void extract_embedded_source(Path_t binary);
bool is_stale(Path_t path, Path_t relative_to, bool ignore_missing);
bool is_stale_for_any(Path_t path, List_t relative_to, bool ignore_missing);
bool is_config_outdated(Path_t path);
