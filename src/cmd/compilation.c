// The compilation engine driving transpiling, object compilation, executable
// linking, and package builds

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <gc.h>
#include <libgen.h>
#include <miniz.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../ast.h"
#include "../compile/cli.h"
#include "../compile/files.h"
#include "../compile/headers.h"
#include "../compile/text.h"
#include "../config.h"
#include "../naming.h"
#include "../packages.h"
#include "../parse/files.h"
#include "../sha256.h"
#include "../stdlib/bytes.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/enums.h"
#include "../stdlib/lists.h"
#include "../stdlib/optionals.h"
#include "../stdlib/paths.h"
#include "../stdlib/print.h"
#include "../stdlib/profiling.h"
#include "../stdlib/random.h"
#include "../stdlib/tables.h"
#include "../stdlib/text.h"
#include "../types.h"
#include "../util.h"
#include "common.h"
#include "compilation.h"

typedef struct {
    bool h : 1, c : 1, o : 1;
} staleness_t;

// The sentinel every compiled Tomo binary (and package.a) carries at the head
// of its embedded build-info blob. Scanned for both to print the info and to
// recognize a file as Tomo's own before uninstalling it.
static const char *const TOMO_BUILD_INFO_HEADER = "===== Begin Tomo Build Info =====";

// Whether a file's raw bytes contain `needle` anywhere. Used to recognize
// Tomo-produced artifacts (executables carry the build-info header; man pages
// carry TOMO_MANPAGE_MARKER) without parsing their on-disk format.
static bool file_contains(Path_t p, const char *needle) {
    p = Path$expand_home(p);
    int fd = open(Path$as_c_string(p), O_RDONLY);
    if (fd < 0) return false;
    struct stat sb;
    if (fstat(fd, &sb) != 0 || sb.st_size == 0) {
        close(fd);
        return false;
    }
    char *contents = mmap(NULL, (size_t)sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (contents == MAP_FAILED) return false;
    bool found = memmem(contents, (size_t)sb.st_size, needle, strlen(needle)) != NULL;
    munmap(contents, (size_t)sb.st_size);
    return found;
}

// Whether `p` is a compiled Tomo binary (carries the embedded build info).
bool is_tomo_binary(Path_t p) {
    return file_contains(p, TOMO_BUILD_INFO_HEADER);
}

// Whether `p` is a man page Tomo generated (carries TOMO_MANPAGE_MARKER).
bool is_tomo_manpage(Path_t p) {
    return file_contains(p, TOMO_MANPAGE_MARKER);
}

// The build-info blob lives in a named section (see compile_build_info()
// below), but rather than maintaining ELF/Mach-O/archive parsers just to find
// that section, the blob brackets itself with sentinel strings and this scans
// the raw bytes for them, which works uniformly on executables for any
// platform and on `ar` archives like package.a. Entries are NUL-separated on
// ELF (where the section is a string table) and newline-separated on Mach-O,
// so NULs print as newlines.
void print_build_info(Path_t p) {
    p = Path$expand_home(p);
    char *contents = NULL;
    struct stat sb;
    int fd = open(p, O_RDONLY);
    if (fd != -1 && fstat(fd, &sb) == 0) {
        contents = mmap(NULL, (size_t)sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    }
    if (contents == NULL) {
        fprint(stderr, "Could not open file: ", p);
        exit(1);
    }
    const char *contents_end = contents + sb.st_size;
    const char *start_header = TOMO_BUILD_INFO_HEADER;
    static const char *end_header = "===== End Tomo Build Info =====";
    bool found = false;
    for (const char *match = contents;
         (match = memmem(match, (size_t)(contents_end - match), start_header, strlen(start_header)));) {
        const char *info = match + strlen(start_header);
        if (info < contents_end && (*info == '\n' || *info == '\0')) info += 1;
        const char *info_end = memmem(info, (size_t)(contents_end - info), end_header, strlen(end_header));
        if (info_end == NULL) break;
        for (const char *c = info; c < info_end; c++)
            fputc(*c == '\0' ? '\n' : *c, stdout);
        match = info_end + strlen(end_header);
        found = true;
    }
    if (!found) fprint(stderr, "No Tomo build info found in: ", p);
    munmap(contents, (size_t)sb.st_size);
}

// --- Embedded source zips ---------------------------------------------------
// Every compiled executable embeds a zip of the sources needed to rebuild it:
// the program's .tm file, its transitive local imports, and each source
// directory's packages.ini (which pins the versions of any used packages).
// The zip is deterministic (sorted entries, no timestamps: miniz is built with
// MINIZ_NO_TIME) and is bracketed by magic header/footer strings so it can be
// located with a raw scan, the same way build info is. It lives in a retained
// section (.tomo.source / __TEXT,__tomo_source), so it can also be pulled out
// with `objcopy -O binary --only-section=.tomo.source` or `segedit`.
static const char *SOURCE_ZIP_HEADER = "===== Begin Tomo Source Zip =====\n";
static const char *SOURCE_ZIP_FOOTER = "\n===== End Tomo Source Zip =====\n";

static char *slurp_file(Path_t path, size_t *size) {
    int fd = open(path, O_RDONLY);
    struct stat sb;
    if (fd < 0) return NULL;
    if (fstat(fd, &sb) != 0) {
        close(fd);
        return NULL;
    }
    char *buf = GC_MALLOC_ATOMIC((size_t)sb.st_size + 1);
    for (ssize_t off = 0; off < (ssize_t)sb.st_size;) {
        ssize_t got = read(fd, buf + off, (size_t)(sb.st_size - off));
        if (got <= 0) {
            close(fd);
            return NULL;
        }
        off += got;
    }
    close(fd);
    *size = (size_t)sb.st_size;
    return buf;
}

static void write_all(int fd, const void *data, size_t size, Path_t path) {
    for (size_t off = 0; off < size;) {
        ssize_t wrote = write(fd, (const char *)data + off, size - off);
        if (wrote <= 0) print_err("Could not write to file: ", path);
        off += (size_t)wrote;
    }
}

static int compare_source_entries(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static bool is_build_artifact(Text_t filename) {
    return Text$ends_with(filename, Text(".a"), NULL) || Text$ends_with(filename, Text(".o"), NULL)
           || Text$ends_with(filename, Text(".so"), NULL) || Text$ends_with(filename, Text(".dylib"), NULL);
}

// Recursively add every file in `dir` to the zip's name->path table under
// `prefix`, skipping hidden files (like .tomo/), compiled artifacts, and
// symlinks (package binding links, since each linked package is embedded once,
// under its own packages/ entry, via the dependency graph):
static void add_dir_files(Table_t *files, Path_t dir, const char *prefix) {
    List_t children = Path$glob(Path$child(dir, Text("[!.]*")));
    for (int64_t i = 0; i < (int64_t)children.length; i++) {
        Path_t child = *(Path_t *)(children.data + i * children.stride);
        struct stat child_stat;
        if (lstat(child, &child_stat) != 0 || S_ISLNK(child_stat.st_mode)) continue;
        Text_t base = Path$base_name(child);
        const char *name = String(prefix, "/", Text$as_c_string(base));
        if (Path$is_directory(child, true)) add_dir_files(files, child, name);
        else if (!is_build_artifact(base)) Table$str_set(files, name, Path$as_c_string(child));
    }
}

static bool is_store_entry_dir(Path_t dir) {
    return Text$equal_values(Path$base_name(Path$parent(dir)), Text("store"))
           && Text$equal_values(Path$base_name(Path$parent(Path$parent(dir))), Text(".tomo"));
}

// Collect the binding names and (transitively) the store-entry digests that
// the .tm files in `dir` still depend on. Parse-only, so no installs or
// confirmation prompts can trigger during garbage collection:
static void collect_needed_packages(Path_t dir, Path_t store_root, Table_t *digests, Table_t *names) {
    List_t files = Path$glob(Path$child(dir, Text("*.tm")));
    for (int64_t i = 0; i < (int64_t)files.length; i++) {
        Path_t file = *(Path_t *)(files.data + i * files.stride);
        // A file that doesn't parse has no `use` statements to collect, and it
        // may well be unrelated to what's being built, and garbage collection
        // shouldn't fail the build over it, or report an error about a file the
        // user never asked to compile:
        parse_error_t parse_err = {};
        ast_t *ast = parse_file(Path$as_c_string(file), &parse_err);
        if (!ast) continue;
        for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
            if (stmt->ast->tag != Use) continue;
            DeclareMatch(use, stmt->ast, Use);
            if (use->what != USE_PACKAGE) continue;
            if (names) Table$str_set(names, use->path, "");
            const char *digest = find_pinned_digest(file, use->path);
            if (digest == NULL) continue;
            const char *hex = package_digest_hex(digest);
            if (Table$str_get(*digests, hex)) continue;
            Table$str_set(digests, hex, "");
            Path_t entry = package_store_path(store_root, digest);
            if (Path$is_directory(entry, true)) collect_needed_packages(entry, store_root, digests, NULL);
        }
    }
}

// Garbage-collect a source directory's .tomo/packages binding links and
// .tomo/store entries: anything no .tm file in the directory (transitively)
// uses anymore is removed. packages.ini pins and vendored sources are never
// touched, so a garbage-collected package reinstalls without any network
// access if its `use` comes back.
static void gc_package_dir(Path_t dir) {
    // Store entries are content-addressed and must never be modified (and
    // their dependencies live in the containing store, not their own):
    if (is_store_entry_dir(dir)) return;
    // The compiler's own system-wide default pins (TOMO_PATH/lib/tomo@VERSION/
    // packages.ini) are shared by every project that falls back to them and
    // must never be rewritten with unused=true markers based on one
    // directory's usage:
    Path_t tomo_lib_dir = Path$from_text(Texts(Text$from_str(TOMO_PATH), "/lib/tomo@", TOMO_VERSION));
    if (Enum$equal(&dir, &tomo_lib_dir, &Path$info)) return;
    Path_t tomo_root = tomo_root_for(dir);
    Path_t store_root = Path$child(tomo_root, Text("store"));
    Table_t digests = EMPTY_TABLE, names = EMPTY_TABLE;
    collect_needed_packages(dir, store_root, &digests, &names);
    mark_unused_packages(Path$child(dir, Text("packages.ini")), names);

    List_t links = Path$glob(Path$child(tomo_root, Text("packages/*")));
    for (int64_t i = 0; i < (int64_t)links.length; i++) {
        Path_t link = *(Path_t *)(links.data + i * links.stride);
        if (Table$str_get(names, Text$as_c_string(Path$base_name(link)))) continue;
        if (unlink(link) == 0)
            LOG(LOG_BUILD, "Removed unused package binding: ", Path$relative_to(link, Path$current_dir()));
    }
    List_t entries = Path$glob(Path$child(tomo_root, Text("store/*")));
    for (int64_t i = 0; i < (int64_t)entries.length; i++) {
        Path_t entry = *(Path_t *)(entries.data + i * entries.stride);
        if (Table$str_get(digests, Text$as_c_string(Path$base_name(entry)))) continue;
        Result_t removed = Path$remove(entry, true);
        if (removed.Failure.reason.tag == TEXT_NONE)
            LOG(LOG_BUILD, "Removed unused store entry: ", Path$relative_to(entry, Path$current_dir()));
    }
}

// Where a linked package's sources go in the embedded source zip: store
// entries go under store/<digest> (extracted into the pre-seeded
// .tomo/store/), while directory-source packages inside the project (e.g.
// vendored ones) keep their project-relative location, so the extracted
// tree's packages.ini still resolves them. NULL for a directory-source
// package outside the project, which has no resolvable location to embed at:
static const char *package_zip_prefix(Path_t pkg_dir, Path_t root) {
    if (is_store_entry_dir(pkg_dir)) return String("store/", Text$as_c_string(Path$base_name(pkg_dir)));
    Path_t rel = Path$relative_to(Path$resolved(pkg_dir, Path$current_dir()), Path$resolved(root, Path$current_dir()));
    return strncmp(rel, "..", 2) == 0 ? NULL : Path$as_c_string(rel);
}

// The zip entry recording which store entry each consumer's `use NAME`
// actually bound: one "<consumer>\t<name>\t<store dir>" line per direct use,
// where <consumer> is "" for the program's own files or the store-directory
// name of the package making the use. Extraction recreates the
// packages/<name> binding links from these lines (only for actually-used
// packages, since the packages.ini pins may cover transitive dependencies too):
static const char *SOURCE_LINKS_ENTRY = "packages.links";

// Record the packages that `consumer_file`'s use statements directly bind.
// The bound package is identified by its zip prefix ("store/<digest>" or a
// project-relative directory like "vendor/foo"):
static void add_package_bindings(env_t *env, Table_t *bindings, Path_t consumer_file, const char *consumer,
                                 Path_t root) {
    ast_t *ast = parse_file(Path$as_c_string(consumer_file), NULL);
    if (!ast) return;
    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        if (stmt->ast->tag != Use) continue;
        DeclareMatch(use, stmt->ast, Use);
        if (use->what != USE_PACKAGE) continue;
        OptionalPath_t installed = find_installed_package(env->build_info, stmt->ast);
        if (installed == NULL) continue;
        const char *prefix = package_zip_prefix(installed, root);
        if (prefix == NULL) continue; // Not embedded at a resolvable location
        Table$str_set(bindings, String(consumer, "\t", use->path, "\t", prefix), "");
    }
}

// Write the header + source zip + footer blob for `main_file` to `blob_path`:
void write_source_blob(env_t *env, Path_t main_file, Path_t blob_path) {
    Table_t dep_files = EMPTY_TABLE, to_link = EMPTY_TABLE;
    build_file_dependency_graph(env->build_info, main_file, &dep_files, &to_link);
    Path_t root = Path$parent(main_file);

    // Zip entry name -> source file path, deduplicated (several sources can
    // share a directory and thus a packages.ini). Files outside the project
    // directory are package sources (cross builds compile installed packages'
    // sources directly); their whole package is embedded below instead.
    Table_t files = EMPTY_TABLE;
    Table_t package_dirs = EMPTY_TABLE;
    Table_t bindings = EMPTY_TABLE;
    for (int64_t i = 0; i < (int64_t)dep_files.entries.length; i++) {
        struct {
            Path_t filename;
            staleness_t staleness;
        } *entry = dep_files.entries.data + i * dep_files.entries.stride;
        // Files inside a package store (cross builds compile installed
        // packages' sources directly) get their whole store entry embedded:
        OptionalPath_t store_entry = package_store_entry(entry->filename);
        if (store_entry != NULL) {
            Table$str_set(&package_dirs, Path$as_c_string(store_entry), Path$as_c_string(store_entry));
            continue;
        }
        Path_t rel = Path$relative_to(entry->filename, root);
        if (strncmp(rel, "..", 2) == 0) {
            fprint(stderr, "Warning: not embedding source file outside the project: ", entry->filename);
            continue;
        }
        Table$str_set(&files, rel, Path$as_c_string(entry->filename));
        add_package_bindings(env, &bindings, entry->filename, "", root);
        Path_t ini = Path$sibling(entry->filename, Text("packages.ini"));
        if (Path$is_file(ini, true))
            Table$str_set(&files, Path$as_c_string(Path$relative_to(ini, root)), Path$as_c_string(ini));
    }

    // Every installed package that gets linked in (to_link holds each one's
    // package.a, including packages used transitively by other packages):
    for (int64_t i = 0; i < (int64_t)to_link.entries.length; i++) {
        Text_t lib = *(Text_t *)(to_link.entries.data + i * to_link.entries.stride);
        if (!Text$ends_with(lib, Text("/package.a"), NULL)) continue;
        Path_t pkg_dir = Path$parent(Path$from_text(lib));
        Table$str_set(&package_dirs, Path$as_c_string(pkg_dir), Path$as_c_string(pkg_dir));
    }

    // Embed each package's full sources (and license files) under store/,
    // mirroring the installed content-addressed layout. `tomo -x` recreates
    // the packages/<name> binding links from the extracted packages.ini pins:
    for (int64_t i = 0; i < (int64_t)package_dirs.entries.length; i++) {
        struct {
            const char *key, *value;
        } *entry = package_dirs.entries.data + i * package_dirs.entries.stride;
        Path_t pkg_dir = Path$from_str(entry->key);
        const char *prefix = package_zip_prefix(pkg_dir, root);
        if (prefix == NULL) {
            fprint(stderr, "Warning: the directory-source package ", pkg_dir,
                   " lives outside the project, so the extracted source will not rebuild without it");
            prefix = String("store/", Text$as_c_string(Path$base_name(pkg_dir)));
        }
        add_dir_files(&files, pkg_dir, prefix);
        // Manifest links inside packages only apply to store entries;
        // directory-source packages' own binding links live in their .tomo
        // and are regenerated when the extracted tree builds:
        if (!is_store_entry_dir(pkg_dir)) continue;
        const char *store_name = Text$as_c_string(Path$base_name(pkg_dir));
        List_t pkg_files = Path$glob(Path$child(pkg_dir, Text("[!._0-9]*.tm")));
        for (int64_t j = 0; j < (int64_t)pkg_files.length; j++)
            add_package_bindings(env, &bindings, *(Path_t *)(pkg_files.data + j * pkg_files.stride), store_name, root);
    }

    // Also include the license texts shipped with the Tomo install (Tomo's own
    // license plus every statically linked library's) under licenses/:
    List_t licenses = Path$glob(Path$from_str(String(TOMO_PATH, "/share/licenses/tomo@", TOMO_VERSION, "/*")));
    for (int64_t i = 0; i < (int64_t)licenses.length; i++) {
        Path_t license = *(Path_t *)(licenses.data + i * licenses.stride);
        Table$str_set(&files, String("licenses/", Text$as_c_string(Path$base_name(license))),
                      Path$as_c_string(license));
    }

    // Sort by entry name so the zip is deterministic:
    int64_t num_files = files.entries.length;
    struct {
        const char *name, *path;
    } *entries = GC_MALLOC((size_t)num_files * sizeof(*entries));
    for (int64_t i = 0; i < num_files; i++)
        memcpy(&entries[i], files.entries.data + i * files.entries.stride, sizeof(entries[i]));
    qsort(entries, (size_t)num_files, sizeof(*entries), compare_source_entries);

    mz_zip_archive zip = {};
    if (!mz_zip_writer_init_heap(&zip, 0, 0)) print_err("Could not create source zip for ", main_file);
    for (int64_t i = 0; i < num_files; i++) {
        size_t size;
        char *contents = slurp_file(Path$from_str(entries[i].path), &size);
        if (contents == NULL) print_err("Could not read source file: ", entries[i].path);
        if (!mz_zip_writer_add_mem(&zip, entries[i].name, contents, size, MZ_BEST_COMPRESSION))
            print_err("Could not add ", entries[i].name, " to the source zip for ", main_file);
    }
    // The binding-links manifest (see SOURCE_LINKS_ENTRY):
    Text_t links = EMPTY_TEXT;
    for (int64_t i = 0; i < (int64_t)bindings.entries.length; i++) {
        struct {
            const char *key, *value;
        } *entry = bindings.entries.data + i * bindings.entries.stride;
        links = Texts(links, entry->key, "\n");
    }
    if (links.length > 0) {
        const char *links_str = Text$as_c_string(links);
        if (!mz_zip_writer_add_mem(&zip, SOURCE_LINKS_ENTRY, links_str, strlen(links_str), MZ_BEST_COMPRESSION))
            print_err("Could not add ", SOURCE_LINKS_ENTRY, " to the source zip for ", main_file);
    }

    void *zip_data;
    size_t zip_size;
    if (!mz_zip_writer_finalize_heap_archive(&zip, &zip_data, &zip_size))
        print_err("Could not finalize the source zip for ", main_file);
    mz_zip_writer_end(&zip);

    int fd = open(blob_path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0) print_err("Could not write source zip: ", blob_path);
    write_all(fd, SOURCE_ZIP_HEADER, strlen(SOURCE_ZIP_HEADER), blob_path);
    write_all(fd, zip_data, zip_size, blob_path);
    write_all(fd, SOURCE_ZIP_FOOTER, strlen(SOURCE_ZIP_FOOTER), blob_path);
    close(fd);
    mz_free(zip_data);
}

// Recreate the packages/<name> binding links for an extracted source tree
// from its packages.links manifest. Only links tomo itself synthesizes get
// created (validated names, targets constrained to extracted store entries):
static void create_extracted_links(Path_t outdir, char *manifest) {
    for (char *line = manifest; line && *line;) {
        char *end = strchr(line, '\n');
        if (end) *end = '\0';
        char *tab1 = strchr(line, '\t');
        char *tab2 = tab1 ? strchr(tab1 + 1, '\t') : NULL;
        if (!tab1 || !tab2) goto next_line;
        *tab1 = *tab2 = '\0';
        const char *consumer = line, *name = tab1 + 1, *dep = tab2 + 1;
        // The dep is a zip prefix: "store/<digest>" (extracted into
        // .tomo/store/) or a project-relative directory like "vendor/x":
        if (!(*name && *dep && !strchr(consumer, '/') && !strchr(name, '/') && dep[0] != '/' && !streq(consumer, "..")
              && !streq(name, "..") && !strstr(dep, ".."))) {
            goto next_line;
        }
        Path_t store = Path$child(Path$child(outdir, Text(".tomo")), Text("store"));
        Path_t dep_dir = strncmp(dep, "store/", strlen("store/")) == 0
                             ? Path$child(Path$child(outdir, Text(".tomo")), Text$from_str(dep))
                             : Path$child(outdir, Text$from_str(dep));
        Path_t link_dir = *consumer ? Path$child(Path$child(store, Text$from_str(consumer)), Text("packages"))
                                    : Path$child(Path$child(outdir, Text(".tomo")), Text("packages"));
        if (Path$is_directory(dep_dir, true) && (!*consumer || Path$is_directory(Path$parent(link_dir), true))) {
            Result_t result = Path$create_directory(link_dir, 0755, true);
            if (result.Failure.reason.tag == TEXT_NONE) {
                Path_t link = Path$child(link_dir, Text$from_str(name));
                const char *link_target = Path$relative_to(dep_dir, link_dir);
                unlink(link);
                if (symlink(link_target, link) == 0)
                    print("Linked    ", Path$relative_to(link, Path$current_dir()), " -> ", link_target);
            }
        }
    next_line:
        line = end ? end + 1 : NULL;
    }
}

// Extract the embedded source zip from a compiled binary into a
// "<binary name>-source" directory:
void extract_embedded_source(Path_t binary) {
    binary = Path$resolved(Path$expand_home(binary), Path$current_dir());
    char *contents = NULL;
    struct stat sb;
    int fd = open(binary, O_RDONLY);
    if (fd != -1 && fstat(fd, &sb) == 0) {
        contents = mmap(NULL, (size_t)sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    }
    if (contents == NULL) print_err("Could not open file: ", binary);
    const char *zip_start = memmem(contents, (size_t)sb.st_size, SOURCE_ZIP_HEADER, strlen(SOURCE_ZIP_HEADER));
    if (zip_start == NULL) print_err("No embedded Tomo source found in: ", binary);
    zip_start += strlen(SOURCE_ZIP_HEADER);
    const char *zip_end =
        memmem(zip_start, (size_t)(contents + sb.st_size - zip_start), SOURCE_ZIP_FOOTER, strlen(SOURCE_ZIP_FOOTER));
    if (zip_end == NULL) print_err("The embedded Tomo source in ", binary, " is truncated");

    mz_zip_archive zip = {};
    if (!mz_zip_reader_init_mem(&zip, zip_start, (size_t)(zip_end - zip_start), 0))
        print_err("The embedded Tomo source in ", binary, " is not a valid zip file");
    Path_t outdir = Path$sibling(binary, Texts(Path$base_name(binary), Text("-source")));
    bool extracted_packages = false;
    char *links_manifest = NULL;
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); i++) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat))
            print_err("The embedded Tomo source in ", binary, " is not a valid zip file");
        if (stat.m_is_directory) continue;
        if (stat.m_filename[0] == '/' || strstr(stat.m_filename, "..")) {
            fprint(stderr, "Skipping unsafe path in embedded source: ", stat.m_filename);
            continue;
        }
        size_t size;
        void *data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        if (data == NULL) print_err("Could not extract ", stat.m_filename, " from the source zip in ", binary);
        if (streq(stat.m_filename, SOURCE_LINKS_ENTRY)) {
            // Not a source file: consumed below to recreate the binding links.
            links_manifest = GC_MALLOC_ATOMIC(size + 1);
            memcpy(links_manifest, data, size);
            links_manifest[size] = '\0';
            mz_free(data);
            continue;
        }
        // Package sources extract into a project-shaped .tomo/store/, so the
        // extracted tree rebuilds as-is (offline: its store is pre-seeded):
        const char *out_name = stat.m_filename;
        if (strncmp(out_name, "store/", strlen("store/")) == 0) out_name = String(".tomo/", out_name);
        Path_t out = Path$child(outdir, Text$from_str(out_name));
        Path$create_directory(Path$parent(out), 0755, true);
        int out_fd = open(out, O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (out_fd < 0) print_err("Could not write extracted file: ", out);
        write_all(out_fd, data, size, out);
        close(out_fd);
        mz_free(data);
        print("Extracted ", Path$relative_to(out, Path$current_dir()));
        if (strncmp(stat.m_filename, "store/", strlen("store/")) == 0) extracted_packages = true;
    }
    mz_zip_reader_end(&zip);
    munmap(contents, (size_t)sb.st_size);

    if (links_manifest) create_extracted_links(outdir, links_manifest);

    if (extracted_packages)
        print("\nNote: the sources of the packages this program uses were extracted into\n"
              ".tomo/store/, so this program can be rebuilt as-is, without fetching the\n"
              "pinned package sources.");
}

