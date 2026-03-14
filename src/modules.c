// This file defines some code for getting info about modules and installing them.

#include <err.h>
#include <openssl/evp.h>
#include <stdlib.h>

#include "config.h"
#include "modules.h"
#include "stdlib/optionals.h"
#include "stdlib/paths.h"
#include "stdlib/print.h"
#include "stdlib/simpleparse.h"
#include "stdlib/tables.h"
#include "stdlib/text.h"

#define xsystem(...)                                                                                                   \
    ({                                                                                                                 \
        const char *cmd = String(__VA_ARGS__);                                                                         \
        int _status = system(cmd);                                                                                     \
        if (!WIFEXITED(_status) || WEXITSTATUS(_status) != 0) {                                                        \
            errx(1, "Failed to run command: %s", String(__VA_ARGS__));                                                 \
        }                                                                                                              \
    })

#define xsystem_cleanup(tmpdir, ...)                                                                                   \
    ({                                                                                                                 \
        const char *cmd = String(__VA_ARGS__);                                                                         \
        int _status = system(cmd);                                                                                     \
        if (!WIFEXITED(_status) || WEXITSTATUS(_status) != 0) {                                                        \
            Path$remove(tmpdir, true);                                                                                 \
            errx(1, "Failed to run command: %s", String(__VA_ARGS__));                                                 \
        }                                                                                                              \
    })

static OptionalText_t file_digest(Path_t path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NONE_TEXT;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);

    unsigned char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        EVP_DigestUpdate(ctx, buf, n);
    fclose(f);

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int len;
    EVP_DigestFinal_ex(ctx, hash, &len);
    EVP_MD_CTX_free(ctx);

    const char *prefix = "sha256:";
    char *ret = GC_MALLOC_ATOMIC(strlen(prefix) + 2 * len + 1);
    char *p = ret;
    p = stpcpy(p, prefix);
    for (size_t i = 0; i < len; i++) {
        p += sprintf(p, "%02x", hash[i]);
    }
    *p = '\0';
    return Text$from_str(ret);
}

Text_t get_library_name(Path_t lib_dir) {
    Text_t name = Path$base_name(lib_dir);
    name = Text$without_prefix(name, Text("tomo-"));
    name = Text$without_suffix(name, Text("-tomo"));
    return name;
}

static Text_t module_text(module_info_t mod) {
    Text_t text = Texts("[", mod.name, "]\n");
    for (int64_t i = 0; i < (int64_t)mod.info.entries.length; i++) {
        struct {
            const char *key, *value;
        } *entry = mod.info.entries.data + i * mod.info.entries.stride;
        text = Texts(text, entry->key, "=", entry->value, "\n");
    }
    return text;
}

static OptionalPath_t try_install_module_from_source(Path_t ini_file, module_info_t *mod, const char *source,
                                                     bool ask_confirmation) {
    if (source[0] == '.' || source[0] == '/' || source[0] == '~') {
        Path_t source_path = Path$from_str(source);
        if (!Path$exists(source_path)) {
            print("No such file: ", source_path);
            return NONE_PATH;
        }
        if (Path$is_directory(source_path, true)) {
            xsystem("tomo -L ", source_path);
            return source_path;
        } else {
            source = String("file://", Path$resolved(source, Path$parent(ini_file)));
        }
    }

    if (ask_confirmation) {
        OptionalText_t answer = ask(Texts("The module ", Text$quoted(Text$from_str(mod->name), false, Text("\"")),
                                          " is not installed.\nDo you want to install it from ",
                                          Text$quoted(Text$from_str(source), false, Text("\"")), "? [Y/n] "),
                                    true, true);
        if (!(answer.length == 0 || Text$equal_values(answer, Text("Y")) || Text$equal_values(answer, Text("y")))) {
            print("Okay, not installing it!");
            exit(1);
        }
    }

    print("Installing ", Text$quoted(Text$from_str(mod->name), false, Text("\"")), " from URL...");

    Path_t tmpdir = Path$unique_directory(Path$from_text(Texts("/tmp/tomo-", mod->name, "-XXXXXX")));

    xsystem_cleanup(tmpdir, "curl --output-dir ", quoted(tmpdir), " -LJO ", quoted(source));

    List_t children = Path$children(tmpdir, true);
    if (children.length != 1) {
        Path$remove(tmpdir, true);
        print("Failed to download file ", mod->name, " from: ", source);
        return NONE_PATH;
    }

    Path_t downloaded = *(Path_t *)children.data;
    OptionalText_t downloaded_digest = file_digest(downloaded);
    if (downloaded_digest.tag == TEXT_NONE) {
        Path$remove(tmpdir, true);
        fail("Failed to compute digest for module ", mod->name);
    }

    const char *digest = Table$str_get(mod->info, "digest");
    if (digest == NULL) {
        digest = Text$as_c_string(downloaded_digest);
        Table$str_set(&mod->info, "digest", digest);
        print("Added digest for ", mod->name, ": ", digest);
    } else {
        if (!Text$equal_values(downloaded_digest, Text$from_str(digest))) {
            // Digest mismatch
            Path$remove(tmpdir, true);
            fail("Mismatched digest sum for module ", mod->name, "! Expected ", digest, " but got ", downloaded_digest);
        }
    }

    OptionalPath_t install_location = Path$from_text(
        Texts(Text$from_str(TOMO_PATH), "/lib/tomo@", TOMO_VERSION, "/", Text$from_str(mod->name), "/", digest));

    Result_t result = Path$create_directory(install_location, 0755, true);
    if (result.Failure.reason.tag != TEXT_NONE) {
        Path$remove(tmpdir, true);
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
        fail("Unsupported module filetype: ", downloaded);
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
                Path$remove(tmpdir, true);
                fail(result.Failure.reason);
            }
        }
        result = Path$remove(top_level, true);
        if (result.Failure.reason.tag != TEXT_NONE) {
            Path$remove(tmpdir, true);
            fail(result.Failure.reason);
        }
    }

    xsystem_cleanup(tmpdir, "tomo -L ", install_location);

    // Always clean up tmpdir!
    Path$remove(tmpdir, true);

    // Add digest to the module.ini file if it wasn't already there
    if (digest == NULL && downloaded_digest.tag != TEXT_NONE) {
        Table$str_set(&mod->info, "digest", Text$as_c_string(downloaded_digest));
        print("Added digest for ", mod->name, ": ", digest);
    }

    return install_location;
}

