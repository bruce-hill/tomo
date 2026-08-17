// `tomo uninstall`: with no arguments, remove this whole Tomo installation
// from its prefix; with names or paths, remove individual installed programs
// (the executables + manpages that `tomo build --install` put in TOMO_PATH).

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../config.h"
#include "../stdlib/bools.h"
#include "../stdlib/lists.h"
#include "../stdlib/paths.h"
#include "../stdlib/print.h"
#include "../stdlib/text.h"
#include "commands.h"
#include "common.h"
#include "compilation.h"

static List_t names = EMPTY_LIST;
static OptionalBool_t skip_confirm = false;

static cli_arg_t uninstall_spec[] = {
    {"names", &names, List$info(&Text$info), .positional = true, .metavar = "name-or-path",
     .description = "the installed programs to uninstall (a name in TOMO_PATH/bin, or a path to a "
                    "binary); with no names, uninstalls this entire Tomo installation"}, //
    {"yes", &skip_confirm, &Bool$info, .short_flag = 'y',
     .description = "when uninstalling the whole installation, don't ask for confirmation"}, //
    VERBOSE_FLAG, //
};

#define warn(...) fprint(stderr, "\033[33mWarning:\033[m ", __VA_ARGS__)

// --- Uninstalling a single installed program --------------------------------

// Delete `p`, translating a permission error into the "re-run with more
// permissions" message (Tomo never elevates itself).
static void remove_file(Path_t p) {
    if (unlink(Path$as_c_string(Path$expand_home(p))) != 0) {
        if (errno == EACCES || errno == EPERM)
            print_err("You don't have permission to remove ", p,
                      "\nRe-run this command as the owner of that file (for example with `sudo`).");
        print_err("Could not remove ", p, ": ", Text$from_str(strerror(errno)));
    }
}

// Uninstall one program given a bare name (looked up in TOMO_PATH/bin) or a
// path to a binary. Only files Tomo itself produced are removed: the binary
// must carry the build-info header and the manpage must carry the Tomo marker.
// A missing binary or manpage is a warning, not an error -- the other half is
// still attempted.
static void uninstall_program(Text_t arg) {
    // A slash means the argument is a path; otherwise it's a name in bin/:
    bool is_path = strchr(Text$as_c_string(arg), '/') != NULL;
    Path_t bin = is_path ? Path$resolved(Path$from_text(arg), Path$current_dir())
                         : Path$from_str(String(TOMO_PATH, "/bin/", arg));
    Text_t name = Path$base_name(bin);
    Path_t manpage = Path$from_str(String(TOMO_PATH, "/man/man1/", name, ".1"));

    bool removed_any = false;

    if (!Path$exists(bin)) {
        warn("No installed program found at ", bin, "\n");
    } else if (!is_tomo_binary(bin)) {
        warn(bin, " is not a Tomo program; leaving it in place\n");
    } else {
        remove_file(bin);
        print("Removed ", bin);
        removed_any = true;
    }

    if (!Path$exists(manpage)) {
        // Only worth mentioning if we did remove the binary (an installed
        // program usually has a manpage; a plain missing one is noise):
        if (removed_any) warn("No manpage found at ", manpage, "\n");
    } else if (!is_tomo_manpage(manpage)) {
        warn(manpage, " is not a Tomo-generated manpage; leaving it in place\n");
    } else {
        remove_file(manpage);
        print("Removed ", manpage);
        removed_any = true;
    }

    if (removed_any) print("Uninstalled \033[1m", name, "\033[m");
    else print_err("Nothing to uninstall for: ", arg);
}

// --- Uninstalling this entire Tomo installation (former `uninstall-self`) ----

// The version another tomo@<version> installation in this prefix would
// downgrade to: the newest one, or NULL if this was the only installation.
static const char *newest_remaining_version(void) {
    List_t bins = Path$glob(Path$from_str(String(TOMO_PATH, "/bin/tomo@*")));
    const char *best = NULL;
    for (int64_t i = 0; i < (int64_t)bins.length; i++) {
        Path_t *bin = (Path_t *)(bins.data + i * bins.stride);
        const char *version = strrchr(*bin, '@') + 1;
        if (!best || strcmp(version, best) > 0) best = version;
    }
    return best;
}