// The C code embedding the source blob in a retained section on ELF targets
// (`.incbin` splices the blob file in verbatim). On Mach-O the blob is
// embedded at link time instead, via -sectcreate (see compile_executable):
static Text_t compile_source_asm(Path_t blob_path) {
    Text_t asm_text =
        Texts(".pushsection .tomo.source,\"aR\",%progbits\n"
              ".globl tomo_source\ntomo_source:\n"
              ".incbin ",
              Text$quoted(Text$from_str(Path$as_c_string(blob_path)), false, Text("\"")), "\n.popsection\n");
    return Texts("__asm__(", Text$quoted(asm_text, false, Text("\"")), ");\n");
}
// The C code defining the build-info blob, which lives in a named section so
// it can be retrieved with standard tools (readelf -p .tomo.build_info /
// otool -s __TEXT __tomo_build) in addition to `tomo --build-info`'s
// sentinel-based scan. On ELF targets the section is emitted with module-level
// asm so it can carry the SHF_STRINGS + SHF_GNU_RETAIN flags ("aRS"): one
// NUL-terminated string per entry, which `readelf -p` prints one line at a
// time, retained through the linker's default --gc-sections. Mach-O has no
// equivalent flags, so there the blob is a plain newline-separated char array.
static Text_t compile_build_info(env_t *env, const char *symbol) {
    if (link_macho) {
        Text_t blob = Text("===== Begin Tomo Build Info =====\n");
        for (int64_t i = 0; i < (int64_t)env->build_info->entries.length; i++) {
            struct {
                const char *key, *value;
            } *entry = env->build_info->entries.data + i * env->build_info->entries.stride;
            blob = Texts(blob, entry->key, ": ", entry->value, "\n");
        }
        blob = Texts(blob, "===== End Tomo Build Info =====\n");
        return Texts("const char ", symbol,
                     "[] __attribute__((used, visibility(\"default\"), section(\"__TEXT,__tomo_build\"))) = ",
                     Text$quoted(blob, false, Text("\"")), ";\n");
    }
    Text_t asm_text = Texts(".pushsection .tomo.build_info,\"aRS\",%progbits\n"
                            ".globl ",
                            symbol, "\n", symbol,
                            ":\n"
                            ".asciz \"===== Begin Tomo Build Info =====\"\n");
    for (int64_t i = 0; i < (int64_t)env->build_info->entries.length; i++) {
        struct {
            const char *key, *value;
        } *entry = env->build_info->entries.data + i * env->build_info->entries.stride;
        asm_text =
            Texts(asm_text, ".asciz ", Text$quoted(Texts(entry->key, ": ", entry->value), false, Text("\"")), "\n");
    }
    asm_text = Texts(asm_text, ".asciz \"===== End Tomo Build Info =====\"\n.popsection\n");
    return Texts("__asm__(", Text$quoted(asm_text, false, Text("\"")), ");\n");
}

