// `tomo build`: compile a Tomo program to a standalone executable

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../config.h"
#include "../environment.h"
#include "../stdlib/bools.h"
#include "../stdlib/lists.h"
#include "../stdlib/optionals.h"
#include "../stdlib/paths.h"
#include "../stdlib/print.h"
#include "commands.h"
#include "common.h"
#include "compilation.h"

static OptionalPath_t file = NULL, output = NULL, prefix = NULL;
static OptionalBool_t do_install = false, skip_confirm = false;

static cli_arg_t build_spec[] = {
    {"file", &file, &Path$info, .positional = true, .required = true, .metavar = "file.tm",
     .description = "the program to compile"}, //
    {"output", &output, &Path$info, .short_flag = 'o',
     .description = "where to put the executable (defaults to a sibling of the .tm file)"}, //
    {"install", &do_install, &Bool$info,
     .description = "also install the executable (and its manpage) into a prefix's bin/ and man/"}, //
    {"prefix", &prefix, &Path$info, .metavar = "dir",
     .description = "the install prefix for --install (defaults to this Tomo installation's prefix)"}, //
    {"yes", &skip_confirm, &Bool$info, .short_flag = 'y',
     .description = "when installing, overwrite existing files without asking"}, //
    OPTIMIZATION_FLAG, //
    INSTRUMENT_FLAG, //
    DEBUG_FLAG, //
    VERBOSE_FLAG, //
    QUIET_FLAG, //
};

// The install prefix: --prefix if given (relative to the working directory),
// otherwise this Tomo installation's own prefix.
static Path_t install_prefix(void) {
    return prefix != NONE_PATH ? Path$expand_home(Path$resolved(prefix, Path$current_dir())) : Path$from_str(TOMO_PATH);
}

// Whether `dir` (resolved) is one of the directories on $PATH.
static bool dir_on_path(Path_t dir) {
    char real_dir[PATH_MAX];
    if (!realpath(Path$as_c_string(dir), real_dir)) return false;
    const char *path = getenv("PATH");
    if (!path) return false;
    for (const char *p = path; *p;) {
        size_t len = strcspn(p, ":");
        if (len > 0 && len < PATH_MAX) {
            char entry[PATH_MAX], real_entry[PATH_MAX];
            memcpy(entry, p, len);
            entry[len] = '\0';
            if (realpath(entry, real_entry) && strcmp(real_entry, real_dir) == 0) return true;
        }
        p += len + (p[len] == ':' ? 1 : 0);
    }
    return false;
}

// Copy the freshly built executable (and its generated manpage, if any) into
// the prefix's bin/ and man/man1/. `tomo uninstall <name>` reverses this.
// Prompts before overwriting existing files unless --yes was given.
static void install_program(Path_t src_file, Path_t exe_path) {
    Path_t prefix_dir = install_prefix();
    Text_t name = Path$base_name(exe_path);
    Path_t bin_dir = Path$child(prefix_dir, Text("bin"));
    Path_t man_dir = Path$child(Path$child(prefix_dir, Text("man")), Text("man1"));
    Path_t bin_dest = Path$child(bin_dir, name);
    Path_t man_dest = Path$child(man_dir, Texts(name, ".1"));

    Path_t manpage_file = build_file(Path$with_extension(src_file, Text(".1"), true), "");
    bool have_manpage = Path$is_file(manpage_file, true);

    // Confirm before clobbering anything already installed under those names:
    bool bin_exists = Path$exists(bin_dest), man_exists = have_manpage && Path$exists(man_dest);
    if ((bin_exists || man_exists) && skip_confirm != true) {
        if (bin_exists) fprint(stderr, "\x1b[33mWarning:\x1b[m \x1b[1m", bin_dest, "\x1b[m already exists.");
        if (man_exists) fprint(stderr, "\x1b[33mWarning:\x1b[m \x1b[1m", man_dest, "\x1b[m already exists.");
        if (!isatty(STDIN_FILENO)) print_err("Refusing to overwrite; re-run with --yes to overwrite existing files.");
        fprint_inline(stderr, "Overwrite? [y/N] ");
        fflush(stderr);
        char answer[16] = {};
        if (!fgets(answer, sizeof(answer), stdin) || (answer[0] != 'y' && answer[0] != 'Y'))
            print_err("Not installing ", name);
    }

    xsystem("mkdir -p '", bin_dir, "' '", man_dir, "'");
    xsystem("cp -v '", exe_path, "' '", bin_dest, "'");
    if (have_manpage) xsystem("cp -v '", manpage_file, "' '", man_dest, "'");
    print("Installed \033[1m", name, "\033[m into ", prefix_dir);
    if (!dir_on_path(bin_dir))
        fprint(stderr, "\x1b[33mWarning:\x1b[m ", bin_dir,
               " is not on your $PATH, so `", name, "` won't be found by name until you add it.");
}

static int cmd_build(cli_command_t *self, List_t extra_args) {
    (void)self, (void)extra_args;
    set_default_logs(LOG_BUILD);
    if (prefix != NONE_PATH && do_install != true) print_err("`--prefix` only applies together with `--install`");
    // `--install` puts a native binary into the prefix, so it can't be combined
    // with cross-compilation (the binary wouldn't run here) and needs write
    // access to the prefix -- check both before doing the build work:
    if (do_install) {
        if (cross_compiling) print_err("`--install` can't be combined with --target: the binary wouldn't run here");
        require_writable_prefix(Path$as_c_string(install_prefix()));
    }
    // A built executable is a persistent artifact, so default to the highest
    // safe optimization level and the size-reducing link flags; -O overrides
    // the level:
    configure_codegen(opt_flag.tag == TEXT_NONE ? Text("3") : opt_flag, /*optimize=*/true);
    List_t files = normalize_tm_paths(List(file));
    Path_t path = *(Path_t *)files.data;

    Path_t exe_path;
    if (output != NULL) {
        exe_path = Path$resolved(output, Path$current_dir());
    } else {
        exe_path = get_exe_path(path);
        // Put the executable as a sibling to the .tm file instead of in the
        // .tomo directory. Cross-compiled executables get the target platform
        // as a suffix (foo.aarch64-macos) so they don't collide with the
        // native executable or each other:
        Text_t exe_name = Path$base_name(exe_path);
        if (cross_compiling) exe_name = Texts(exe_name, ".", target);
        exe_path = Path$sibling(path, exe_name);
    }

    env_t *env = global_env(source_mapping, instrument, debugging);
    List_t object_files = EMPTY_LIST, extra_ldlibs = EMPTY_LIST;
    compile_files(env, List(path), &object_files, &extra_ldlibs, COMPILE_EXE);
    compile_executable(env, path, exe_path, object_files, extra_ldlibs, /*embed_git_info=*/true);
    if (do_install) install_program(path, exe_path);
    return 0;
}

cli_command_t build_command = {
    .name = "build",
    .summary = "Compile a Tomo program to a standalone executable",
    .description = "Compiles the .tm program to a standalone executable next to the source\n"
                   "file (or at -o). With --install, the executable and its manpage are also\n"
                   "copied into a prefix's bin/ and man/man1/ (the prefix defaults to this Tomo\n"
                   "installation's, or --prefix a different one); existing files are overwritten\n"
                   "only after confirmation, or with --yes. Remove installed programs with\n"
                   "`tomo uninstall <name>`.",
    .spec_len = sizeof(build_spec) / sizeof(build_spec[0]),
    .spec = build_spec,
    .handler = cmd_build,
};
