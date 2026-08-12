// This file defines some code for getting info about packages and installing them.

#include <err.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "config.h"
#include "naming.h"
#include "packages.h"
#include "sha256.h"
#include "stdlib/datatypes.h"
#include "stdlib/optionals.h"
#include "stdlib/paths.h"
#include "stdlib/print.h"
#include "stdlib/simpleparse.h"
#include "stdlib/tables.h"
#include "stdlib/text.h"
#include "stdlib/util.h"

typedef struct {
    const char *name;
    Table_t info;
} pkg_info_t;

// Installed packages are content-addressed: each project keeps its own store
// of them in .tomo/store/<digest>/, and the names things call them by are
// symlinks (see create_binding_link() below).

// The store entry (a .tomo/store/<digest>/ directory) containing the given
// path, if any:
OptionalPath_t package_store_entry(Path_t path) {
    for (int depth = 0; depth < PATH_MAX; depth++) {
        OptionalPath_t parent = Path$parent(path);
        // Stop at the root, or once the walk escapes a relative path's start:
        if (parent == NULL || parent[0] == '\0' || streq(parent, path) || streq(parent, ".")
            || Text$equal_values(Path$base_name(parent), Text("..")))
            return NONE_PATH;
        if (Text$equal_values(Path$base_name(parent), Text("store"))
            && Text$equal_values(Path$base_name(Path$parent(parent)), Text(".tomo")))
            return path;
        path = parent;
    }
    return NONE_PATH;
}

// A file inside a store entry installs *its* dependencies into the same
// store, so the store root for a consumer is the store containing it (if any)
// or its own directory's .tomo/store:
static Path_t package_store_root(Path_t using_file) {
    OptionalPath_t entry = package_store_entry(using_file);
    if (entry != NULL) return Path$parent(entry);
    return Path$child(tomo_root_for(Path$parent(using_file)), Text("store"));
}

// Verified downloaded package artifacts are cached globally, keyed by digest,
// so other projects can materialize their stores without re-fetching:
static Path_t download_cache_dir(Text_t digest) {
    const char *cache_home = getenv("XDG_CACHE_HOME");
    Path_t base = (cache_home && cache_home[0] == '/') ? Path$from_str(String(cache_home, "/tomo"))
                                                       : Path$expand_home(Path$from_str("~/.cache/tomo"));
    return Path$child(base, digest);
}

// Record which package a `use NAME` resolved to, as a "packages/NAME" symlink
// next to the file that used it. For a consumer inside a store, the link is
// store-relative ("../../<digest>", pointing two levels up from its packages/
// directory into the store); for a program, the link farm lives in its .tomo
// directory, pointing "../store/<digest>" for entries in the project's own
// store. Either way the whole project tree is relocatable, and every level of
// the dependency graph resolves the same way: ./packages/NAME, one level at a
// time. Only dependencies living outside the consumer's store (e.g. local
// directory-source packages) get absolute link targets.
static void create_binding_link(Path_t using_file, const char *name, Path_t installed) {
    if (strchr(name, '/') != NULL) return; // Just in case: never write outside packages/
    Path_t using_dir = Path$parent(using_file);
    bool consumer_in_store = package_store_entry(using_file) != NULL;
    bool dep_in_this_store = streq(Path$parent(installed), package_store_root(using_file));

    Path_t link_dir = consumer_in_store ? Path$child(using_dir, Text("packages"))
                                        : Path$child(tomo_root_for(using_dir), Text("packages"));
    const char *target = installed;
    if (dep_in_this_store) {
        target = Path$relative_to(installed, link_dir);
    } else if (!consumer_in_store) {
        // Directory-source packages inside the project (e.g. vendored ones)
        // also get relative links, so the project stays relocatable:
        Path_t cwd = Path$current_dir();
        Path_t project = Path$resolved(using_dir, cwd);
        Path_t dep = Path$resolved(installed, cwd);
        if (strncmp(dep, project, strlen(project)) == 0 && dep[strlen(project)] == '/')
            target = Path$relative_to(dep, Path$resolved(link_dir, cwd));
    }

    Result_t result = Path$create_directory(link_dir, 0755, true);
    if (result.Failure.reason.tag != TEXT_NONE) return;
    Path_t link = Path$child(link_dir, Text$from_str(name));
    char existing[PATH_MAX];
    ssize_t len = readlink(link, existing, sizeof(existing) - 1);
    if (len >= 0) {
        existing[len] = '\0';
        if (streq(existing, target)) return; // Already correct
    }
    unlink(link);
    if (symlink(target, link) != 0) fprint(stderr, "Warning: could not create package link ", link, " -> ", target);
}