// Launch a shell command with its stdout on a pipe (logging it like
// command_output does), but WITHOUT waiting for it, so several can run
// concurrently rather than one-at-a-time:
#define popen_logged(...)                                                                                              \
    ({                                                                                                                 \
        const char *_cmd = String(__VA_ARGS__);                                                                        \
        LOG(LOG_COMMANDS, "\033[94;1m", _cmd, "\033[m");                                                               \
        popen(_cmd, "r");                                                                                              \
    })

// Read one newline-trimmed line from a pipe, or NULL at EOF (e.g. when the
// command produced no output because it failed):
static char *read_pipe_line(FILE *f) {
    if (!f) return NULL;
    char *buf = GC_MALLOC_ATOMIC(1024);
    if (!fgets(buf, 1023, f)) return NULL;
    size_t n = strlen(buf);
    if (n > 0 && buf[n - 1] == '\n') buf[n - 1] = '\0';
    return buf;
}

static void add_git_info(env_t *env, Path_t dir) {
    // Launch both git queries up front so they run in parallel (each popen
    // spawns a shell + git). The commit hash and its time come from a single
    // `git log` (two output lines) instead of separate rev-parse + log calls,
    // so this is 2 concurrent invocations rather than 3 sequential ones:
    FILE *log = popen_logged("git -C '", dir, "' log -1 --format='%H%n%cI' 2>/dev/null");
    FILE *dirty = popen_logged("git -C '", dir, "' diff --quiet 2>/dev/null && echo false || echo true");

    char *commit = read_pipe_line(log);
    char *commit_time = commit ? read_pipe_line(log) : NULL;
    char *local_changes = read_pipe_line(dirty);
    if (log) pclose(log);
    if (dirty) pclose(dirty);

    // No commit line means this isn't a git repo (or git is unavailable); the
    // `diff` result is meaningless then, so drop it too:
    if (!commit) return;
    Table$str_set(env->build_info, "Git commit", commit);
    if (commit_time) Table$str_set(env->build_info, "Git commit time", commit_time);
    if (local_changes) Table$str_set(env->build_info, "Git local changes", local_changes);
}

