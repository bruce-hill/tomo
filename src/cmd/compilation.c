// The compilation engine driving transpiling, object compilation, executable
// linking, and package builds

#include <ctype.h>
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
#include "../config.h"
#include "../naming.h"
#include "../packages.h"
#include "../parse/files.h"
#include "../stdlib/bytes.h"
#include "../stdlib/datatypes.h"
#include "../stdlib/enums.h"
#include "../stdlib/lists.h"
#include "../stdlib/paths.h"
#include "../stdlib/print.h"
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

// The build-info blob lives in a named section (see compile_build_info()
// below), but rather than maintaining ELF/Mach-O/archive parsers just to find
// that section, the blob brackets itself with sentinel strings and this scans
// the raw bytes for them -- which works uniformly on executables for any
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
    static const char *start_header = "===== Begin Tomo Build Info =====";
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
// `prefix`, skipping hidden files (like .build/), compiled artifacts, and
// symlinks (package binding links -- each linked package is embedded once,
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
           && Text$equal_values(Path$base_name(Path$parent(Path$parent(dir))), Text(".build"));
}

// Collect the binding names and (transitively) the store-entry digests that
// the .tm files in `dir` still depend on -- parse-only, so no installs or
// confirmation prompts can trigger during garbage collection:
static void collect_needed_packages(Path_t dir, Path_t store_root, Table_t *digests, Table_t *names) {
    List_t files = Path$glob(Path$child(dir, Text("*.tm")));
    for (int64_t i = 0; i < (int64_t)files.length; i++) {
        Path_t file = *(Path_t *)(files.data + i * files.stride);
        ast_t *ast = parse_file(Path$as_c_string(file), NULL);
        if (!ast) continue;
        for (ast_list_t *stmt = Match(ast, Block)->statements; stmt; stmt = stmt->next) {
            if (stmt->ast->tag != Use) continue;
            DeclareMatch(use, stmt->ast, Use);
            if (use->what != USE_PACKAGE) continue;
            if (names) Table$str_set(names, use->path, "");
            const char *digest = find_pinned_digest(file, use->path);
            if (digest == NULL || Table$str_get(*digests, digest)) continue;
            Table$str_set(digests, digest, "");
            Path_t entry = Path$child(store_root, Text$from_str(digest));
            if (Path$is_directory(entry, true)) collect_needed_packages(entry, store_root, digests, NULL);
        }
    }
}

// Garbage-collect a source directory's .build/packages binding links and
// .build/store entries: anything no .tm file in the directory (transitively)
// uses anymore is removed. packages.ini pins and vendored sources are never
// touched, so a garbage-collected package reinstalls without any network
// access if its `use` comes back.
static void gc_package_dir(Path_t dir) {
    // Store entries are content-addressed and must never be modified (and
    // their dependencies live in the containing store, not their own):
    if (is_store_entry_dir(dir)) return;
    Path_t store_root = Path$child(Path$child(dir, Text(".build")), Text("store"));
    Table_t digests = EMPTY_TABLE, names = EMPTY_TABLE;
    collect_needed_packages(dir, store_root, &digests, &names);
    mark_unused_packages(Path$child(dir, Text("packages.ini")), names);

    List_t links = Path$glob(Path$child(dir, Text(".build/packages/*")));
    for (int64_t i = 0; i < (int64_t)links.length; i++) {
        Path_t link = *(Path_t *)(links.data + i * links.stride);
        if (Table$str_get(names, Text$as_c_string(Path$base_name(link)))) continue;
        if (unlink(link) == 0 && !quiet)
            print("Removed unused package binding: ", Path$relative_to(link, Path$current_dir()));
    }
    List_t entries = Path$glob(Path$child(dir, Text(".build/store/*")));
    for (int64_t i = 0; i < (int64_t)entries.length; i++) {
        Path_t entry = *(Path_t *)(entries.data + i * entries.stride);
        if (Table$str_get(digests, Text$as_c_string(Path$base_name(entry)))) continue;
        Result_t removed = Path$remove(entry, true);
        if (removed.Failure.reason.tag == TEXT_NONE && !quiet)
            print("Removed unused store entry: ", Path$relative_to(entry, Path$current_dir()));
    }
}