#define xsystem(...)                                                                                                   \
    ({                                                                                                                 \
        const char *cmd = String(__VA_ARGS__);                                                                         \
        int _status = system(cmd);                                                                                     \
        if (!WIFEXITED(_status) || WEXITSTATUS(_status) != 0) {                                                        \
            errx(1, "Failed to run command: %s", String(__VA_ARGS__));                                                 \
        }                                                                                                              \
    })

// The tomo executable to use for nested invocations: the same one that's
// running (recorded in main()), never a possibly-older `tomo` from PATH:
static const char *tomo_exe(void) {
    const char *exe = getenv("TOMO_EXE");
    return exe ? exe : "tomo";
}

// Packages install as source; make sure the compiled package.a exists too
// (e.g. for store entries pre-seeded by `tomo --extract-source`):
static void ensure_package_built(Path_t install_location) {
    if (!Path$exists(Path$child(install_location, Text("package.a"))))
        xsystem(quoted(tomo_exe()), " package ", install_location);
}

#define xsystem_cleanup(tmpdir, ...)                                                                                   \
    ({                                                                                                                 \
        const char *cmd = String(__VA_ARGS__);                                                                         \
        int _status = system(cmd);                                                                                     \
        if (!WIFEXITED(_status) || WEXITSTATUS(_status) != 0) {                                                        \
            if (tmpdir) Path$remove(tmpdir, true);                                                                     \
            errx(1, "Failed to run command: %s", String(__VA_ARGS__));                                                 \
        }                                                                                                              \
    })

static OptionalText_t file_digest(Path_t path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NONE_TEXT;

    sha256 ctx;
    sha256_init(&ctx);

    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        sha256_append(&ctx, buf, n);
    fclose(f);

    char hash[SHA256_HEX_SIZE];
    sha256_finalize_hex(&ctx, hash);

    const char *prefix = "sha256:";
    char *ret = GC_MALLOC_ATOMIC(strlen(prefix) + SHA256_HEX_SIZE + 1);
    char *p = ret;
    p = stpcpy(p, prefix);
    memcpy(p, hash, SHA256_HEX_SIZE);
    p += SHA256_HEX_SIZE;
    *p = '\0';
    return Text$from_str(ret);
}

Text_t get_package_name(Path_t lib_dir) {
    Text_t name = Path$base_name(lib_dir);
    name = Text$without_prefix(name, Text("tomo-"));
    name = Text$without_suffix(name, Text("-tomo"));
    return name;
}

static Text_t package_text(pkg_info_t pkg) {
    Text_t text = Texts("[", pkg.name, "]\n");
    for (int64_t i = 0; i < (int64_t)pkg.info.entries.length; i++) {
        struct {
            const char *key, *value;
        } *entry = pkg.info.entries.data + i * pkg.info.entries.stride;
        text = Texts(text, entry->key, "=", entry->value, "\n");
    }
    return text;
}

static OptionalPath_t try_install_package_from_file(pkg_info_t *pkg, const char *source, Path_t downloaded,
                                                    OptionalPath_t tmpdir, Path_t store_root);

static OptionalPath_t try_install_package_from_source(Path_t ini_file, pkg_info_t *pkg, const char *source,
                                                      bool ask_confirmation, Path_t store_root) {
    Table$str_set(&pkg->info, "source", source);
    if (source[0] == '.' || source[0] == '/' || source[0] == '~') {
        Path_t source_path = Path$from_str(source);
        source_path = Path$resolved(source_path, Path$parent(ini_file));
        if (!Path$exists(source_path)) {
            print("No such file: ", source_path);
            return NONE_PATH;
        }
        if (Path$is_directory(source_path, true)) {
            if (Table$str_get(pkg->info, "digest") != NULL) {
                // TODO: add support for automatically deleting digest with confirmation
                fail("The package source for ", pkg->name, " is a directory, but the package has a digest.",
                     "\nSource directory packages cannot have a digest, so please delete the digest from this "
                     "package.\nSource: ",
                     source_path);
            }
            xsystem(quoted(tomo_exe()), " package ", source_path);
            return source_path;
        } else {
            return try_install_package_from_file(pkg, source, Path$resolved(source, Path$parent(ini_file)), NULL,
                                                 store_root);
        }
    }

    if (ask_confirmation) {
        OptionalText_t answer = ask(Texts("The package ", Text$quoted(Text$from_str(pkg->name), false, Text("\"")),
                                          " is not installed.\nDo you want to install it from ",
                                          Text$quoted(Text$from_str(source), false, Text("\"")), "? [Y/n] "),
                                    true, true);
        if (!(answer.length == 0 || Text$equal_values(answer, Text("Y")) || Text$equal_values(answer, Text("y")))) {
            print("Okay, not installing it!");
            exit(1);
        }
    }

    print("Installing ", Text$quoted(Text$from_str(pkg->name), false, Text("\"")), " from URL...");

    Path_t tmpdir = Path$unique_directory(Path$from_text(Texts("/tmp/tomo-", pkg->name, "-XXXXXX")));

    xsystem_cleanup(tmpdir, "curl --output-dir ", quoted(tmpdir), " -LJO ", quoted(source));

    List_t children = Path$children(tmpdir, true);
    if (children.length != 1) {
        Path$remove(tmpdir, true);
        print("Failed to download file ", pkg->name, " from: ", source);
        return NONE_PATH;
    }

    Path_t downloaded = *(Path_t *)children.data;
    return try_install_package_from_file(pkg, source, downloaded, tmpdir, store_root);
}