void build_package(Path_t pkg_dir) {
    pkg_dir = Path$resolved(pkg_dir, Path$current_dir());
    if (!Path$is_directory(pkg_dir, true)) print_err("Not a valid directory: ", pkg_dir);

    List_t tm_files = Path$glob(Path$child(pkg_dir, Text("[!._0-9]*.tm")));
    // Cross-compiled package archives go in the per-target .tomo directory so
    // they don't clobber the native package.a:
    Path_t archive = cross_compiling ? build_file(Path$child(pkg_dir, Text("package.a")), "")
                                     : Path$child(pkg_dir, Text("package.a"));
    build_package_archive(pkg_dir, tm_files, archive);
}

void build_package_archive(Path_t pkg_dir, List_t tm_files, Path_t archive) {
    env_t *env = fresh_scope(global_env(source_mapping, instrument, debugging));
    List_t object_files = EMPTY_LIST, extra_ldlibs = EMPTY_LIST;

    compile_files(env, tm_files, &object_files, &extra_ldlibs, COMPILE_OBJ);
    if (is_stale_for_any(archive, object_files, false)) {
        add_git_info(env, pkg_dir);

        // Store metadata about the package's build information (in the
        // package's own .tomo directory, not the working directory's):
        Path_t build_info_obj = build_file(Path$child(pkg_dir, Text("__build_info")), ".o");
        {
            FILE *prog = run_cmd(cc, " ", cflags, " -x c -c - -o ", build_info_obj);
            if (!prog) print_err("Failed to run C compiler: ", cc);
            Text_t build_info = compile_build_info(env, "package_build_info");
            fputs(Text$as_c_string(build_info), prog);
            int status = pclose(prog);
            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) exit(EXIT_FAILURE);
        }

        FILE *prog = run_cmd(ar, " rcs '", archive, "' ", paths_str(object_files), " '", build_info_obj, "'");
        if (!prog) print_err("Failed to run `ar`");
        int status = pclose(prog);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) exit(EXIT_FAILURE);
        LOG(LOG_BUILD, "Compiled static package:\t", Path$relative_to(archive, Path$current_dir()));
        gc_package_dir(pkg_dir);
    } else {
        LOG(LOG_SKIP, "Unchanged: ", archive);
    }
}

// ---- atomic build artifacts -----------------------------------------------
//
// Artifacts are keyed by source path, so two `tomo` processes only ever write
// the same file when they're building a shared dependency, but when they do,
// an O_TRUNC-then-write leaves a window where a third process reads a truncated
// header or links a half-written object. Every artifact is therefore written to
// a sibling temp file and renamed into place: rename(2) within a directory is
// atomic, so a reader sees the old artifact or the new one, never a partial
// one. It also means an interrupted build can't leave behind a corrupt file
// that a later staleness check mistakes for a good one.
static Path_t artifact_temp(Path_t final) {
    return (Path_t)String(final, ".tmp", (int64_t)getpid());
}

static void commit_artifact(Path_t temp, Path_t final) {
    if (rename(Path$as_c_string(temp), Path$as_c_string(final)) != 0) {
        unlink(Path$as_c_string(temp));
        print_err("Failed to write ", final, ": ", strerror(errno));
    }
}

// --- Precompiled header -----------------------------------------------------
// Every generated .c opens with `#include <tomo.h>`, which pulls in ~40 more
// headers. Preprocessing and parsing them is a fixed per-translation-unit cost
// that dwarfs the code Tomo actually generates: measured on
// examples/learnxiny.tm, preloading a precompiled tomo.h instead cuts the
// object compile from 136ms to 119ms at -O0, and from 228ms to 218ms at -O3
// (where the optimizer, which a PCH does nothing for, dominates).
//
// A PCH is only usable by an invocation whose language and codegen options
// match the ones it was built with (clang rejects a mismatch outright rather
// than falling back), and Tomo's flags vary with the optimization level, the
// target platform, and which headers the installation has. So rather than
// shipping prebuilt ones (which cross-compiled distribution archives couldn't
// produce anyway: their `tomo` doesn't run on the build host), the PCH is
// built on demand and cached under a fingerprint covering everything that
// could invalidate it. A miss costs one extra ~65ms compile, once.

// Append each header matched by `pattern` to `fingerprint` with its size and
// mtime, so that reinstalling Tomo (or editing a header in place) names a
// different cache entry instead of tripping clang's "file has been modified
// since the precompiled header was built" error.
static const char *stamp_headers(const char *fingerprint, Path_t pattern) {
    List_t files = Path$glob(pattern);
    for (int64_t i = 0; i < (int64_t)files.length; i++) {
        Path_t file = *(Path_t *)(files.data + i * files.stride);
        struct stat sb;
        if (stat(Path$as_c_string(file), &sb) != 0) continue;
        fingerprint = String(fingerprint, "\n", file, ":", (int64_t)sb.st_size, ":", (int64_t)sb.st_mtime);
    }
    return fingerprint;
}

// Each reinstall of Tomo (or change of optimization level or target platform)
// names a new cache entry, and nothing else ever removes the old one, so keep
// only the most recently built few. Called on a cache miss, which is the only
// time the directory grows. The cap is generous enough for the levels and
// targets one checkout realistically compiles at.
#define MAX_CACHED_PCHS 8

static void prune_pch_cache(Path_t cache_dir, Path_t keep) {
    List_t cached = Path$glob(Path$child(cache_dir, Text("*.pch")));
    if ((int64_t)cached.length <= MAX_CACHED_PCHS) return;

    // Drop the oldest entries first: repeatedly evict the least recently
    // modified one that isn't the entry just built.
    for (int64_t remaining = (int64_t)cached.length; remaining > MAX_CACHED_PCHS; remaining--) {
        Path_t oldest = NONE_PATH;
        time_t oldest_time = 0;
        for (int64_t i = 0; i < (int64_t)cached.length; i++) {
            Path_t file = *(Path_t *)(cached.data + i * cached.stride);
            struct stat sb;
            if (file == NONE_PATH || streq(file, keep) || stat(Path$as_c_string(file), &sb) != 0) continue;
            if (oldest == NONE_PATH || sb.st_mtime < oldest_time) {
                oldest = file;
                oldest_time = sb.st_mtime;
            }
        }
        if (oldest == NONE_PATH) break;
        unlink(Path$as_c_string(oldest));
        // Tombstone it so the next pass doesn't pick it again:
        for (int64_t i = 0; i < (int64_t)cached.length; i++) {
            Path_t *file = (Path_t *)(cached.data + i * cached.stride);
            if (*file != NONE_PATH && streq(*file, oldest)) *file = NONE_PATH;
        }
    }
}

// The cached PCH to preload, or NONE if there isn't one to use. Memoized: the
// answer is the same for every translation unit in a build.
static OptionalPath_t precompiled_header = NONE_PATH;
static bool precompiled_header_resolved = false;
// A -D carrying the cache fingerprint, passed to both the PCH build and every
// compile that preloads it (they must agree on macro definitions or clang
// rejects the PCH). It exists to defeat `zig cc`'s own compilation cache,
// which is content-addressed: without it, zig answers a PCH build with an
// artifact it produced earlier from byte-identical headers, one that recorded
// their *previous* mtimes, and clang validates a PCH by mtime, so the reused
// artifact is rejected by every compile that tries to load it. Since the
// fingerprint changes whenever those mtimes do (a plain `cp` of the headers
// during `make install` is enough), keying zig's cache on it too keeps the two
// caches from disagreeing.
static const char *precompiled_header_stamp = "";

