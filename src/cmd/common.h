// Shared state and helpers for the tomo CLI commands

#pragma once

#include <gc.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "../stdlib/datatypes.h"
#include "../stdlib/print.h"
#include "../stdlib/text.h"

#define run_cmd(...)                                                                                                   \
    ({                                                                                                                 \
        const char *_cmd = String(__VA_ARGS__);                                                                        \
        if (verbose) print("\033[94;1m", _cmd, "\033[m");                                                              \
        popen(_cmd, "w");                                                                                              \
    })
#define command_output(...)                                                                                            \
    ({                                                                                                                 \
        const char *_cmd = String(__VA_ARGS__);                                                                        \
        if (verbose) print("\033[94;1m", _cmd, "\033[m");                                                              \
        FILE *_prog = popen(_cmd, "r");                                                                                \
        char *_output = GC_MALLOC_ATOMIC(1024);                                                                        \
        fgets(_output, 1023, _prog);                                                                                   \
        if (_output[strlen(_output) - 1] == '\n') _output[strlen(_output) - 1] = '\0';                                 \
        int status = pclose(_prog);                                                                                    \
        (!WIFEXITED(status) || WEXITSTATUS(status) != 0) ? NULL : _output;                                             \
    })
#define xsystem(...)                                                                                                   \
    ({                                                                                                                 \
        int _status = system(String(__VA_ARGS__));                                                                     \
        if (!WIFEXITED(_status) || WEXITSTATUS(_status) != 0)                                                          \
            errx(1, "Failed to run command: %s", String(__VA_ARGS__));                                                 \
    })
#define list_text(list) Text$join(Text(" "), list)

#define whisper(...) print("\033[2m", __VA_ARGS__, "\033[m")

// Configuration shared by every command, populated from the global CLI flags
// (and the toolchain/linker setup in main()) before any command runs:
extern OptionalBool_t verbose, quiet, clean_build, source_mapping, install_target;

// Cross-compilation state, set up in main() when --target names a platform
// other than the one this Tomo build is for:
extern OptionalText_t target;
extern bool cross_compiling;
// The directory holding the target platform's lib/ + include/ trees (extracted
// from that platform's Tomo distribution archive). Lives in the user's XDG data
// directory rather than TOMO_PATH so installing one never needs root:
extern Text_t target_root;
// The prefix whose lib/ + include/ user programs compile and link against
// (TOMO_PATH normally, target_root when cross-compiling):
extern Text_t lib_root;
// Whether the platform being compiled for uses Mach-O linking (macOS):
extern bool link_macho;

extern OptionalText_t show_codegen, cflags, ldlibs, ldflags, optimization,
    // The toolchain is not configurable; these are set in main() to the
    // `zig cc`/`zig ar` bundled inside the Tomo installation:
    cc, ar;

extern Text_t config_summary,
    // This will be either "" or "sudo -u <user>" or "doas -u <user>"
    // to allow a command to put stuff into TOMO_PATH as the owner
    // of that directory.
    as_owner;

#ifdef __linux__
// The file modification time of the compiler itself, so files can be
// recompiled after the compiler changes (only on Linux is /proc/self/exe
// available); set up in main():
extern struct stat compiler_stat;
#endif

const char *paths_str(List_t paths);
const char *platform_os(const char *platform);
const char *platform_triple(const char *platform);
bool platform_supported(const char *platform);
void ensure_target_installed(void);
List_t normalize_tm_paths(List_t paths);
void wait_for_child_success(pid_t child);
Path_t get_exe_path(Path_t path);
Path_t build_file(Path_t path, const char *extension);