OptionalPath_t try_install_package_from_file(pkg_info_t *pkg, const char *source, Path_t downloaded,
                                             OptionalPath_t tmpdir, Path_t store_root) {

    OptionalText_t digest = file_digest(downloaded);
    if (digest.tag == TEXT_NONE) {
        if (tmpdir != NULL) Path$remove(tmpdir, true);
        fail("Failed to compute digest for package ", pkg->name);
    }

    const char *required_digest = Table$str_get(pkg->info, "digest");
    if (required_digest == NULL) {
        Table$str_set(&pkg->info, "digest", Text$as_c_string(digest));
        print("Added digest for ", pkg->name, ": ", digest);
    } else {
        if (!Text$equal_values(Text$from_str(required_digest), digest)) {
            // Digest mismatch
            if (tmpdir != NULL) Path$remove(tmpdir, true);
            fail("Mismatched digest sum for package ", pkg->name, "! Expected ", required_digest, " but got ", digest);
        }
    }

    // Cache the verified artifact so other projects can materialize their
    // stores without re-downloading:
    Path_t cache_dir = download_cache_dir(digest);
    Path_t cached = Path$child(cache_dir, Path$base_name(downloaded));
    if (!Path$exists(cached)) {
        Result_t cache_result = Path$create_directory(cache_dir, 0755, true);
        if (cache_result.Failure.reason.tag == TEXT_NONE) xsystem("cp ", quoted(downloaded), " ", quoted(cached));
    }

    OptionalPath_t install_location = Path$child(store_root, digest);

    Result_t result = Path$create_directory(install_location, 0755, true);
    if (result.Failure.reason.tag != TEXT_NONE) {
        if (tmpdir != NULL) Path$remove(tmpdir, true);
        fail("Failed to make install directory: ", result.Failure.reason);
    }

    if (Path$has_extension(downloaded, Text(".tar.gz")) || Path$has_extension(downloaded, Text(".tgz"))
        || Path$has_extension(downloaded, Text(".tar.xz")) || Path$has_extension(downloaded, Text(".txz"))
        || Path$has_extension(downloaded, Text(".tar"))) {
        xsystem_cleanup(tmpdir, "tar xf ", downloaded, " -C ", install_location);
    } else if (Path$has_extension(downloaded, Text(".zip"))) {
        xsystem_cleanup(tmpdir, "unzip ", downloaded, " -d ", install_location);
    } else {
        Path$remove(tmpdir, true);
        fail("Unsupported package filetype: ", downloaded);
    }

    List_t installed_files = Path$children(install_location, true);
    if (installed_files.length == 1
        && Path$is_directory(*(Path_t *)installed_files.data, false)) { // Single top-level wrapper dir
        Path_t top_level = *(Path_t *)installed_files.data;
        List_t contents = Path$children(top_level, true);
        for (int64_t i = 0; i < (int64_t)contents.length; i++) {
            Path_t p = *(Path_t *)(contents.data + i * contents.stride);
            result = Path$move(p, Path$child(install_location, Path$base_name(p)), false);
            if (result.Failure.reason.tag != TEXT_NONE) {
                if (tmpdir != NULL) Path$remove(tmpdir, true);
                fail(result.Failure.reason);
            }
        }
        result = Path$remove(top_level, true);
        if (result.Failure.reason.tag != TEXT_NONE) {
            if (tmpdir != NULL) Path$remove(tmpdir, true);
            fail(result.Failure.reason);
        }
    }

    xsystem_cleanup(tmpdir, quoted(tomo_exe()), " package ", install_location);

    Path_t info = Path$child(install_location, Text("package_info.ini"));
    Path$write(info, Texts("name=", pkg->name, "\nsource=", source, "\ndigest=", digest, "\n"), 0644);

    // Always clean up tmpdir!
    if (tmpdir != NULL) Path$remove(tmpdir, true);

    // Add digest to the package.ini file if it wasn't already there
    if (required_digest == NULL && digest.tag != TEXT_NONE) {
        Table$str_set(&pkg->info, "digest", Text$as_c_string(digest));
        print("Added digest for ", pkg->name, ": ", digest);
    }

    return install_location;
}