static OptionalPath_t get_precompiled_header(void) {
    if (precompiled_header_resolved) return precompiled_header;
    precompiled_header_resolved = true;

    // Escape hatch: a user whose install has drifted (hand-edited headers, a
    // partially-overwritten prefix) can turn the PCH off without reinstalling.
    const char *disabled = getenv("TOMO_NO_PCH");
    if (disabled && disabled[0] != '\0' && !streq(disabled, "0")) return NONE_PATH;

    Path_t include_dir = Path$from_str(String(lib_root, "/include/tomo@", TOMO_VERSION));
    Path_t umbrella = Path$child(include_dir, Text("tomo.h"));
    if (!Path$is_file(umbrella, true)) return NONE_PATH;

    // Everything that decides whether a given PCH is valid for this compile:
    // the exact invocation, and the headers it would be built from.
    const char *fingerprint = String(cc, " ", cflags, " -O", optimization);
    fingerprint = stamp_headers(fingerprint, Path$child(include_dir, Text("*.h")));
    // One level down covers every subdirectory tomo.h reaches into: tomo/ for
    // the standard library's own headers, plus gc/ and unistring/, which the
    // vendored gc.h and uni*.h pull in and which clang validates just as
    // strictly as the rest.
    fingerprint = stamp_headers(fingerprint, Path$child(Path$child(include_dir, Text("*")), Text("*.h")));

    // 64 bits of the digest is plenty to keep configurations apart, the same
    // truncation tomo_root_for() uses to name build-cache directories.
    // siphash24() would not do here: its key is randomized per process, so it
    // would name a different file every run.
    char digest[SHA256_HEX_SIZE];
    sha256_hex(fingerprint, strlen(fingerprint), digest);
    digest[16] = '\0';
    const char *name = String("tomo-", digest, ".pch");
    // An identifier, not a number: the value is never expanded, and a bare hex
    // digest would be a pp-number too wide for any integer type.
    precompiled_header_stamp = String(" -D__TOMO_PCH__=pch_", digest);
    Path_t cache_dir = Path$child(xdg_tomo_dir("XDG_CACHE_HOME", "~/.cache"), Text("pch"));
    Path_t pch = Path$child(cache_dir, Text$from_str(name));
    if (Path$is_file(pch, true)) {
        precompiled_header = pch;
        return precompiled_header;
    }

    // Cache miss: build it. A failure here is not fatal, since the same
    // headers are about to be compiled the ordinary way, which will report any
    // real error against the user's own source rather than against tomo.h.
    (void)Path$create_directory(cache_dir, 0755, /*recursive=*/true);
    Path_t temp = artifact_temp(pch);
    TOMO_PROFILE_SPAN_BEGIN(span, "cc precompile tomo.h");
    FILE *prog = run_cmd(cc, " ", cflags, " -O", optimization, precompiled_header_stamp, " -x c-header ", umbrella,
                         " -o ", temp);
    int status = prog ? pclose(prog) : -1;
    TOMO_PROFILE_SPAN_END(span);
    if (!prog || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        unlink(Path$as_c_string(temp));
        return NONE_PATH;
    }
    commit_artifact(temp, pch);
    LOG(LOG_BUILD, "Precompiled header:\t", pch);
    prune_pch_cache(cache_dir, pch);
    precompiled_header = pch;
    return precompiled_header;
}

// Build the precompiled header now, if it isn't cached already. Callers that
// fork a worker per file use this to make sure the one shared PCH is built
// once in the parent instead of N times in parallel children.
void warm_precompiled_header(void) {
    (void)get_precompiled_header();
}

// The `-include-pch <path>` fragment to splice into a compile command, or ""
// when no precompiled header is available.
static const char *pch_flag(void) {
    OptionalPath_t pch = get_precompiled_header();
    return pch == NONE_PATH ? "" : String(precompiled_header_stamp, " -include-pch '", pch, "'");
}

// Throw away the cached PCH and stop using one for the rest of this process.
static void discard_precompiled_header(void) {
    if (precompiled_header != NONE_PATH) unlink(Path$as_c_string(precompiled_header));
    precompiled_header = NONE_PATH;
    precompiled_header_resolved = true;
}