// Remove the symlinks in the given directory that pointed into the removed
// installation and no longer resolve. Other files -- and even other dangling
// symlinks, in a shared prefix -- are left alone: only links whose target is
// a tomo@<version> path are Tomo's to delete.
static void remove_dangling_links(const char *dir) {
    List_t entries = Path$glob(Path$from_str(String(dir, "/*")));
    for (int64_t i = 0; i < (int64_t)entries.length; i++) {
        Path_t *entry = (Path_t *)(entries.data + i * entries.stride);
        if (!Path$is_symlink(*entry) || Path$exists(*entry)) continue;
        char link_target[PATH_MAX] = {};
        if (readlink(*entry, link_target, sizeof(link_target) - 1) < 0 || !strstr(link_target, "tomo@")) continue;
        xsystem("rm -f '", *entry, "'");
    }
}

// Create symlinks in PREFIX/man/manN/ for every page in the given version's
// man page store, without touching any links that still resolve:
static void link_man_pages(const char *version, const char *section) {
    List_t pages = Path$glob(Path$from_str(String(TOMO_PATH, "/man/tomo@", version, "/", section, "/*")));
    for (int64_t i = 0; i < (int64_t)pages.length; i++) {
        Path_t *page = (Path_t *)(pages.data + i * pages.stride);
        Text_t name = Path$base_name(*page);
        Path_t link = Path$from_str(String(TOMO_PATH, "/man/", section, "/", name));
        if (!Path$exists(link) && !Path$is_symlink(link))
            xsystem("ln -s '../tomo@", version, "/", section, "/", name, "' '", link, "'");
    }
}

// The zig toolchain is shared between coresident Tomo versions: the real
// copy lives in PREFIX/libexec/zig@<zig version>, refcounted by the
// libexec/tomo@<version>/zig symlinks pointing into it. Remove any store
// that no remaining installation references:
static void remove_orphaned_toolchains(void) {
    List_t stores = Path$glob(Path$from_str(String(TOMO_PATH, "/libexec/zig@*")));
    if (stores.length == 0) return;
    List_t links = Path$glob(Path$from_str(String(TOMO_PATH, "/libexec/tomo@*/zig")));
    for (int64_t i = 0; i < (int64_t)stores.length; i++) {
        Path_t *store = (Path_t *)(stores.data + i * stores.stride);
        char real_store[PATH_MAX];
        if (!realpath(*store, real_store)) continue;
        bool referenced = false;
        for (int64_t l = 0; l < (int64_t)links.length && !referenced; l++) {
            Path_t *link = (Path_t *)(links.data + l * links.stride);
            char real_link[PATH_MAX];
            referenced = realpath(*link, real_link) && strcmp(real_link, real_store) == 0;
        }
        if (!referenced) xsystem("rm -rf '", *store, "'");
    }
}

// Whether some `tomo` executable is still reachable through $PATH:
static bool tomo_on_path(void) {
    const char *path = getenv("PATH");
    if (!path) return false;
    for (const char *dir = path; *dir;) {
        size_t len = strcspn(dir, ":");
        if (len > 0) {
            char *exe = GC_MALLOC_ATOMIC(len + sizeof("/tomo"));
            memcpy(exe, dir, len);
            memcpy(exe + len, "/tomo", sizeof("/tomo"));
            if (access(exe, X_OK) == 0) return true;
        }
        dir += len + (dir[len] == ':' ? 1 : 0);
    }
    return false;
}