static OptionalPath_t try_install_package(Path_t ini_file, pkg_info_t *pkg, bool ask_confirmation, Path_t store_root) {
    OptionalPath_t install_location = NULL;
    const char *digest = Table$str_get(pkg->info, "digest");
    if (digest) {
        install_location = Path$child(store_root, Text$from_str(digest));
        if (Path$exists(install_location)) {
            ensure_package_built(install_location);
            return install_location;
        }
        // A verified artifact for this digest may already be cached, in which
        // case no download (and no confirmation) is needed:
        Path_t cache_dir = download_cache_dir(Text$from_str(digest));
        if (Path$is_directory(cache_dir, true)) {
            List_t cached = Path$children(cache_dir, true);
            if (cached.length == 1) {
                const char *source = Table$str_get(pkg->info, "source");
                install_location = try_install_package_from_file(pkg, source ? source : "cache", *(Path_t *)cached.data,
                                                                 NULL, store_root);
                if (install_location != NULL) return install_location;
            }
        }
    }

    const char *source = Table$str_get(pkg->info, "source");
    for (int i = 1; source; i++) {
        install_location = try_install_package_from_source(ini_file, pkg, source, ask_confirmation, store_root);
        if (install_location != NULL) return install_location;
        const char *new_source_key = String("source-", i + 1);
        source = Table$str_get(pkg->info, new_source_key);
    }
    fail("No source for package: ", pkg->name);
    return NULL;
}

static bool ini_is_writable(Path_t ini_file) {
    return Path$exists(ini_file) ? Path$can_write(ini_file) : Path$can_write(Path$parent(ini_file));
}

// Package pins are just an optimization, so skip the write instead of
// crashing; warn once per ini file:
static void warn_ini_not_writable(Path_t ini_file) {
    static Table_t warned = EMPTY_TABLE;
    if (Table$str_get(warned, ini_file) != NULL) return;
    fprint(stderr, "Warning: ", ini_file, " is not writable, so package pins will not be saved\n");
    Table$str_set(&warned, ini_file, ini_file);
}

static OptionalPath_t get_package_install_location(Table_t *build_info, Path_t ini_file, const char *name,
                                                   Path_t store_root) {
    OptionalClosure_t by_line = Path$by_line(ini_file);
    if (by_line.fn == NULL) return NONE_PATH;
    OptionalText_t (*next_line)(void *) = by_line.fn;

    Text_t reformatted = EMPTY_TEXT;
    for (OptionalText_t line; (line = next_line(by_line.userdata)).tag != TEXT_NONE;) {
        if (Text$equal_values(line, Texts("[", name, "]"))) goto found_package;
        reformatted = Texts(reformatted, line, "\n");
    }
    return NONE_PATH;

found_package:;

    pkg_info_t pkg = {.name = name, .info = EMPTY_TABLE};
    for (OptionalText_t line; (line = next_line(by_line.userdata)).tag != TEXT_NONE;) {
        const char *line_str = Text$as_c_string(line);
        const char *key = NULL, *value = NULL;
        if (!strparse(line_str, &key, "=", &value)) {
            Table$str_set(&pkg.info, key, value);
        } else {
            break;
        }
    }
    bool had_digest = Table$str_get(pkg.info, "digest") != NULL;
    OptionalPath_t installed = try_install_package(ini_file, &pkg, true, store_root);
    if (installed == NULL) return NULL;

    if (!had_digest && Table$str_get(pkg.info, "digest") != NULL) {
        reformatted = Texts(reformatted, package_text(pkg), "\n");
        for (OptionalText_t line; (line = next_line(by_line.userdata)).tag != TEXT_NONE;) {
            reformatted = Texts(reformatted, line, "\n");
        }
        reformatted = Texts(Text$trim(reformatted, Text(" \r\n\t"), true, true), "\n");
        if (ini_is_writable(ini_file)) {
            Result_t result = Path$write(ini_file, reformatted, 0644);
            if (result.Failure.reason.tag != TEXT_NONE) {
                fail(result.Failure.reason);
            }
        } else {
            warn_ini_not_writable(ini_file);
        }
    }

    const char *digest = Table$str_get(pkg.info, "digest");
    Text_t digest_key = Texts("Package digest [", name, "]");
    Table$str_set(build_info, Text$as_c_string(digest_key), digest);

    Text_t source_key = Texts("Package source [", name, "]");
    const char *source = Table$str_get(pkg.info, "source");
    Table$str_set(build_info, Text$as_c_string(source_key), source);

    return installed;
}