// Where a linked package's sources go in the embedded source zip: store
// entries go under store/<digest> (extracted into the pre-seeded
// .build/store/), while directory-source packages inside the project (e.g.
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
// packages -- the packages.ini pins may cover transitive dependencies too):
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
        // directory-source packages' own binding links live in their .build
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
        if (tab1 && tab2) {
            *tab1 = *tab2 = '\0';
            const char *consumer = line, *name = tab1 + 1, *dep = tab2 + 1;
            // The dep is a zip prefix: "store/<digest>" (extracted into
            // .build/store/) or a project-relative directory like "vendor/x":
            if (*name && *dep && !strchr(consumer, '/') && !strchr(name, '/') && dep[0] != '/' && !streq(consumer, "..")
                && !streq(name, "..") && !strstr(dep, "..")) {
                Path_t store = Path$child(Path$child(outdir, Text(".build")), Text("store"));
                Path_t dep_dir = strncmp(dep, "store/", strlen("store/")) == 0
                                     ? Path$child(Path$child(outdir, Text(".build")), Text$from_str(dep))
                                     : Path$child(outdir, Text$from_str(dep));
                Path_t link_dir = *consumer ? Path$child(Path$child(store, Text$from_str(consumer)), Text("packages"))
                                            : Path$child(Path$child(outdir, Text(".build")), Text("packages"));
                if (Path$is_directory(dep_dir, true)
                    && (!*consumer || Path$is_directory(Path$parent(link_dir), true))) {
                    Result_t result = Path$create_directory(link_dir, 0755, true);
                    if (result.Failure.reason.tag == TEXT_NONE) {
                        Path_t link = Path$child(link_dir, Text$from_str(name));
                        const char *link_target = Path$relative_to(dep_dir, link_dir);
                        unlink(link);
                        if (symlink(link_target, link) == 0)
                            print("Linked    ", Path$relative_to(link, Path$current_dir()), " -> ", link_target);
                    }
                }
            }
        }
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
        // Package sources extract into a project-shaped .build/store/, so the
        // extracted tree rebuilds as-is (offline: its store is pre-seeded):
        const char *out_name = stat.m_filename;
        if (strncmp(out_name, "store/", strlen("store/")) == 0) out_name = String(".build/", out_name);
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
              ".build/store/, so this program can be rebuilt as-is, without fetching the\n"
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

static void add_git_info(env_t *env, Path_t dir) {
    const char *commit = command_output("git -C '", dir, "' rev-parse HEAD 2>/dev/null");
    if (commit) {
        Table$str_set(env->build_info, "Git commit", commit);
        const char *commit_time = command_output("git -C '", dir, "' log -1 --format=%cI 2>/dev/null");
        if (commit_time) Table$str_set(env->build_info, "Git commit time", commit_time);
        const char *dirty = command_output("git -C '", dir, "' diff --quiet  2>/dev/null && echo false || echo true");
        if (dirty) Table$str_set(env->build_info, "Git local changes", dirty);
    }
}

void build_package(Path_t pkg_dir) {
    pkg_dir = Path$resolved(pkg_dir, Path$current_dir());
    if (!Path$is_directory(pkg_dir, true)) print_err("Not a valid directory: ", pkg_dir);

    List_t tm_files = Path$glob(Path$child(pkg_dir, Text("[!._0-9]*.tm")));
    // Cross-compiled package archives go in the per-target .build directory so
    // they don't clobber the native package.a:
    Path_t archive = cross_compiling ? build_file(Path$child(pkg_dir, Text("package.a")), "")
                                     : Path$child(pkg_dir, Text("package.a"));
    build_package_archive(pkg_dir, tm_files, archive);
}

void build_package_archive(Path_t pkg_dir, List_t tm_files, Path_t archive) {
    env_t *env = fresh_scope(global_env(source_mapping));
    List_t object_files = EMPTY_LIST, extra_ldlibs = EMPTY_LIST;

    compile_files(env, tm_files, &object_files, &extra_ldlibs, COMPILE_OBJ);
    if (is_stale_for_any(archive, object_files, false)) {
        add_git_info(env, pkg_dir);

        // Store metadata about the package's build information (in the
        // package's own .build directory, not the working directory's):
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
        if (!quiet) print("Compiled static package:\t", Path$relative_to(archive, Path$current_dir()));
        gc_package_dir(pkg_dir);
    } else {
        if (verbose) whisper("Unchanged: ", archive);
    }
}

void install_package(Path_t pkg_dir) {
    Text_t pkg_name = get_package_name(pkg_dir);
    Path_t dest = Path$child(Path$from_str(String(TOMO_PATH, "/lib/tomo@", TOMO_VERSION)), pkg_name);
    print("Installing ", pkg_dir, " into ", dest);
    if (!Enum$equal(&pkg_dir, &dest, &Path$info)) {
        if (verbose) whisper("Clearing out any pre-existing version of ", pkg_name);
        xsystem(as_owner, "rm -rf '", dest, "'");
        if (verbose) whisper("Moving files to ", dest);
        xsystem(as_owner, "mkdir -p '", dest, "'");
        xsystem(as_owner, "cp -r '", pkg_dir, "'/* '", dest, "/'");
        xsystem(as_owner, "cp -r '", pkg_dir, "'/.build '", dest, "/'");
    }
    print("Installed \033[1m", pkg_dir, "\033[m to ", TOMO_PATH, "/lib/tomo@", TOMO_VERSION, "/", pkg_name);
}