static OptionalPath_t try_install_module(Path_t ini_file, module_info_t *mod, bool ask_confirmation) {
    OptionalPath_t install_location = NULL;
    const char *digest = Table$str_get(mod->info, "digest");
    if (digest) {
        install_location = Path$from_text(
            Texts(Text$from_str(TOMO_PATH), "/lib/tomo@", TOMO_VERSION, "/", Text$from_str(mod->name), "/", digest));
        if (Path$exists(install_location)) {
            return install_location;
        }
    }

    const char *source = Table$str_get(mod->info, "source");
    for (int i = 1; source; i++) {
        install_location = try_install_module_from_source(ini_file, mod, source, ask_confirmation);
        if (install_location != NULL) return install_location;
        const char *new_source_key = String("source-", i + 1);
        source = Table$str_get(mod->info, new_source_key);
    }
    fail("No source for module: ", mod->name);
    return NULL;
}

static OptionalPath_t get_module_install_location(Path_t ini_file, const char *name) {
    OptionalClosure_t by_line = Path$by_line(ini_file);
    if (by_line.fn == NULL) return NONE_PATH;
    OptionalText_t (*next_line)(void *) = by_line.fn;

    Text_t reformatted = EMPTY_TEXT;
    for (OptionalText_t line; (line = next_line(by_line.userdata)).tag != TEXT_NONE;) {
        if (Text$equal_values(line, Texts("[", name, "]"))) goto found_module;
        reformatted = Texts(reformatted, line, "\n");
    }
    return NONE_PATH;

found_module:;

    module_info_t mod = {.name = name, .info = EMPTY_TABLE};
    for (OptionalText_t line; (line = next_line(by_line.userdata)).tag != TEXT_NONE;) {
        const char *line_str = Text$as_c_string(line);
        const char *key = NULL, *value = NULL;
        if (!strparse(line_str, &key, "=", &value)) {
            Table$str_set(&mod.info, key, value);
        } else {
            break;
        }
    }
    bool had_digest = Table$str_get(mod.info, "digest") != NULL;
    OptionalPath_t installed = try_install_module(ini_file, &mod, true);
    if (installed == NULL) return NULL;

    if (!had_digest && Table$str_get(mod.info, "digest") != NULL) {
        reformatted = Texts(reformatted, module_text(mod), "\n\n");
        for (OptionalText_t line; (line = next_line(by_line.userdata)).tag != TEXT_NONE;) {
            reformatted = Texts(reformatted, line, "\n");
        }
        reformatted = Texts(Text$trim(reformatted, Text(" \r\n\t"), true, true), "\n");
        Result_t result = Path$write(ini_file, reformatted, 0644);
        if (result.Failure.reason.tag != TEXT_NONE) {
            fail(result.Failure.reason);
        }
    }

    return installed;
}

OptionalPath_t find_installed_module(ast_t *use) {
    const char *name = Match(use, Use)->path;

    {
        Path_t file_module = Path$with_extension(Path$from_str(use->file->filename), Text(":modules.ini"), false);
        OptionalPath_t installed = get_module_install_location(file_module, name);
        if (installed != NULL) return installed;
    }

    {
        Path_t local_module = Path$sibling(Path$from_str(use->file->filename), Text("modules.ini"));
        OptionalPath_t installed = get_module_install_location(local_module, name);
        if (installed != NULL) return installed;
    }

    {
        Path_t tomo_default_modules =
            Path$from_text(Texts(Text$from_str(TOMO_PATH), "/lib/tomo@", TOMO_VERSION, "/modules.ini"));
        OptionalPath_t installed = get_module_install_location(tomo_default_modules, name);
        if (installed != NULL) return installed;
    }

    return NONE_PATH;
}