OptionalPath_t find_installed_package(Table_t *build_info, ast_t *use) {
    const char *name = Match(use, Use)->path;
    Path_t using_file = Path$from_str(use->file->filename);
    Path_t store_root = package_store_root(using_file);
    OptionalPath_t installed = NONE_PATH;

    Path_t local_package = Path$sibling(using_file, Text("packages.ini"));
    installed = get_package_install_location(build_info, local_package, name, store_root);

    if (installed == NULL) {
        Path_t tomo_default_packages =
            Path$from_text(Texts(Text$from_str(TOMO_PATH), "/lib/tomo@", TOMO_VERSION, "/packages.ini"));
        installed = get_package_install_location(build_info, tomo_default_packages, name, store_root);
    }

    if (installed != NULL) create_binding_link(using_file, name, installed);
    return installed;
}

static bool parse_package_entry(Path_t ini_file, const char *name, pkg_info_t *pkg);

// The pinned digest for `name`, as resolved through the same packages.ini
// chain a `use` in `using_file` would consult -- parse-only, installing
// nothing. NULL if the package isn't pinned by digest (e.g. a
// directory-source package) or isn't found at all:
const char *find_pinned_digest(Path_t using_file, const char *name) {
    Path_t candidates[] = {
        Path$sibling(using_file, Text("packages.ini")),
        Path$from_text(Texts(Text$from_str(TOMO_PATH), "/lib/tomo@", TOMO_VERSION, "/packages.ini")),
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        pkg_info_t pkg = {.name = name, .info = EMPTY_TABLE};
        if (parse_package_entry(candidates[i], name, &pkg)) return Table$str_get(pkg.info, "digest");
    }
    return NULL;
}

// Parse [name]'s key=value entries from an ini file (without installing
// anything). Returns whether the section was found:
static bool parse_package_entry(Path_t ini_file, const char *name, pkg_info_t *pkg) {
    OptionalClosure_t by_line = Path$by_line(ini_file);
    if (by_line.fn == NULL) return false;
    OptionalText_t (*next_line)(void *) = by_line.fn;
    bool found = false;
    for (OptionalText_t line; (line = next_line(by_line.userdata)).tag != TEXT_NONE;) {
        if (!found) {
            if (Text$equal_values(line, Texts("[", name, "]"))) found = true;
            continue;
        }
        const char *key = NULL, *value = NULL;
        if (strparse(Text$as_c_string(line), &key, "=", &value)) break; // End of section
        Table$str_set(&pkg->info, key, value);
    }
    return found;
}

// Replace (or append) [name]'s entry in an ini file with the given info.
// Returns whether the file was actually written (false, with a warning
// printed, if `ini_file` isn't writable):
static bool rewrite_package_entry(Path_t ini_file, const char *name, pkg_info_t pkg) {
    Text_t out = EMPTY_TEXT;
    bool replaced = false;
    OptionalClosure_t by_line = Path$by_line(ini_file);
    if (by_line.fn != NULL) {
        OptionalText_t (*next_line)(void *) = by_line.fn;
        bool in_section = false;
        for (OptionalText_t line; (line = next_line(by_line.userdata)).tag != TEXT_NONE;) {
            if (in_section) {
                const char *key = NULL, *value = NULL;
                if (!strparse(Text$as_c_string(line), &key, "=", &value)) continue; // Drop the old entries
                in_section = false;
            }
            if (Text$equal_values(line, Texts("[", name, "]"))) {
                out = Texts(out, package_text(pkg));
                in_section = true, replaced = true;
                continue;
            }
            out = Texts(out, line, "\n");
        }
    }
    if (!replaced) {
        if (out.length > 0) out = Texts(Text$trim(out, Text(" \r\n\t"), false, true), Text("\n\n"));
        out = Texts(out, package_text(pkg));
    }
    if (!ini_is_writable(ini_file)) {
        warn_ini_not_writable(ini_file);
        return false;
    }
    Result_t result = Path$write(ini_file, out, 0644);
    if (result.Failure.reason.tag != TEXT_NONE) fail(result.Failure.reason);
    return true;
}

// Enumerate the [section] names in an ini file:
static List_t ini_section_names(Path_t ini_file) {
    List_t names = EMPTY_LIST;
    OptionalClosure_t by_line = Path$by_line(ini_file);
    if (by_line.fn == NULL) return names;
    OptionalText_t (*next_line)(void *) = by_line.fn;
    for (OptionalText_t line; (line = next_line(by_line.userdata)).tag != TEXT_NONE;) {
        const char *str = Text$as_c_string(line);
        size_t len = strlen(str);
        if (len >= 2 && str[0] == '[' && str[len - 1] == ']') {
            const char *name = Text$as_c_string(Text$from_strn(str + 1, len - 2));
            List$insert(&names, &name, I(0), sizeof(const char *));
        }
    }
    return names;
}