static void uninstall_self(void) {
    if (!skip_confirm) {
        if (!isatty(STDIN_FILENO)) print_err("Re-run with --yes to uninstall tomo@", TOMO_VERSION, " from ", TOMO_PATH);
        fprint_inline(stderr, "Uninstall \x1b[1mtomo@", TOMO_VERSION, "\x1b[m from ", TOMO_PATH, "? [y/N] ");
        fflush(stderr);
        char answer[16] = {};
        if (!fgets(answer, sizeof(answer), stdin) || (answer[0] != 'y' && answer[0] != 'Y'))
            print_err("Not uninstalling tomo@", TOMO_VERSION);
    }

    require_writable_prefix(TOMO_PATH);

    // Everything this installation put in the prefix lives in tomo@VERSION
    // directories (plus the symlinks fixed up below):
    xsystem("rm -rf", " '", TOMO_PATH, "/bin/tomo@", TOMO_VERSION, "'", " '", TOMO_PATH, "/include/tomo@", TOMO_VERSION,
            "'", " '", TOMO_PATH, "/lib/tomo@", TOMO_VERSION, "'", " '", TOMO_PATH, "/libexec/tomo@", TOMO_VERSION, "'",
            " '", TOMO_PATH, "/man/tomo@", TOMO_VERSION, "'", " '", TOMO_PATH, "/share/licenses/tomo@", TOMO_VERSION,
            "'");

    // ...and this version's per-user directories (scratch/stdin runner state
    // and cross-compilation target packs):
    xsystem("rm -rf '", Path$child(xdg_tomo_dir("XDG_STATE_HOME", "~/.local/state"), Texts("tomo@", TOMO_VERSION)),
            "' '", Path$child(xdg_tomo_dir("XDG_DATA_HOME", "~/.local/share"), Texts("tomo@", TOMO_VERSION)), "'");

    remove_orphaned_toolchains();

    // Fix up the version-independent symlinks: if another tomo@<version>
    // remains in this prefix, downgrade them to it; otherwise remove them.
    remove_dangling_links(String(TOMO_PATH, "/bin"));
    remove_dangling_links(String(TOMO_PATH, "/man/man1"));
    remove_dangling_links(String(TOMO_PATH, "/man/man3"));
    const char *downgrade = newest_remaining_version();
    if (downgrade) {
        Path_t tomo_link = Path$from_str(String(TOMO_PATH, "/bin/tomo"));
        if (!Path$exists(tomo_link) && !Path$is_symlink(tomo_link))
            xsystem("ln -s 'tomo@", downgrade, "' '", tomo_link, "'");
        link_man_pages(downgrade, "man1");
        link_man_pages(downgrade, "man3");
        print("Uninstalled \x1b[1mtomo@", TOMO_VERSION, "\x1b[m from ", TOMO_PATH, " (tomo@", downgrade, " remains)");
    } else {
        print("Uninstalled \x1b[1mtomo@", TOMO_VERSION, "\x1b[m from ", TOMO_PATH);
    }

    // If that was the last tomo anywhere on $PATH, the global cache (package
    // downloads and the bundled zig's compile cache) has no owner left either:
    if (!tomo_on_path()) {
        Path_t cache = xdg_tomo_dir("XDG_CACHE_HOME", "~/.cache");
        if (Path$is_directory(cache, true)) {
            xsystem("rm -rf '", cache, "'");
            whisper("Removed the cache at ", cache, " (no tomo left on $PATH)");
        }
    }
}

static int cmd_uninstall(cli_command_t *self, List_t extra_args) {
    (void)self, (void)extra_args;
    set_default_logs(0);
    // No names: uninstall this whole Tomo installation. Names/paths: uninstall
    // those individual installed programs.
    if (names.length == 0) {
        uninstall_self();
    } else {
        for (int64_t i = 0; i < (int64_t)names.length; i++)
            uninstall_program(*(Text_t *)(names.data + i * names.stride));
    }
    return 0;
}

cli_command_t uninstall_command = {
    .name = "uninstall",
    .summary = "Uninstall Tomo programs, or this whole Tomo installation",
    .description = "With one or more names or paths, uninstalls those programs: each named binary\n"
                   "(found in TOMO_PATH/bin, or at the given path) is removed if it is a Tomo\n"
                   "program, along with its Tomo-generated manpage. Files Tomo didn't create are\n"
                   "left alone, and a missing binary or manpage is only a warning.\n"
                   "\n"
                   "With no arguments, uninstalls this entire Tomo installation from its prefix,\n"
                   "along with its per-user state and cross-compilation target packs. If other\n"
                   "Tomo versions remain in the same prefix, the tomo and man page symlinks are\n"
                   "repointed to the newest one; otherwise they are removed too. If no tomo\n"
                   "remains anywhere on $PATH afterwards, the cache is also cleared.",
    .spec_len = sizeof(uninstall_spec) / sizeof(uninstall_spec[0]),
    .spec = uninstall_spec,
    .handler = cmd_uninstall,
};