// Compile one translation unit: `cc <cflags> -O<level> [pch] <args>`. Returns
// whether it succeeded.
//
// clang refuses a precompiled header whose input files have changed since it
// was built, and that can happen underneath a build in progress: the cache
// entry is chosen from the headers' sizes and mtimes, but a `make install` (or
// anything else that rewrites the installed headers) landing between that
// check and the moment clang reads them leaves the entry stale. A stale cache
// must never be able to fail a compile that would otherwise succeed, so an
// attempt that used a PCH holds its diagnostics back; if it fails, the PCH is
// discarded and the compile re-run without one, which either succeeds or
// reports the program's real error itself, uncaptured and in colour.
static bool run_compile(const char *args, Path_t scratch) {
    const char *pch = pch_flag();
    if (pch[0] != '\0') {
        Path_t captured = (Path_t)String(scratch, ".err");
        FILE *prog = run_cmd(cc, " ", cflags, " -O", optimization, pch, " ", args, " 2>'", captured, "'");
        int status = prog ? pclose(prog) : -1;
        if (prog && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            // It worked, so anything it printed is a warning worth showing:
            FILE *diagnostics = fopen(Path$as_c_string(captured), "r");
            if (diagnostics) {
                for (int c; (c = fgetc(diagnostics)) != EOF;)
                    fputc(c, stderr);
                fclose(diagnostics);
            }
            unlink(Path$as_c_string(captured));
            return true;
        }
        unlink(Path$as_c_string(captured));
        discard_precompiled_header();
    }

    FILE *prog = run_cmd(cc, " ", cflags, " -O", optimization, " ", args);
    if (!prog) print_err("Failed to run C compiler: ", cc);
    int status = pclose(prog);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

void compile_files(env_t *env, List_t to_compile, List_t *object_files, List_t *extra_ldlibs, compile_mode_t mode) {
    Table_t to_link = EMPTY_TABLE;
    Table_t dependency_files = EMPTY_TABLE;
    for (int64_t i = 0; i < (int64_t)to_compile.length; i++) {

        Path_t filename = *(Path_t *)(to_compile.data + i * to_compile.stride);
        if (!Path$has_extension(filename, Text("tm")))
            print_err("Not a valid .tm file: \x1b[91;1m", filename, "\x1b[m");
        if (!Path$is_file(filename, true)) print_err("Couldn't find file: ", filename);
        TOMO_PROFILE_SPAN("dependency graph",
                          build_file_dependency_graph(env->build_info, filename, &dependency_files, &to_link));
    }

    // Make sure all files and dependencies have a .id file:
    for (int64_t i = 0; i < (int64_t)dependency_files.entries.length; i++) {
        struct {
            Path_t filename;
            staleness_t staleness;
        } *entry = (dependency_files.entries.data + i * dependency_files.entries.stride);

        Path_t id_file = build_file(entry->filename, ".id");
        if (Path$exists(id_file)) continue;

        static const char id_chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        int64_t num_id_chars = (int64_t)strlen(id_chars);
        char id_str[8];
        for (int j = 0; j < (int)sizeof(id_str); j++) {
            id_str[j] = id_chars[random_range(0, num_id_chars - 1)];
        }
        Text_t filename_id = Text("");
        Text_t base = Path$base_name(entry->filename);
        TextIter_t state = NEW_TEXT_ITER_STATE(base);
        for (int64_t j = 0; j < (int64_t)base.length; j++) {
            uint32_t c = Text$get_main_grapheme_fast(&state, j);
            if (c == '.') break;
            if (isalpha(c) || isdigit(c) || c == '_')
                filename_id = Texts(filename_id, Text$from_strn((char[]){(char)c}, 1));
        }
        // The id is *random*, so two processes racing here would invent two different ones and each bake its own
        // into the names it mangles, a wrong build and not just a duplicated one. Write the id to a temp file and
        // link() it into place: link is atomic and fails with EEXIST if someone else got there first, so everyone
        // ends up agreeing on a single id. Linking after the content is written means a reader never sees a
        // half-written id either.
        Path_t id_temp = artifact_temp(id_file);
        Path$write(id_temp, Texts(filename_id, Text("_"), Text$from_strn(id_str, sizeof(id_str))), 0644);
        if (link(Path$as_c_string(id_temp), Path$as_c_string(id_file)) != 0 && errno != EEXIST)
            print_err("Failed to create id file: ", id_file, ": ", Text$from_str(strerror(errno)));
        unlink(Path$as_c_string(id_temp));
    }

    // (Re)compile header files, eagerly for explicitly passed in files, lazily
    // for downstream dependencies:
    for (int64_t i = 0; i < (int64_t)dependency_files.entries.length; i++) {
        struct {
            Path_t filename;
            staleness_t staleness;
        } *entry = (dependency_files.entries.data + i * dependency_files.entries.stride);

        if (entry->staleness.h || clean_build) {
            TOMO_PROFILE_SPAN("transpile header", transpile_header(env, entry->filename));
            entry->staleness.o = true;
        } else {
            LOG(LOG_SKIP, "Unchanged: ", build_file(entry->filename, ".h"));
        }
    }

    env->imports = new (Table_t);

    struct child_s {
        struct child_s *next;
        pid_t pid;
    } *child_processes = NULL;

    // Transpiling and `zig cc -c` run in forked children, so their profile
    // spans never reach the parent's totals. When profiling, give the children
    // a pipe to serialize their spans back over (merged into the parent below).
    int profile_pipe[2] = {-1, -1};
    if (profiling && pipe(profile_pipe) != 0) profile_pipe[0] = profile_pipe[1] = -1;

    // Resolve (and, on a cold cache, build) the precompiled header here in the
    // parent: the children below all need the same one, and each forking its
    // own build would mean N redundant compiles of tomo.h. Only when there is
    // actually something to compile, though: an up-to-date build that forks
    // no children at all shouldn't pay for a cold cache.
    if (mode != COMPILE_C_FILES) {
        for (int64_t i = 0; i < (int64_t)dependency_files.entries.length; i++) {
            struct {
                Path_t filename;
                staleness_t staleness;
            } *entry = (dependency_files.entries.data + i * dependency_files.entries.stride);
            if (clean_build || entry->staleness.c || entry->staleness.h || entry->staleness.o
                || is_config_outdated(entry->filename)) {
                warm_precompiled_header();
                break;
            }
        }
    }

    // Drain the parent's stdio buffers before forking. When output is a pipe
    // rather than a tty it is block-buffered, so anything still buffered here
    // would be inherited by every child and printed a second time when they
    // flush on exit.
    fflush(NULL);

    // (Re)transpile and compile object files, eagerly for files explicitly
    // specified and lazily for downstream dependencies:
    for (int64_t i = 0; i < (int64_t)dependency_files.entries.length; i++) {
        struct {
            Path_t filename;
            staleness_t staleness;
        } *entry = (dependency_files.entries.data + i * dependency_files.entries.stride);
        // A stale config means retranspiling, not just recompiling: it covers
        // flags that change the generated code (--instrument, --source-mapping),
        // not only the ones passed to the C compiler.
        bool config_outdated = is_config_outdated(entry->filename);
        if (!clean_build && !entry->staleness.c && !entry->staleness.h && !entry->staleness.o && !config_outdated) {
            LOG(LOG_SKIP, "Unchanged: ", build_file(entry->filename, ".c"));
            LOG(LOG_SKIP, "Unchanged: ", build_file(entry->filename, ".o"));
            continue;
        }

        pid_t pid = fork();
        if (pid == 0) {
            // Start with a clean slate so we report only this child's work, not
            // the parent history inherited by copy-on-write:
            tomo_profile_reset();
            if (clean_build || entry->staleness.c || config_outdated)
                TOMO_PROFILE_SPAN("transpile code", transpile_code(env, entry->filename));
            else LOG(LOG_SKIP, "Unchanged: ", build_file(entry->filename, ".c"));
            if (mode != COMPILE_C_FILES) TOMO_PROFILE_SPAN("cc compile object", compile_object_file(entry->filename));
            // `tomo transpile` writes a .c but no .o, so it never refreshes the
            // .config that says which flags that .c was generated with. Drop
            // the stale one instead, or a later `tomo build` would see an
            // up-to-date .c with a matching .config and reuse code generated
            // under different flags (e.g. a --instrument transpile leaking
            // instrumentation into a plain build):
            else unlink(Path$as_c_string(build_file(entry->filename, ".config")));
            if (profile_pipe[1] >= 0) tomo_profile_serialize(profile_pipe[1]);
            fflush(NULL);
            _exit(EXIT_SUCCESS);
        }
        child_processes = new (struct child_s, .next = child_processes, .pid = pid);
    }

    // Close the parent's write end so the merge read below sees EOF once every
    // child has written and exited:
    if (profile_pipe[1] >= 0) close(profile_pipe[1]);

    for (; child_processes; child_processes = child_processes->next)
        wait_for_child_success(child_processes->pid);

    if (profile_pipe[0] >= 0) tomo_profile_merge(profile_pipe[0]); // closes the read end

    if (object_files) {
        for (int64_t i = 0; i < (int64_t)dependency_files.entries.length; i++) {
            struct {
                Path_t filename;
                staleness_t staleness;
            } *entry = (dependency_files.entries.data + i * dependency_files.entries.stride);
            Path_t path = entry->filename;
            path = build_file(path, ".o");
            List$insert(object_files, &path, I(0), sizeof(Path_t));
        }
    }
    if (extra_ldlibs) {
        for (int64_t i = 0; i < (int64_t)to_link.entries.length; i++) {
            Text_t lib = *(Text_t *)(to_link.entries.data + i * to_link.entries.stride);
            List$insert(extra_ldlibs, &lib, I(0), sizeof(Text_t));
        }
    }
}

bool is_config_outdated(Path_t path) {
    OptionalText_t config = Path$read(build_file(path, ".config"));
    if (config.tag == TEXT_NONE) return true;
    return !Text$equal_values(config, config_summary);
}

void build_file_dependency_graph(Table_t *build_info, Path_t path, Table_t *to_compile, Table_t *to_link) {
    if (Table$has_value(*to_compile, path, Table$info(&Path$info, &Byte$info))) return;

    staleness_t staleness = {
        .h = is_stale(build_file(path, ".h"), Path$sibling(path, Text("packages.ini")), true)
             || is_stale(build_file(path, ".h"), path, false)
             || is_stale(build_file(path, ".h"), build_file(path, ".id"), false),
        .c = is_stale(build_file(path, ".c"), Path$sibling(path, Text("packages.ini")), true)
             || is_stale(build_file(path, ".c"), path, false)
             || is_stale(build_file(path, ".c"), build_file(path, ".id"), false),
    };
    staleness.o = staleness.c || staleness.h || is_stale(build_file(path, ".o"), build_file(path, ".c"), false)
                  || is_stale(build_file(path, ".o"), build_file(path, ".h"), false);
    Table$set(to_compile, &path, &staleness, Table$info(&Path$info, &Byte$info));

    assert(Text$equal_values(Path$extension(path, true), Text("tm")));

    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    if (!ast) print_err("Could not parse file: ", path);

    for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
        ast_t *stmt_ast = stmt->ast;
        if (stmt_ast->tag != Use) continue;
        DeclareMatch(use, stmt_ast, Use);

        switch (use->what) {
        case USE_LOCAL: {
            Path_t dep_tm = Path$resolved(Path$from_str(use->path), Path$parent(path));
            if (!Path$is_file(dep_tm, true)) code_err(stmt_ast, "Not a valid file: ", dep_tm);
            if (is_stale(build_file(path, ".h"), dep_tm, false)) staleness.h = true;
            if (is_stale(build_file(path, ".c"), dep_tm, false)) staleness.c = true;
            if (staleness.c || staleness.h) staleness.o = true;
            Table$set(to_compile, &path, &staleness, Table$info(&Path$info, &Byte$info));
            build_file_dependency_graph(build_info, dep_tm, to_compile, to_link);
            break;
        }
        case USE_PACKAGE: {
            OptionalPath_t installed = find_installed_package(build_info, stmt_ast);
            if (!installed) code_err(stmt_ast, "I don't know where to find this package.");

            List_t children = Path$glob(Path$child(installed, Text("/[!._0-9]*.tm")));
            if (cross_compiling) {
                // Installed package archives were compiled for the native
                // platform, so cross builds recompile the package's modules from
                // their installed sources (into per-target .tomo directories)
                // and link those objects instead of package.a:
                for (int64_t i = 0; i < (int64_t)children.length; i++) {
                    Path_t *child = (Path_t *)(children.data + i * children.stride);
                    build_file_dependency_graph(build_info, *child, to_compile, to_link);
                }
                break;
            }

            Text_t lib = Texts(installed, "/package.a");
            Table$set(to_link, &lib, NULL, Table$info(&Text$info, &Void$info));

            for (int64_t i = 0; i < (int64_t)children.length; i++) {
                Path_t *child = (Path_t *)(children.data + i * children.stride);
                Table_t discarded = {.entries = EMPTY_LIST, .fallback = to_compile};
                build_file_dependency_graph(build_info, *child, &discarded, to_link);
            }
            break;
        }
        case USE_SHARED_OBJECT: {
            Text_t lib = Text$from_str(use->path);
            Table$set(to_link, &lib, NULL, Table$info(&Text$info, &Void$info));
            break;
        }
        case USE_ASM: {
            Path_t asm_path = Path$from_str(use->path);
            asm_path = Path$concat(Path$parent(path), asm_path);
            Text_t linker_text = Path$as_text(&asm_path, NULL, &Path$info);
            Table$set(to_link, &linker_text, NULL, Table$info(&Text$info, &Void$info));
            if (is_stale(build_file(path, ".o"), asm_path, false)) {
                staleness.o = true;
                Table$set(to_compile, &path, &staleness, Table$info(&Path$info, &Byte$info));
            }
            break;
        }
        case USE_HEADER:
        case USE_C_CODE: {
            if (use->path[0] == '<') break;

            Path_t dep_path = Path$resolved(Path$from_str(use->path), Path$parent(path));
            if (is_stale(build_file(path, ".o"), dep_path, false)) {
                staleness.o = true;
                Table$set(to_compile, &path, &staleness, Table$info(&Path$info, &Byte$info));
            }
            break;
        }
        default: break;
        }
    }
}

time_t latest_included_modification_time(Path_t path) {
    static Table_t c_modification_times = EMPTY_TABLE;
    const TypeInfo_t time_info = {.size = sizeof(time_t), .align = __alignof__(time_t), .tag = OpaqueInfo};
    time_t *cached_latest = Table$get(c_modification_times, &path, Table$info(&Path$info, &time_info));
    if (cached_latest) return *cached_latest;

    struct stat s;
    time_t latest = 0;
    if (stat(Path$as_c_string(path), &s) == 0) latest = s.st_mtime;
    Table$set(&c_modification_times, &path, &latest, Table$info(&Path$info, &time_info));

    OptionalClosure_t by_line = Path$by_line(path);
    if (by_line.fn == NULL) return 0;
    OptionalText_t (*next_line)(void *) = by_line.fn;
    Path_t parent = Path$parent(path);
    bool allow_dot_include = Path$has_extension(path, Text("s")) || Path$has_extension(path, Text("S"));
    for (OptionalText_t line; (line = next_line(by_line.userdata)).tag != TEXT_NONE;) {
        line = Text$trim(line, Text(" \t"), true, false);
        if (!Text$starts_with(line, Text("#include"), NULL)
            && !(allow_dot_include && Text$starts_with(line, Text(".include"), NULL)))
            continue;

        // Check for `"` after `#include` or `.include` and some spaces:
        if (!Text$starts_with(Text$trim(Text$from(line, I(9)), Text(" \t"), true, false), Text("\""), NULL)) continue;

        List_t chunks = Text$split(line, Text("\""));
        if (chunks.length < 3) // Should be `#include "foo" ...` -> ["#include ", "foo", "..."]
            continue;

        Text_t included = *(Text_t *)(chunks.data + 1 * chunks.stride);
        Path_t included_path = Path$resolved(Path$from_text(included), parent);
        time_t included_time = latest_included_modification_time(included_path);
        if (included_time > latest) {
            latest = included_time;
            Table$set(&c_modification_times, &path, &latest, Table$info(&Path$info, &time_info));
        }
    }
    return latest;
}