void compile_files(env_t *env, List_t to_compile, List_t *object_files, List_t *extra_ldlibs, compile_mode_t mode) {
    Table_t to_link = EMPTY_TABLE;
    Table_t dependency_files = EMPTY_TABLE;
    for (int64_t i = 0; i < (int64_t)to_compile.length; i++) {

        Path_t filename = *(Path_t *)(to_compile.data + i * to_compile.stride);
        if (!Path$has_extension(filename, Text("tm")))
            print_err("Not a valid .tm file: \x1b[91;1m", filename, "\x1b[m");
        if (!Path$is_file(filename, true)) print_err("Couldn't find file: ", filename);
        build_file_dependency_graph(env->build_info, filename, &dependency_files, &to_link);
    }

    // Make sure all files and dependencies have a .id file:
    for (int64_t i = 0; i < (int64_t)dependency_files.entries.length; i++) {
        struct {
            Path_t filename;
            staleness_t staleness;
        } *entry = (dependency_files.entries.data + i * dependency_files.entries.stride);

        Path_t id_file = build_file(entry->filename, ".id");
        if (!Path$exists(id_file)) {
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
            Path$write(id_file, Texts(filename_id, Text("_"), Text$from_strn(id_str, sizeof(id_str))), 0644);
        }
    }

    // (Re)compile header files, eagerly for explicitly passed in files, lazily
    // for downstream dependencies:
    for (int64_t i = 0; i < (int64_t)dependency_files.entries.length; i++) {
        struct {
            Path_t filename;
            staleness_t staleness;
        } *entry = (dependency_files.entries.data + i * dependency_files.entries.stride);

        if (entry->staleness.h || clean_build) {
            transpile_header(env, entry->filename);
            entry->staleness.o = true;
        } else {
            if (verbose) whisper("Unchanged: ", build_file(entry->filename, ".h"));
        }
    }

    env->imports = new (Table_t);

    struct child_s {
        struct child_s *next;
        pid_t pid;
    } *child_processes = NULL;

    // (Re)transpile and compile object files, eagerly for files explicitly
    // specified and lazily for downstream dependencies:
    for (int64_t i = 0; i < (int64_t)dependency_files.entries.length; i++) {
        struct {
            Path_t filename;
            staleness_t staleness;
        } *entry = (dependency_files.entries.data + i * dependency_files.entries.stride);
        if (!clean_build && !entry->staleness.c && !entry->staleness.h && !entry->staleness.o
            && !is_config_outdated(entry->filename)) {
            if (verbose) whisper("Unchanged: ", build_file(entry->filename, ".c"));
            if (verbose) whisper("Unchanged: ", build_file(entry->filename, ".o"));
            continue;
        }

        pid_t pid = fork();
        if (pid == 0) {
            if (clean_build || entry->staleness.c) transpile_code(env, entry->filename);
            else if (verbose) whisper("Unchanged: ", build_file(entry->filename, ".c"));
            if (mode != COMPILE_C_FILES) compile_object_file(entry->filename);
            fflush(NULL);
            _exit(EXIT_SUCCESS);
        }
        child_processes = new (struct child_s, .next = child_processes, .pid = pid);
    }

    for (; child_processes; child_processes = child_processes->next)
        wait_for_child_success(child_processes->pid);

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
             || is_stale(build_file(path, ".h"), build_file(path, ":packages.ini"), true)
             || is_stale(build_file(path, ".h"), path, false)
             || is_stale(build_file(path, ".h"), build_file(path, ".id"), false),
        .c = is_stale(build_file(path, ".c"), Path$sibling(path, Text("packages.ini")), true)
             || is_stale(build_file(path, ".c"), build_file(path, ":packages.ini"), true)
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
                // their installed sources (into per-target .build directories)
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

    FILE *header = fopen(Path$as_c_string(h_filename), "w");
    if (!header) print_err("Failed to open header file: ", h_filename);
    Text$print(header, h_code);
    if (fclose(header) == -1) print_err("Failed to write header file: ", h_filename);

    if (!quiet) print("Transpiled header:\t", Path$relative_to(h_filename, Path$current_dir()));

}