// Keep packages.ini's `unused=true` markers in sync with what the directory's
// code actually uses: entries whose binding name is not in `used_names` get
// marked, and the marker is removed from entries that are used again. The
// marker is informational (resolution ignores unknown keys); the file is only
// rewritten when a marker actually changes, since its modification time
// triggers rebuilds.
void mark_unused_packages(Path_t ini_file, Table_t used_names) {
    List_t sections = ini_section_names(ini_file);
    for (int64_t i = 0; i < (int64_t)sections.length; i++) {
        const char *name = *(const char **)(sections.data + i * sections.stride);
        pkg_info_t pkg = {.name = name, .info = EMPTY_TABLE};
        if (!parse_package_entry(ini_file, name, &pkg)) continue;
        bool used = Table$str_get(used_names, name) != NULL;
        bool marked = Table$str_get(pkg.info, "unused") != NULL;
        if (used == !marked) continue; // Already in the right state

        pkg_info_t updated = {.name = name, .info = EMPTY_TABLE};
        for (int64_t j = 0; j < (int64_t)pkg.info.entries.length; j++) {
            struct {
                const char *key, *value;
            } *entry = pkg.info.entries.data + j * pkg.info.entries.stride;
            if (!streq(entry->key, "unused")) Table$str_set(&updated.info, entry->key, entry->value);
        }
        if (!used) Table$str_set(&updated.info, "unused", "true");
        if (!rewrite_package_entry(ini_file, name, updated)) continue;
        if (used) print("Removed the unused=true marker for ", name, " in ", ini_file);
        else print("Marked the package ", name, " as unused=true in ", ini_file);
    }
}

// Extract a package source archive into dest, flattening a single top-level
// wrapper directory if there is one:
static void extract_package_archive(Path_t archive, Path_t dest) {
    Result_t result = Path$create_directory(dest, 0755, true);
    if (result.Failure.reason.tag != TEXT_NONE) fail("Failed to make directory: ", result.Failure.reason);

    if (Path$has_extension(archive, Text(".tar.gz")) || Path$has_extension(archive, Text(".tgz"))
        || Path$has_extension(archive, Text(".tar.xz")) || Path$has_extension(archive, Text(".txz"))
        || Path$has_extension(archive, Text(".tar"))) {
        xsystem("tar xf ", quoted(archive), " -C ", quoted(dest));
    } else if (Path$has_extension(archive, Text(".zip"))) {
        xsystem("unzip -q ", quoted(archive), " -d ", quoted(dest));
    } else {
        fail("Unsupported package filetype: ", archive);
    }

    List_t extracted = Path$children(dest, true);
    if (extracted.length == 1 && Path$is_directory(*(Path_t *)extracted.data, false)) {
        Path_t top_level = *(Path_t *)extracted.data;
        List_t contents = Path$children(top_level, true);
        for (int64_t i = 0; i < (int64_t)contents.length; i++) {
            Path_t p = *(Path_t *)(contents.data + i * contents.stride);
            Result_t moved = Path$move(p, Path$child(dest, Path$base_name(p)), false);
            if (moved.Failure.reason.tag != TEXT_NONE) fail(moved.Failure.reason);
        }
        Result_t removed = Path$remove(top_level, true);
        if (removed.Failure.reason.tag != TEXT_NONE) fail(removed.Failure.reason);
    }
}

