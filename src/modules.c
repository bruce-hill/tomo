// This file defines some code for getting info about modules and installing them.

#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "config.h"
#include "modules.h"
#include "stdlib/memory.h"
#include "stdlib/paths.h"
#include "stdlib/pointers.h"
#include "stdlib/print.h"
#include "stdlib/simpleparse.h"
#include "stdlib/tables.h"
#include "stdlib/text.h"
#include "stdlib/types.h"
#include "stdlib/util.h"

#define xsystem(...)                                                                                                   \
    ({                                                                                                                 \
        int _status = system(String(__VA_ARGS__));                                                                     \
        if (!WIFEXITED(_status) || WEXITSTATUS(_status) != 0)                                                          \
            errx(1, "Failed to run command: %s", String(__VA_ARGS__));                                                 \
    })

const char *get_library_version(Path_t lib_dir) {
    Path_t changes_file = Path$child(lib_dir, Text("CHANGES.md"));
    OptionalText_t changes = Path$read(changes_file);
    if (changes.length <= 0) {
        return "v0.0";
    }
    const char *changes_str = Text$as_c_string(Texts(Text("\n"), changes));
    const char *version_line = strstr(changes_str, "\n## ");
    if (version_line == NULL)
        print_err("CHANGES.md in ", lib_dir, " does not have any valid versions starting with '## '");
    return String(string_slice(version_line + 4, strcspn(version_line + 4, "\r\n")));
}

Text_t get_library_name(Path_t lib_dir) {
    Text_t name = Path$base_name(lib_dir);
    name = Text$without_prefix(name, Text("tomo-"));
    name = Text$without_suffix(name, Text("-tomo"));
    Text_t suffix = Texts(Text("_"), Text$from_str(get_library_version(lib_dir)));
    if (!Text$ends_with(name, suffix, NULL)) name = Texts(name, suffix);
    return name;
}

bool install_from_modules_ini(Path_t ini_file, bool ask_confirmation) {
    OptionalClosure_t by_line = Path$by_line(ini_file);
    if (by_line.fn == NULL) return false;
    OptionalText_t (*next_line)(void *) = by_line.fn;
    module_info_t info = {};
    for (OptionalText_t line; (line = next_line(by_line.userdata)).tag != TEXT_NONE;) {
        char *line_str = Text$as_c_string(line);
        const char *next_section = NULL;
        if (!strparse(line_str, "[", &next_section, "]")) {
            if (info.name) {
                if (!try_install_module(info, ask_confirmation)) return false;
            }
            print("Checking module ", next_section, "...");
            info = (module_info_t){.name = next_section};
            continue;
        }
        if (!strparse(line_str, "version=", &info.version) || !strparse(line_str, "url=", &info.url)
            || !strparse(line_str, "git=", &info.git) || !strparse(line_str, "path=", &info.path)
            || !strparse(line_str, "revision=", &info.revision))
            continue;
    }
    if (info.name) {
        if (!try_install_module(info, ask_confirmation)) return false;
    }
    return true;
}

static void read_modules_ini(Path_t ini_file, module_info_t *info) {
    OptionalClosure_t by_line = Path$by_line(ini_file);
    if (by_line.fn == NULL) return;
    OptionalText_t (*next_line)(void *) = by_line.fn;
find_section:;
    for (OptionalText_t line; (line = next_line(by_line.userdata)).tag != TEXT_NONE;) {
        char *line_str = Text$as_c_string(line);
        if (line_str[0] == '[' && strncmp(line_str + 1, info->name, strlen(info->name)) == 0
            && line_str[1 + strlen(info->name)] == ']')
            break;
    }
    for (OptionalText_t line; (line = next_line(by_line.userdata)).tag != TEXT_NONE;) {
        char *line_str = Text$as_c_string(line);
        if (line_str[0] == '[') goto find_section;
        if (!strparse(line_str, "version=", &info->version) || !strparse(line_str, "url=", &info->url)
            || !strparse(line_str, "git=", &info->git) || !strparse(line_str, "path=", &info->path)
            || !strparse(line_str, "revision=", &info->revision))
            continue;
    }
}