void transpile_code(env_t *base_env, Path_t path) {
    Path_t c_filename = build_file(path, ".c");
    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    if (!ast) print_err("Could not parse file: ", path);

    env_t *module_env = load_module_env(base_env, ast);

    Text_t c_code = compile_file(module_env, ast);

    FILE *c_file = fopen(Path$as_c_string(c_filename), "w");
    if (!c_file) print_err("Failed to write C file: ", c_filename);

    Text$print(c_file, c_code);

    binding_t *main_binding = get_binding(module_env, "main");
    if (main_binding && main_binding->type->tag == FunctionType) {
        type_t *ret = Match(main_binding->type, FunctionType)->ret;
        if (ret->tag != VoidType && ret->tag != AbortType)
            compiler_err(ast->file, ast->start, ast->end, "The main() function in this file has a return type of ",
                         type_to_text(ret), ", but it should not have any return value!");

        Text$print(c_file, Texts("int parse_and_run$$", main_binding->code, "(int argc, char *argv[]) {\n",
                                 module_env->do_source_mapping ? Text("#line 1\n") : EMPTY_TEXT, "tomo_init();\n",
                                 namespace_name(module_env, module_env->namespace, Text("$initialize")),
                                 "();\n"
                                 "\n",
                                 compile_cli_arg_call(module_env, ast, main_binding->code, main_binding->type),
                                 "return 0;\n"
                                 "}\n"));
    }

    if (fclose(c_file) == -1) print_err("Failed to output C code to ", c_filename);

    if (!quiet) print("Transpiled code:\t", Path$relative_to(c_filename, Path$current_dir()));

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

    FILE *prog = run_cmd(cc, " ", cflags, " -O", optimization, " -c ", c_file, " -o ", obj_file);
    if (!prog) print_err("Failed to run C compiler: ", cc);
    int status = pclose(prog);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) exit(EXIT_FAILURE);

    Path$write(build_file(path, ".config"), config_summary, 0644);

    if (!quiet) print("Compiled object:\t", Path$relative_to(obj_file, Path$current_dir()));
}

Path_t compile_executable(env_t *base_env, Path_t path, Path_t exe_path, List_t object_files, List_t extra_ldlibs) {
    warn_if_first_compile();
    ast_t *ast = parse_file(Path$as_c_string(path), NULL);
    if (!ast) print_err("Could not parse file ", path);
    env_t *env = load_module_env(base_env, ast);
    binding_t *main_binding = get_binding(env, "main");
    if (main_binding && main_binding->type->tag == FunctionType) {
        Path_t manpage_file = build_file(Path$with_extension(path, Text(".1"), true), "");
        if (clean_build || !Path$is_file(manpage_file, true) || is_stale(manpage_file, path, true)) {
            Text_t manpage =
                compile_manpage(Path$base_name(exe_path), ast, Match(main_binding->type, FunctionType)->args);
            Path$write(manpage_file, manpage, 0644);
            if (!quiet) print("Wrote manpage:\t", Path$relative_to(manpage_file, Path$current_dir()));
        } else {
            if (verbose) whisper("Unchanged: ", manpage_file);
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
        && !is_stale(exe_path, Path$sibling(path, Text("packages.ini")), true)
        && !is_stale(exe_path, build_file(path, ":packages.ini"), true)) {
        if (verbose) whisper("Unchanged: ", exe_path);
        return exe_path;
    }

    add_git_info(env, Path$parent(path));

    // Zip up the program's sources for embedding into the executable:
    Path_t source_blob = build_file(path, ".source.zip");
    write_source_blob(env, path, source_blob);

    Text_t program;
    if (main_binding && main_binding->type->tag == FunctionType) {
        program =
            Texts("extern int parse_and_run$$", main_binding->code,
                  "(int argc, char *argv[]);\n"
                  "__attribute__ ((noinline))\n"
                  "int main(int argc, char *argv[]) {\n"
                  "\treturn parse_and_run$$",
                  main_binding->code,
                  "(argc, argv);\n"
                  "}\n",
                  compile_build_info(env, "build_info"), link_macho ? EMPTY_TEXT : compile_source_asm(source_blob));
    } else {
        program =
            Texts("extern void ", namespace_name(env, env->namespace, Text("$initialize")),
                  "(void);\n"
                  "extern void tomo_init(void);\n"
                  "__attribute__ ((noinline))\n"
                  "int main(int argc, char *argv[]) {\n"
                  "tomo_init();\n",
                  namespace_name(env, env->namespace, Text("$initialize")),
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
    // become a -l flag -- no system copies exist (the toolchain uses its own
    // static musl builds):
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

    // On Mach-O the source blob is embedded by the linker rather than asm:
    Text_t source_section_flag =
        link_macho ? Texts(" '-Wl,-sectcreate,__TEXT,__tomo_source,", Text$from_str(Path$as_c_string(source_blob)), "'")
                   : EMPTY_TEXT;

    FILE *runner = run_cmd( // Invoke C compiler
        cc,
        // C flags:
        " ", cflags, " -O", optimization,
        // Linker flags and dynamically linked shared packages:
        " ", ldflags, source_section_flag, " ", ldlibs, " ", list_text(extra_ldlibs),
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
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) exit(EXIT_FAILURE);

    if (!quiet) print("Compiled executable:\t", Path$relative_to(exe_path, Path$current_dir()));
    gc_package_dir(Path$parent(path));
    return exe_path;
}