// Vendor the named package into the current project's vendor/ directory,
// updating (or creating) its ./packages.ini entry. The normal mode copies the
// digest-verified archive and keeps the digest pin; editable mode extracts the
// sources into a directory (dropping the digest, since directory sources
// aren't digested) so the vendored copy can be modified freely. Previous
// sources are demoted to fallbacks.
void vendor_package(const char *name, bool editable) {
    Path_t cwd = Path$current_dir();
    Path_t ini = Path$child(cwd, Text("packages.ini"));
    pkg_info_t pkg = {.name = name, .info = EMPTY_TABLE};
    Path_t found_in = ini;
    if (!parse_package_entry(ini, name, &pkg)) {
        // Fall back to the compiler's default pins, copying the entry into
        // the project's own packages.ini:
        found_in = Path$from_text(Texts(Text$from_str(TOMO_PATH), "/lib/tomo@", TOMO_VERSION, "/packages.ini"));
        if (!parse_package_entry(found_in, name, &pkg))
            fail("There is no [", name, "] package entry in ", ini, " or ", found_in);
    }

    const char *source = Table$str_get(pkg.info, "source");
    if (source == NULL) fail("The package ", name, " has no source to vendor");
    if ((source[0] == '.' || source[0] == '/' || source[0] == '~')
        && Path$is_directory(Path$resolved(Path$from_str(source), cwd), true))
        fail("The package ", name, " is already vendored as a directory: ", source);

    // Make sure the package is installed, which computes/verifies the digest
    // and caches the verified archive:
    Path_t store_root = Path$child(Path$child(cwd, Text(".tomo")), Text("store"));
    OptionalPath_t installed = try_install_package(found_in, &pkg, true, store_root);
    if (installed == NULL) fail("Could not install package: ", name);
    const char *digest = Table$str_get(pkg.info, "digest");
    if (digest == NULL) fail("The package ", name, " has no digest to vendor by");

    Path_t cache_dir = download_cache_dir(Text$from_str(digest));
    List_t cached = Path$is_directory(cache_dir, true) ? Path$children(cache_dir, true) : EMPTY_LIST;
    if (cached.length != 1) fail("There is no cached archive for package ", name, " (digest ", digest, ")");
    Path_t archive = *(Path_t *)cached.data;

    Result_t result = Path$create_directory(Path$child(cwd, Text("vendor")), 0755, true);
    if (result.Failure.reason.tag != TEXT_NONE) fail("Failed to make vendor/ directory: ", result.Failure.reason);

    const char *new_source;
    Path_t link_target = installed;
    if (editable) {
        Path_t vendored = Path$child(Path$child(cwd, Text("vendor")), Text$from_str(name));
        if (Path$exists(vendored)) fail("There is already a vendored copy at ", vendored);
        extract_package_archive(archive, vendored);
        new_source = String("./vendor/", name);
        link_target = vendored;
    } else {
        Path_t vendored = Path$child(Path$child(cwd, Text("vendor")), Path$base_name(archive));
        xsystem("cp ", quoted(archive), " ", quoted(vendored));
        new_source = String("./vendor/", Text$as_c_string(Path$base_name(archive)));
    }

    // Rebuild the entry: digest (unless editable), the vendored source first,
    // then the previous sources demoted to fallbacks, then any other keys:
    pkg_info_t updated = {.name = name, .info = EMPTY_TABLE};
    if (!editable) Table$str_set(&updated.info, "digest", digest);
    Table$str_set(&updated.info, "source", new_source);
    int fallback = 2;
    for (int i = 1;; i++) {
        const char *key = i == 1 ? "source" : String("source-", i);
        const char *old = Table$str_get(pkg.info, key);
        if (old == NULL) break;
        if (!streq(old, new_source)) Table$str_set(&updated.info, String("source-", fallback++), old);
    }
    for (int64_t i = 0; i < (int64_t)pkg.info.entries.length; i++) {
        struct {
            const char *key, *value;
        } *entry = pkg.info.entries.data + i * pkg.info.entries.stride;
        if (streq(entry->key, "digest") || strncmp(entry->key, "source", strlen("source")) == 0) continue;
        Table$str_set(&updated.info, entry->key, entry->value);
    }
    rewrite_package_entry(ini, name, updated);
    // Record the .tomo/packages/<name> binding link right away (it would
    // otherwise only appear on the next build):
    create_binding_link(ini, name, link_target);
    print("Vendored ", name, " to \033[1m", new_source, "\033[m", editable ? " (editable, without a digest pin)" : "");

    // In editable mode the digest pin is gone, so the store entry it pointed
    // at is no longer used; clean it up. (Non-editable vendoring keeps the
    // digest, so its store entry stays in use.)
    if (editable) {
        Path_t old_entry = Path$child(store_root, Text$from_str(digest));
        if (Path$exists(old_entry)) {
            Result_t removed = Path$remove(old_entry, true);
            if (removed.Failure.reason.tag == TEXT_NONE) print("Removed the now-unused store entry: ", old_entry);
        }
    }
}

// Whether a packages.ini source string points into the project's ./vendor/
// directory:
static bool is_vendored_source(const char *source) {
    return source && (starts_with(source, "./vendor/") || starts_with(source, "vendor/"));
}

// Whether any package entry in the ini file (other than `except_name`) still
// references `source` as one of its sources:
static bool source_still_referenced(Path_t ini_file, const char *except_name, const char *source) {
    List_t sections = ini_section_names(ini_file);
    for (int64_t i = 0; i < (int64_t)sections.length; i++) {
        const char *section = *(const char **)(sections.data + i * sections.stride);
        if (streq(section, except_name)) continue;
        pkg_info_t pkg = {.name = section, .info = EMPTY_TABLE};
        if (!parse_package_entry(ini_file, section, &pkg)) continue;
        for (int j = 1;; j++) {
            const char *key = j == 1 ? "source" : String("source-", j);
            const char *value = Table$str_get(pkg.info, key);
            if (value == NULL) break;
            if (streq(value, source)) return true;
        }
    }
    return false;
}