module_info_t get_used_module_info(ast_t *use) {
    static Table_t cache = EMPTY_TABLE;
    TypeInfo_t *cache_type = Table$info(Pointer$info("@", &Memory$info), Pointer$info("@", &Memory$info));
    module_info_t **cached = Table$get(cache, &use, cache_type);
    if (cached) return **cached;
    const char *name = Match(use, Use)->path;
    module_info_t *info = new (module_info_t, .name = name);
    Path_t tomo_default_modules =
        Path$from_text(Texts(Text$from_str(TOMO_PATH), "/lib/tomo_" TOMO_VERSION "/modules.ini"));
    read_modules_ini(tomo_default_modules, info);
    read_modules_ini(Path$sibling(Path$from_str(use->file->filename), Text("modules.ini")), info);
    read_modules_ini(Path$with_extension(Path$from_str(use->file->filename), Text(":modules.ini"), false), info);
    Table$set(&cache, &use, &info, cache_type);
    return *info;
}

bool try_install_module(module_info_t mod, bool ask_confirmation) {
    Path_t dest = Path$from_text(Texts(Text$from_str(TOMO_PATH), "/lib/tomo_" TOMO_VERSION "/", Text$from_str(mod.name),
                                       "_", Text$from_str(mod.version)));
    if (Path$exists(dest)) return true;

    print("No such path: ", dest);

    if (mod.git) {
        if (ask_confirmation) {
            OptionalText_t answer =
                ask(Texts("The module \"", Text$from_str(mod.name), "\" ", Text$from_str(mod.version),
                          " is not installed.\nDo you want to install it from git URL ", Text$from_str(mod.git),
                          "? [Y/n] "),
                    true, true);
            if (!(answer.length == 0 || Text$equal_values(answer, Text("Y")) || Text$equal_values(answer, Text("y"))))
                return false;
        }
        print("Installing ", mod.name, " from git...");
        if (mod.revision) xsystem("git clone --depth=1 --revision ", mod.revision, " ", mod.git, " ", dest);
        else if (mod.version) xsystem("git clone --depth=1 --branch ", mod.version, " ", mod.git, " ", dest);
        else xsystem("git clone --depth=1 ", mod.git, " ", dest);
        xsystem("tomo -L ", dest);
        return true;
    } else if (mod.url) {
        if (ask_confirmation) {
            OptionalText_t answer = ask(
                Texts("The module \"", Text$from_str(mod.name), "\" ", Text$from_str(mod.version),
                      " is not installed.\nDo you want to install it from URL ", Text$from_str(mod.url), "? [Y/n] "),
                true, true);
            if (!(answer.length == 0 || Text$equal_values(answer, Text("Y")) || Text$equal_values(answer, Text("y"))))
                return false;
        }

        print("Installing ", mod.name, " from URL...");

        const char *p = strrchr(mod.url, '/');
        if (!p) return false;
        const char *filename = p + 1;
        p = strchr(filename, '.');
        if (!p) return false;
        const char *extension = p + 1;
        Path_t tmpdir = Path$unique_directory(Path("/tmp/tomo-module-XXXXXX"));
        tmpdir = Path$child(tmpdir, Text$from_str(mod.name));
        Path$create_directory(tmpdir, 0755, true);

        xsystem("curl ", mod.url, " -o ", tmpdir);
        Path$create_directory(dest, 0755, true);
        if (streq(extension, ".zip")) xsystem("unzip ", tmpdir, "/", filename, " -d ", dest);
        else if (streq(extension, ".tar.gz") || streq(extension, ".tar"))
            xsystem("tar xf ", tmpdir, "/", filename, " -C ", dest);
        else return false;
        xsystem("tomo -L ", dest);
        Path$remove(tmpdir, true);
        return true;
    } else if (mod.path) {
        if (ask_confirmation) {
            OptionalText_t answer = ask(
                Texts("The module \"", Text$from_str(mod.name), "\" ", Text$from_str(mod.version),
                      " is not installed.\nDo you want to install it from path ", Text$from_str(mod.path), "? [Y/n] "),
                true, true);
            if (!(answer.length == 0 || Text$equal_values(answer, Text("Y")) || Text$equal_values(answer, Text("y"))))
                return false;
        }

        print("Installing ", mod.name, " from path...");
        xsystem("ln -s ", mod.path, " ", dest);
        xsystem("tomo -L ", dest);
        return true;
    }

    return false;
}