bool is_stale(Path_t path, Path_t relative_to, bool ignore_missing) {
    struct stat target_stat;
    if (stat(Path$as_c_string(path), &target_stat) != 0) {
        if (ignore_missing) return false;
        return true;
    }

#ifdef __linux__
    // Any file older than the compiler is stale:
    if (target_stat.st_mtime < compiler_stat.st_mtime) return true;
#endif

    if (Path$has_extension(relative_to, Text("c")) || Path$has_extension(relative_to, Text("h"))
        || Path$has_extension(relative_to, Text("s")) || Path$has_extension(relative_to, Text("S"))) {
        time_t mtime = latest_included_modification_time(relative_to);
        return target_stat.st_mtime < mtime;
    }

    struct stat relative_to_stat;
    if (stat(Path$as_c_string(relative_to), &relative_to_stat) != 0) {
        if (ignore_missing) return false;
        print_err("File doesn't exist: ", relative_to);
    }
    return target_stat.st_mtime < relative_to_stat.st_mtime;
}

bool is_stale_for_any(Path_t path, List_t relative_to, bool ignore_missing) {
    for (int64_t i = 0; i < (int64_t)relative_to.length; i++) {
        Path_t r = *(Path_t *)(relative_to.data + i * relative_to.stride);
        if (is_stale(path, r, ignore_missing)) return true;
    }
    return false;
}

void transpile_header(env_t *base_env, Path_t path) {
    Path_t h_filename = build_file(path, ".h");
    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    if (!ast) print_err("Could not parse file: ", path);

    env_t *module_env = load_module_env(base_env, ast);
    Text_t h_code = compile_file_header(module_env, Path$resolved(h_filename, Path$from_str(".")), ast);

    Path_t h_temp = artifact_temp(h_filename);
    FILE *header = fopen(Path$as_c_string(h_temp), "w");
    if (!header) print_err("Failed to open header file: ", h_temp);
    Text$print(header, h_code);
    if (fclose(header) == -1) print_err("Failed to write header file: ", h_temp);
    commit_artifact(h_temp, h_filename);

    LOG(LOG_BUILD, "Transpiled header:\t", Path$relative_to(h_filename, Path$current_dir()));
}

void transpile_code(env_t *base_env, Path_t path) {
    Path_t c_filename = build_file(path, ".c");
    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    if (!ast) print_err("Could not parse file: ", path);

    env_t *module_env = load_module_env(base_env, ast);
    Text_t c_code = compile_file(module_env, ast);

    binding_t *main_binding = get_binding(module_env, "main");
    bool has_main = main_binding && main_binding->type->tag == FunctionType;
    cli_command_def_t *subcommands = get_cli_subcommands(module_env, ast);
    if (has_main) {
        type_t *ret = Match(main_binding->type, FunctionType)->ret;
        if (ret->tag != VoidType && ret->tag != AbortType)
            compiler_err(ast->file, ast->start, ast->end, "The main() function in this file has a return type of ",
                         type_to_text(ret), ", but it should not have any return value!");
    }

    if (has_main || subcommands) {
        Text_t entry = namespace_name(module_env, module_env->namespace, Text("main"));
        Text_t dispatch = compile_cli_dispatch(module_env, ast, subcommands, entry);
        // The generated command-line wrapper is code the program never wrote,
        // so it must not inherit the .tm line numbering compile_file() opens
        // the file with. Left alone, every line of the wrapper claims to be a
        // line of the .tm file, counting on from that opening `#line 1`,
        // and a debugger stopped in it reports whichever Tomo code happens to
        // sit at those line numbers. Point it back at the C file it actually
        // is, which means knowing how many lines come before it.
        if (module_env->do_source_mapping) {
            int64_t line = 1;
            char last = '\n';
            for (const char *p = Text$as_c_string(c_code); *p; p++) {
                if (*p == '\n') line += 1;
                last = *p;
            }
            // A `#line` is a preprocessor directive, so it has to start its own
            // line. If the code so far didn't end with a newline, break the
            // line first (which puts the directive one line further down).
            Text_t newline = EMPTY_TEXT;
            if (last != '\n') {
                newline = Text("\n");
                line += 1;
            }
            // `line` is now the directive's own line, and a `#line` numbers the
            // line after itself:
            dispatch =
                Texts(newline, "#line ", line + 1, " ", quoted_str(Path$as_c_string(c_filename)), "\n", dispatch);
        }
        c_code = Texts(c_code, dispatch);
    }

    Path_t c_temp = artifact_temp(c_filename);
    FILE *c_file = fopen(Path$as_c_string(c_temp), "w");
    if (!c_file) print_err("Failed to write C file: ", c_temp);
    Text$print(c_file, c_code);
    if (fclose(c_file) == -1) print_err("Failed to output C code to ", c_temp);
    commit_artifact(c_temp, c_filename);

    LOG(LOG_BUILD, "Transpiled code:\t", Path$relative_to(c_filename, Path$current_dir()));
}

// The first time the bundled Zig toolchain compiles anything on a machine, it
// builds its libc (musl, compiler-rt, etc.) from source and stores it in its
// global cache; that one-time setup takes tens of seconds and can look like a
// hang. Detect a missing cache and print a notice so users know what's
// happening. ZIG_GLOBAL_CACHE_DIR is always set: main() points it into Tomo's
// own cache directory unless the environment already had it.
static void warn_if_first_compile(void) {
    static bool already_checked = false;
    if (already_checked) return;
    already_checked = true;

    const char *dir = getenv("ZIG_GLOBAL_CACHE_DIR");
    if (dir == NULL || dir[0] == '\0') return;

    struct stat st;
    if (stat(dir, &st) != 0) {
        fprint(stderr, USE_COLOR ? "\x1b[93;1m" : "",
               "First compile on this machine: the bundled Zig toolchain is building its libc cache "
               "(a one-time setup that takes a little while)...",
               USE_COLOR ? "\x1b[m" : "");
        fflush(stderr);
    }
}

void compile_object_file(Path_t path) {
    warn_if_first_compile();
    Path_t obj_file = build_file(path, ".o");
    Path_t c_file = build_file(path, ".c");

    Path_t obj_temp = artifact_temp(obj_file);
    if (!run_compile(String("-c ", c_file, " -o ", obj_temp), obj_temp)) {
        unlink(Path$as_c_string(obj_temp)); // don't leave a partial object looking like a finished one
        exit(EXIT_FAILURE);
    }
    commit_artifact(obj_temp, obj_file);

    // After the object, so a reader that sees a fresh .config always finds a finished .o behind it:
    Path_t config_file = build_file(path, ".config");
    Path_t config_temp = artifact_temp(config_file);
    Path$write(config_temp, config_summary, 0644);
    commit_artifact(config_temp, config_file);

    LOG(LOG_BUILD, "Compiled object:\t", Path$relative_to(obj_file, Path$current_dir()));
}

