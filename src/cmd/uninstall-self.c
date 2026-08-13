// `tomo uninstall-self`: remove this Tomo installation from its prefix

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../config.h"
#include "../stdlib/bools.h"
#include "../stdlib/lists.h"
#include "../stdlib/paths.h"
#include "../stdlib/text.h"
#include "common.h"
#include "commands.h"

static OptionalBool_t skip_confirm = false;

static cli_arg_t uninstall_self_spec[] = {
    {"yes", &skip_confirm, &Bool$info, .short_flag = 'y', .description = "uninstall without asking for confirmation"}, //
    VERBOSE_FLAG, //
};

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
        xsystem(as_owner, "rm -f '", *entry, "'");
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
            xsystem(as_owner, "ln -s '../tomo@", version, "/", section, "/", name, "' '", link, "'");
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
        if (!referenced) xsystem(as_owner, "rm -rf '", *store, "'");
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

static int cmd_uninstall_self(cli_command_t *self, List_t extra_args) {
    (void)self, (void)extra_args;
    set_default_logs(0);

    if (!skip_confirm) {
        if (!isatty(STDIN_FILENO))
            print_err("Re-run with --yes to uninstall tomo@", TOMO_VERSION, " from ", TOMO_PATH);
        fprint_inline(stderr, "Uninstall \x1b[1mtomo@", TOMO_VERSION, "\x1b[m from ", TOMO_PATH, "? [y/N] ");
        fflush(stderr);
        char answer[16] = {};
        if (!fgets(answer, sizeof(answer), stdin) || (answer[0] != 'y' && answer[0] != 'Y'))
            print_err("Not uninstalling tomo@", TOMO_VERSION);
    }

    // Everything this installation put in the prefix lives in tomo@VERSION
    // directories (plus the symlinks fixed up below):
    xsystem(as_owner, "rm -rf", " '", TOMO_PATH, "/bin/tomo@", TOMO_VERSION, "'", " '", TOMO_PATH, "/include/tomo@",
            TOMO_VERSION, "'", " '", TOMO_PATH, "/lib/tomo@", TOMO_VERSION, "'", " '", TOMO_PATH, "/libexec/tomo@",
            TOMO_VERSION, "'", " '", TOMO_PATH, "/man/tomo@", TOMO_VERSION, "'", " '", TOMO_PATH,
            "/share/licenses/tomo@", TOMO_VERSION, "'");

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
            xsystem(as_owner, "ln -s 'tomo@", downgrade, "' '", tomo_link, "'");
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
    return 0;
}

cli_command_t uninstall_self_command = {
    .name = "uninstall-self",
    .summary = "Uninstall this Tomo installation",
    .description = "Removes this version's files from the installation prefix, along with its\n"
                   "per-user state and cross-compilation target packs. If other Tomo versions\n"
                   "remain in the same prefix, the tomo and man page symlinks are repointed to\n"
                   "the newest one; otherwise they are removed too. If no tomo remains anywhere\n"
                   "on $PATH afterwards, the cache (package downloads and the bundled zig's\n"
                   "compile cache) is also cleared.",
    .spec_len = sizeof(uninstall_self_spec) / sizeof(uninstall_self_spec[0]),
    .spec = uninstall_self_spec,
    .handler = cmd_uninstall_self,
};
