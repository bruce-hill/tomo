// Logic for converting user's Tomo names into valid C identifiers

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "environment.h"
#include "sha256.h"
#include "stdlib/fail.h"
#include "stdlib/paths.h"
#include "stdlib/print.h"
#include "stdlib/text.h"
#include "stdlib/util.h"

public
Text_t build_target_platform = EMPTY_TEXT;

static const char *c_keywords[] = {
    // Maintain sorted order:
    "_Alignas",
    "_Alignof",
    "_Atomic",
    "_BitInt",
    "_Bool",
    "_Complex",
    "_Decimal128",
    "_Decimal32",
    "_Decimal64",
    "_Generic",
    "_Imaginary",
    "_Noreturn",
    "_Static_assert",
    "_Thread_local",
    "alignas",
    "__alignof__",
    "auto",
    "bool",
    "break",
    "case",
    "char",
    "const",
    "constexpr",
    "continue",
    "default",
    "do",
    "double",
    "else",
    "enum",
    "extern",
    "false",
    "float",
    "for",
    "goto",
    "if",
    "inline",
    "int",
    "long",
    "nullptr",
    "register",
    "restrict",
    "return",
    "short",
    "signed",
    "sizeof",
    "static",
    "static_assert",
    "struct",
    "switch",
    "thread_local",
    "true",
    "typedef",
    "typeof",
    "typeof_unqual",
    "union",
    "unsigned",
    "void",
    "volatile",
    "while",
};

static CONSTFUNC bool is_keyword(const char *word, size_t len) {
    int64_t lo = 0, hi = sizeof(c_keywords) / sizeof(c_keywords[0]) - 1;
    while (lo <= hi) {
        int64_t mid = (lo + hi) / 2;
        int32_t cmp = strncmp(word, c_keywords[mid], len + 1);
        if (cmp == 0) return true;
        else if (cmp > 0) lo = mid + 1;
        else if (cmp < 0) hi = mid - 1;
    }
    return false;
}

public
Text_t valid_c_name(const char *name) {
    size_t len = strlen(name);
    size_t trailing_underscores = 0;
    while (trailing_underscores < len && name[len - 1 - trailing_underscores] == '_')
        trailing_underscores += 1;
    if (is_keyword(name, len - trailing_underscores)) {
        return Texts(Text$from_str(name), Text("_"));
    }
    return Text$from_str(name);
}

public
Text_t CONSTFUNC namespace_name(env_t *env, namespace_t *ns, Text_t name) {
    for (; ns; ns = ns->parent) {
        name = Texts(ns->name, "$", name);
    }
    if (env->id_suffix.length > 0) name = Texts(name, env->id_suffix);
    return name;
}

#define BUILD_CACHE_MAX_AGE_SECS (30 * 24 * 3600)

// Same XDG_CACHE_HOME-or-~/.cache logic as download_cache_dir() in
// packages.c and xdg_tomo_dir() in cmd/common.c, duplicated to avoid a
// dependency between core and cmd/:
static Path_t xdg_cache_tomo_dir(void) {
    const char *cache_home = getenv("XDG_CACHE_HOME");
    return (cache_home && cache_home[0] == '/') ? Path$from_str(String(cache_home, "/tomo"))
                                                : Path$expand_home(Path$from_str("~/.cache/tomo"));
}

// Only called right after creating a new build-cache dir, so ordinary
// (non-fallback) builds never pay this cost:
static void prune_stale_build_dirs(Path_t build_cache) {
    List_t entries = Path$children(build_cache, true);
    time_t now = time(NULL);
    for (int64_t i = 0; i < (int64_t)entries.length; i++) {
        Path_t entry = *(Path_t *)(entries.data + i * entries.stride);
        if (!Path$is_directory(entry, true)) continue;
        OptionalInt64_t modified = Path$modified(entry, true);
        if (!modified.has_value) continue;
        if (now - (time_t)modified.value > BUILD_CACHE_MAX_AGE_SECS) Path$remove(entry, true);
    }
}

// The directory to use as `dir`'s ".tomo" root: ordinarily "<dir>/.tomo",
// but redirected under $XDG_CACHE_HOME/tomo/build/<hash>/.tomo when `dir` is
// $HOME or otherwise not writable. Always ends in a literal ".tomo" so
// ancestor-walking checks like package_store_entry() keep working either
// way. Creates the directory before returning.
public
Path_t tomo_root_for(Path_t dir) {
    char real_dir_buf[PATH_MAX];
    const char *real_dir = realpath(dir, real_dir_buf) ? real_dir_buf : dir;

    const char *home = getenv("HOME");
    char real_home_buf[PATH_MAX];
    const char *real_home = (home && realpath(home, real_home_buf)) ? real_home_buf : NULL;

    const char *reason = NULL;
    if (real_home && streq(real_dir, real_home)) reason = "your home directory";
    else if (!Path$can_write(dir)) reason = "not writable";

    if (reason == NULL) {
        Path_t root = Path$child(dir, Text(".tomo"));
        Result_t made = Path$create_directory(root, 0755, true);
        if (made.Failure.reason.tag != TEXT_NONE) fail("Could not make .tomo directory: ", made.Failure.reason);
        return root;
    }

    // 64 bits of the digest is plenty to avoid collisions between project
    // directories without a 64-character directory name; the directory's own
    // name is tacked on too, just so the cache isn't a wall of opaque hashes:
    char hash[SHA256_HEX_SIZE];
    sha256_hex(real_dir, strlen(real_dir), hash);
    hash[16] = '\0';
    Path_t build_cache = Path$child(xdg_cache_tomo_dir(), Text("build"));
    Path_t hash_dir = Path$child(build_cache, Texts(Text$from_str(hash), "-", Path$base_name(dir)));
    Path_t root = Path$child(hash_dir, Text(".tomo"));
    bool already_existed = Path$is_directory(root, true);
    Result_t made = Path$create_directory(root, 0755, true);
    if (made.Failure.reason.tag != TEXT_NONE) fail("Could not create build cache directory: ", made.Failure.reason);
    if (!already_existed) {
        fprint(stderr, "Warning: ", dir, " is ", reason, "; using a temporary build directory instead: ", root, "\n");
        prune_stale_build_dirs(build_cache);
    }
    return root;
}

public
Path_t tm_build_dir(Path_t tm_path) {
    Path_t build_dir = tomo_root_for(Path$sibling(tm_path, Text(".")));
    if (build_target_platform.length > 0) {
        build_dir = Path$child(build_dir, build_target_platform);
        if (mkdir(Path$as_c_string(build_dir), 0755) != 0) {
            if (!Path$is_directory(build_dir, true)) err(1, "Could not make .tomo directory");
        }
    }
    return build_dir;
}

public
Text_t get_id_suffix(const char *filename) {
    assert(filename);
    Path_t path = Path$from_str(filename);
    Path_t build_dir = tm_build_dir(path);
    Path_t id_file = Path$child(build_dir, Texts(Path$base_name(path), Text$from_str(".id")));
    OptionalText_t id = Path$read(id_file);
    if (id.tag == TEXT_NONE) err(1, "Could not read ID file: %s", Path$as_c_string(id_file));
    id = Text$trim(id, Text(" \r\n"), true, true);
    return Texts("$", id);
}
