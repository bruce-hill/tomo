// Compilation environments

#pragma once

#include "environment.h"
#include "stdlib/datatypes.h"

Text_t valid_c_name(const char *name);
Text_t namespace_name(env_t *env, namespace_t *ns, Text_t name);
Text_t get_id_suffix(const char *filename);

// When cross-compiling (--target), set to the target platform key so build
// artifacts go in .tomo/<platform>/ instead of .tomo/:
extern Text_t build_target_platform;
// The .tomo directory (created if needed) that holds a .tm file's artifacts:
Path_t tm_build_dir(Path_t tm_path);