// The inverse of vendor_package(): restore the named package's ./packages.ini
// entry to a non-vendored source (the first fallback source, or the compiler's
// default pin), re-install it into the project's .tomo/store/ (re-pinning the
// digest if editable vendoring dropped it), and delete the vendored copy.
void unvendor_package(const char *name) {
    Path_t cwd = Path$current_dir();
    Path_t ini = Path$child(cwd, Text("packages.ini"));
    pkg_info_t pkg = {.name = name, .info = EMPTY_TABLE};
    if (!parse_package_entry(ini, name, &pkg)) fail("There is no [", name, "] package entry in ", ini);

    const char *vendored_source = Table$str_get(pkg.info, "source");
    if (!is_vendored_source(vendored_source))
        fail("The package ", name, " is not vendored (source is ", vendored_source ? vendored_source : "missing", ")");
    Path_t vendored = Path$resolved(Path$from_str(vendored_source), cwd);
    bool editable = Path$is_directory(vendored, true);

    // Find the source to restore: the first non-vendored fallback source (the
    // rest stay as fallbacks), or failing that, the compiler's default pin:
    const char *digest = editable ? NULL : Table$str_get(pkg.info, "digest");
    const char *restored_source = NULL;
    List_t remaining_fallbacks = EMPTY_LIST;
    for (int i = 2;; i++) {
        const char *old = Table$str_get(pkg.info, String("source-", i));
        if (old == NULL) break;
        if (is_vendored_source(old)) continue; // Never restore to another vendored copy
        if (restored_source == NULL) restored_source = old;
        else List$insert(&remaining_fallbacks, &old, I(0), sizeof(const char *));
    }
    if (restored_source == NULL) {
        Path_t defaults = Path$from_text(Texts(Text$from_str(TOMO_PATH), "/lib/tomo@", TOMO_VERSION, "/packages.ini"));
        pkg_info_t default_pkg = {.name = name, .info = EMPTY_TABLE};
        if (!parse_package_entry(defaults, name, &default_pkg))
            fail("The package ", name, " has no fallback source to restore, and no [", name, "] entry in ", defaults);
        restored_source = Table$str_get(default_pkg.info, "source");
        if (restored_source == NULL) fail("The package ", name, " has no fallback source to restore");
        if (digest == NULL) digest = Table$str_get(default_pkg.info, "digest");
    }

    // Rebuild the entry: digest (if still known), the restored source, the
    // remaining fallbacks, then any other keys:
    pkg_info_t updated = {.name = name, .info = EMPTY_TABLE};
    if (digest) Table$str_set(&updated.info, "digest", digest);
    Table$str_set(&updated.info, "source", restored_source);
    for (int64_t i = 0; i < (int64_t)remaining_fallbacks.length; i++) {
        const char *old = *(const char **)(remaining_fallbacks.data + i * remaining_fallbacks.stride);
        Table$str_set(&updated.info, String("source-", i + 2), old);
    }
    for (int64_t i = 0; i < (int64_t)pkg.info.entries.length; i++) {
        struct {
            const char *key, *value;
        } *entry = pkg.info.entries.data + i * pkg.info.entries.stride;
        if (streq(entry->key, "digest") || strncmp(entry->key, "source", strlen("source")) == 0) continue;
        Table$str_set(&updated.info, entry->key, entry->value);
    }

    // Reinstall from the restored source, which re-verifies (and, if editable
    // vendoring dropped the digest pin, recomputes and re-pins) the digest:
    Path_t store_root = Path$child(Path$child(cwd, Text(".tomo")), Text("store"));
    OptionalPath_t installed = try_install_package(ini, &updated, true, store_root);
    if (installed == NULL) fail("Could not reinstall package ", name, " from ", restored_source);

    // A freshly recomputed digest lands at the end of the table; rebuild the
    // entry with the digest first, matching how vendoring writes it:
    if (digest == NULL && Table$str_get(updated.info, "digest") != NULL) {
        pkg_info_t reordered = {.name = name, .info = EMPTY_TABLE};
        Table$str_set(&reordered.info, "digest", Table$str_get(updated.info, "digest"));
        for (int64_t i = 0; i < (int64_t)updated.info.entries.length; i++) {
            struct {
                const char *key, *value;
            } *entry = updated.info.entries.data + i * updated.info.entries.stride;
            if (!streq(entry->key, "digest")) Table$str_set(&reordered.info, entry->key, entry->value);
        }
        updated = reordered;
    }

    rewrite_package_entry(ini, name, updated);
    // Point the .tomo/packages/<name> binding link back at the store entry:
    create_binding_link(ini, name, installed);

    // Delete the vendored copy (an extracted directory for editable vendoring,
    // otherwise an archive file -- kept if another entry still references it):
    if (editable || !source_still_referenced(ini, name, vendored_source)) {
        Result_t removed = Path$remove(vendored, true);
        if (removed.Failure.reason.tag != TEXT_NONE)
            fprint(stderr, "Warning: could not remove the vendored copy at ", vendored);
    }
    // Clean up the vendor/ directory itself if nothing is left in it:
    Path_t vendor_dir = Path$child(cwd, Text("vendor"));
    if (Path$is_directory(vendor_dir, true) && Path$children(vendor_dir, true).length == 0)
        Path$remove(vendor_dir, false);

    print("Unvendored ", name, ": restored to \033[1m", restored_source, "\033[m");
}