Path_t compile_executable(env_t *base_env, Path_t path, Path_t exe_path, List_t object_files, List_t extra_ldlibs,
                          bool embed_git_info) {
    warn_if_first_compile();
    ast_t *ast;
    TOMO_PROFILE_SPAN("parse (main)", ast = parse_file(Path$as_c_string(path), NULL));
    if (!ast) print_err("Could not parse file ", path);
    env_t *env;
    TOMO_PROFILE_SPAN("load module env", env = load_module_env(base_env, ast));
    binding_t *main_binding = get_binding(env, "main");
    cli_command_def_t *subcommands = get_cli_subcommands(env, ast);
    if ((main_binding && main_binding->type->tag == FunctionType) || subcommands) {
        Path_t manpage_file = build_file(Path$with_extension(path, Text(".1"), true), "");
        if (clean_build || !Path$is_file(manpage_file, true) || is_stale(manpage_file, path, true)) {
            Text_t manpage = compile_manpage(Path$base_name(exe_path), ast,
                                             main_binding && main_binding->type->tag == FunctionType
                                                 ? Match(main_binding->type, FunctionType)->args
                                                 : NULL,
                                             subcommands);
            Path$write(manpage_file, manpage, 0644);
            LOG(LOG_BUILD, "Wrote manpage:\t", Path$relative_to(manpage_file, Path$current_dir()));
        } else {
            LOG(LOG_SKIP, "Unchanged: ", manpage_file);
        }
    }

    // Rebuilding is also needed if any linked package archive (like an
    // edited vendored package's recompiled package.a) is newer than the
    // executable:
    List_t linked_archives = EMPTY_LIST;
    for (int64_t i = 0; i < (int64_t)extra_ldlibs.length; i++) {
        Text_t *lib = (Text_t *)(extra_ldlibs.data + i * extra_ldlibs.stride);
        if (!Text$ends_with(*lib, Text(".a"), NULL)) continue;
        Path_t archive_path = Path$from_text(*lib);
        List$insert(&linked_archives, &archive_path, I(0), sizeof(Path_t));
    }

    if (!clean_build && Path$is_file(exe_path, true) && !is_config_outdated(path)
        && !is_stale_for_any(exe_path, object_files, false) && !is_stale_for_any(exe_path, linked_archives, true)
        && !is_stale(exe_path, Path$sibling(path, Text("packages.ini")), true)) {
        LOG(LOG_SKIP, "Unchanged: ", exe_path);
        return exe_path;
    }

    // Git provenance (commit, dirty flag) is embedded in the binary's
    // build_info. It costs a few git subprocesses, so skip it for ephemeral
    // `tomo run`/`eval` executables, where the metadata is thrown away with the
    // binary; only persistent `build`/`install` artifacts pay for it:
    if (embed_git_info) TOMO_PROFILE_SPAN("git info", add_git_info(env, Path$parent(path)));

    // Zip up the program's sources for embedding into the executable:
    Path_t source_blob = build_file(path, ".source.zip");
    TOMO_PROFILE_SPAN("source blob", write_source_blob(env, path, source_blob));

    // An `--instrument` binary starts profiling before anything else runs and
    // prints its report at exit (PROFILE=0 opts out, PROFILE_FILE redirects).
    // Its command-line arguments are left entirely alone. See
    // tomo_profile_start() in src/stdlib/profiling.c.
    Text_t profiler_decl = instrument ? Text("extern void tomo_profile_start(void);\n") : EMPTY_TEXT;
    Text_t start_profiler = instrument ? Text("\ttomo_profile_start();\n") : EMPTY_TEXT;

    Text_t program;
    if ((main_binding && main_binding->type->tag == FunctionType) || subcommands) {
        Text_t entry = main_binding && main_binding->type->tag == FunctionType
                           ? main_binding->code
                           : namespace_name(env, env->namespace, Text("main"));
        program =
            Texts("extern int parse_and_run$$", entry, "(int argc, char *argv[]);\n", profiler_decl,
                  "__attribute__ ((noinline))\n"
                  "int main(int argc, char *argv[]) {\n",
                  start_profiler, "\treturn parse_and_run$$", entry,
                  "(argc, argv);\n"
                  "}\n",
                  compile_build_info(env, "build_info"), link_macho ? EMPTY_TEXT : compile_source_asm(source_blob));
    } else {
        program =
            Texts("extern void ", namespace_name(env, env->namespace, Text("$initialize")),
                  "(void);\n"
                  "extern void tomo_init(void);\n",
                  profiler_decl,
                  "__attribute__ ((noinline))\n"
                  "int main(int argc, char *argv[]) {\n",
                  start_profiler, "tomo_init();\n", namespace_name(env, env->namespace, Text("$initialize")),
                  "();\n"
                  "\n",
                  "return 0;\n"
                  "}\n",
                  compile_build_info(env, "build_info"), link_macho ? EMPTY_TEXT : compile_source_asm(source_blob));
    }
    Path_t runner_file = build_file(path, ".runner.c");
    Path$write(runner_file, program, 0644);

    // Libraries bundled with the Tomo toolchain: every program links the full
    // vendored archives (below), so a package's `use -lgmp` etc. must not
    // become a -l flag, since no system copies exist (the toolchain uses its
    // own static musl builds):
    static const char *bundled_libs[] = {"-lgc", "-lgmp", "-lunistring", "-lbacktrace", "-lm", "-lunwind"};

    // .a archive files need to go later in the positional order:
    List_t archives = EMPTY_LIST;
    for (int64_t i = 0; i < (int64_t)extra_ldlibs.length;) {
        Text_t *lib = (Text_t *)(extra_ldlibs.data + i * extra_ldlibs.stride);
        bool bundled = false;
        for (size_t j = 0; j < sizeof(bundled_libs) / sizeof(bundled_libs[0]); j++)
            bundled = bundled || Text$equal_values(*lib, Text$from_str(bundled_libs[j]));
        if (bundled) {
            List$remove_at(&extra_ldlibs, I(i + 1), I(1), sizeof(Text_t));
        } else if (Text$ends_with(*lib, Text(".a"), NULL)) {
            List$insert(&archives, lib, I(0), sizeof(Text_t));
            List$remove_at(&extra_ldlibs, I(i + 1), I(1), sizeof(Text_t));
        } else {
            i += 1;
        }
    }

    // The vendored static libraries that libtomo (and any package using their
    // headers) is linked against:
    Text_t vendor_dir = Texts(lib_root, "/lib/tomo@", TOMO_VERSION, "/vendor");
    Text_t vendor_archives = Texts(" ", vendor_dir, "/libgc.a ", vendor_dir, "/libgmp.a ", vendor_dir,
                                   "/libunistring.a ", vendor_dir, "/libbacktrace.a");

    // The debug-stripped zig C runtime replacing the libc that -nostdlib told
    // zig not to add (see tomo_configure()). Last, like an implicit libc; the
    // set of archives varies by zig version, so link whichever were staged:
    if (zig_libc_dir.length > 0) {
        static const char *runtime_archives[] = {"libc.a", "libcompiler_rt.a", "libzigc.a", "libunwind.a"};
        for (size_t i = 0; i < sizeof(runtime_archives) / sizeof(runtime_archives[0]); i++) {
            const char *archive = String(zig_libc_dir, "/", runtime_archives[i]);
            if (Path$is_file(Path$from_str(archive), true))
                vendor_archives = Texts(vendor_archives, Text(" '"), Text$from_str(archive), Text("'"));
        }
    }

    // On Mach-O the source blob is embedded by the linker rather than asm:
    Text_t source_section_flag =
        link_macho ? Texts(" '-Wl,-sectcreate,__TEXT,__tomo_source,", Text$from_str(Path$as_c_string(source_blob)), "'")
                   : EMPTY_TEXT;

    TOMO_PROFILE_SPAN_BEGIN(link_span, "cc link executable");
    FILE *runner = run_cmd( // Invoke C compiler
        cc,
        // C flags:
        " ", cflags, " -O", optimization,
        // Linker flags and dynamically linked shared packages (link_optimizations
        // holds the size-reducing flags that optimized builds add, empty for the
        // fast run/eval path):
        " ", ldflags, link_optimizations, source_section_flag, " ", ldlibs, " ", list_text(extra_ldlibs),
        // Object files:
        " ", paths_str(object_files),
        // Input file:
        " ", runner_file,
        // Statically linked archive files (must come after runner). No archive
        // grouping is needed for circular dependencies among packages: zig links
        // with lld, which resolves archive members iteratively.
        " ", list_text(archives),
        // Tomo static library (Mach-O linking has no --no-whole-archive):
        link_macho ? "" : " -Wl,--no-whole-archive", " ", lib_root, "/lib/tomo@", TOMO_VERSION, "/libtomo.a",
        vendor_archives,
        // Output file:
        " -o ", exe_path);

    Text$print(runner, program);
    int status = pclose(runner);
    TOMO_PROFILE_SPAN_END(link_span);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) exit(EXIT_FAILURE);

    LOG(LOG_BUILD, "Compiled executable:\t", Path$relative_to(exe_path, Path$current_dir()));
    gc_package_dir(Path$parent(path));
    return exe_path;
}

Path_t build_test_runner(Path_t path, List_t object_files, List_t extra_ldlibs, Text_t test_source) {
    Path_t test_c = build_file(path, ".test.c");
    Path$write(test_c, test_source, 0644);
    Path_t test_o = build_file(path, ".test.o");
    Path_t exe_path = build_file(path, ".test-runner");

    // Compile the runner to an object with clean cflags first: the link line
    // below carries -nostdlib and other ldflags that break system-header search
    // (<math.h> etc.) if the .c were compiled inline like compile_executable's
    // trivial runner.c (which doesn't include <tomo.h>).
    if (!run_compile(String("-c ", test_c, " -o ", test_o), test_o)) exit(EXIT_FAILURE);

    // The runner TU already contains the module's full code (so tests can reach
    // its private helpers), so drop the module's own object file from the link
    // to avoid duplicate definitions of its public symbols. Compare resolved
    // absolute paths, since the dependency-graph objects and build_file(path)
    // can carry different (relative vs absolute) representations of the same
    // file:
    Path_t cwd = Path$current_dir();
    const char *module_obj = Path$as_c_string(Path$resolved(build_file(path, ".o"), cwd));
    List_t link_objects = EMPTY_LIST;
    for (int64_t i = 0; i < (int64_t)object_files.length; i++) {
        Path_t *obj = (Path_t *)(object_files.data + i * object_files.stride);
        if (!streq(Path$as_c_string(Path$resolved(*obj, cwd)), module_obj))
            List$insert(&link_objects, obj, I(0), sizeof(Path_t));
    }
    object_files = link_objects;

    // Same archive juggling as compile_executable: drop the always-bundled libs
    // and push .a archives after the object files in link order.
    static const char *bundled_libs[] = {"-lgc", "-lgmp", "-lunistring", "-lbacktrace", "-lm", "-lunwind"};
    List_t archives = EMPTY_LIST;
    for (int64_t i = 0; i < (int64_t)extra_ldlibs.length;) {
        Text_t *lib = (Text_t *)(extra_ldlibs.data + i * extra_ldlibs.stride);
        bool bundled = false;
        for (size_t j = 0; j < sizeof(bundled_libs) / sizeof(bundled_libs[0]); j++)
            bundled = bundled || Text$equal_values(*lib, Text$from_str(bundled_libs[j]));
        if (bundled) {
            List$remove_at(&extra_ldlibs, I(i + 1), I(1), sizeof(Text_t));
        } else if (Text$ends_with(*lib, Text(".a"), NULL)) {
            List$insert(&archives, lib, I(0), sizeof(Text_t));
            List$remove_at(&extra_ldlibs, I(i + 1), I(1), sizeof(Text_t));
        } else {
            i += 1;
        }
    }

    Text_t vendor_dir = Texts(lib_root, "/lib/tomo@", TOMO_VERSION, "/vendor");
    Text_t vendor_archives = Texts(" ", vendor_dir, "/libgc.a ", vendor_dir, "/libgmp.a ", vendor_dir,
                                   "/libunistring.a ", vendor_dir, "/libbacktrace.a");
    if (zig_libc_dir.length > 0) {
        static const char *runtime_archives[] = {"libc.a", "libcompiler_rt.a", "libzigc.a", "libunwind.a"};
        for (size_t i = 0; i < sizeof(runtime_archives) / sizeof(runtime_archives[0]); i++) {
            const char *archive = String(zig_libc_dir, "/", runtime_archives[i]);
            if (Path$is_file(Path$from_str(archive), true))
                vendor_archives = Texts(vendor_archives, Text(" '"), Text$from_str(archive), Text("'"));
        }
    }

    FILE *runner = run_cmd(cc, " ", cflags, " -O", optimization, " ", ldflags, link_optimizations, " ", ldlibs, " ",
                           list_text(extra_ldlibs), " ", paths_str(object_files), " ", test_o, " ", list_text(archives),
                           link_macho ? "" : " -Wl,--no-whole-archive", " ", lib_root, "/lib/tomo@", TOMO_VERSION,
                           "/libtomo.a", vendor_archives, " -o ", exe_path);
    int status = pclose(runner);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) exit(EXIT_FAILURE);

    LOG(LOG_BUILD, "Compiled test runner:\t", Path$relative_to(exe_path, Path$current_dir()));
    return exe_path;
}
