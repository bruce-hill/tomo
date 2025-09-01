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

#define xsystem(...)                                                                                                   \
    ({                                                                                                                 \
        int _status = system(String(__VA_ARGS__));                                                                     \
        if (!WIFEXITED(_status) || WEXITSTATUS(_status) != 0)                                                          \
            errx(1, "Failed to run command: %s", String(__VA_ARGS__));                                                 \
    })

static void read_modules_ini(Path_t ini_file, module_info_t *info) {
    OptionalClosure_t by_line = Path$by_line(ini_file);
    if (by_line.fn == NULL) return;
    OptionalText_t (*next_line)(void *) = by_line.fn;
find_section:;
    for (Text_t line; (line = next_line(by_line.userdata)).length >= 0;) {
        char *line_str = Text$as_c_string(line);
        if (line_str[0] == '[' && strncmp(line_str + 1, info->name, strlen(info->name)) == 0
            && line_str[1 + strlen(info->name)] == ']')
            break;
    }
    for (Text_t line; (line = next_line(by_line.userdata)).length >= 0;) {
        char *line_str = Text$as_c_string(line);
        if (line_str[0] == '[') goto find_section;
        if (!strparse(line_str, "version=", &info->version) || !strparse(line_str, "url=", &info->url)
            || !strparse(line_str, "git=", &info->git) || !strparse(line_str, "path=", &info->path)
            || !strparse(line_str, "revision=", &info->revision))
            continue;
    }
}

module_info_t get_module_info(ast_t *use) {
    static Table_t cache = {};
    TypeInfo_t *cache_type = Table$info(Pointer$info("@", &Memory$info), Pointer$info("@", &Memory$info));
    module_info_t **cached = Table$get(cache, &use, cache_type);
    if (cached) return **cached;
    const char *name = Match(use, Use)->path;
    module_info_t *info = new (module_info_t, .name = name);
    read_modules_ini(Path$sibling(Path$from_str(use->file->filename), Text("modules.ini")), info);
    read_modules_ini(Path$with_extension(Path$from_str(use->file->filename), Text(":modules.ini"), false), info);
    Table$set(&cache, &use, &info, cache_type);
    return *info;
}

bool try_install_module(module_info_t mod) {
    Path_t dest = Path$from_text(
        Texts(TOMO_PREFIX "/lib/tomo_" TOMO_VERSION "/", Text$from_str(mod.name), "_", Text$from_str(mod.version)));
    if (mod.git) {
        OptionalText_t answer = ask(Texts("The module \"", Text$from_str(mod.name), "\" ", Text$from_str(mod.version),
                                          " is not installed.\nDo you want to install it from git URL ",
                                          Text$from_str(mod.git), "? [Y/n] "),
                                    true, true);
        if (!(answer.length == 0 || Text$equal_values(answer, Text("Y")) || Text$equal_values(answer, Text("y"))))
            return false;
        print("Installing ", mod.name, " from git...");
        if (mod.revision) xsystem("git clone --depth=1 --revision ", mod.revision, " ", mod.git, " ", dest);
        else xsystem("git clone --depth=1 ", mod.git, " ", dest);
        xsystem("tomo -L ", dest);
        return true;
    } else if (mod.url) {
        OptionalText_t answer =
            ask(Texts("The module \"", Text$from_str(mod.name), "\" ", Text$from_str(mod.version),
                      " is not installed.\nDo you want to install it from URL ", Text$from_str(mod.url), "? [Y/n] "),
                true, true);
        if (!(answer.length == 0 || Text$equal_values(answer, Text("Y")) || Text$equal_values(answer, Text("y"))))
            return false;

        print("Installing ", mod.name, " from URL...");

        const char *p = strrchr(mod.url, '/');
        if (!p) return false;
        const char *filename = p + 1;
        p = strchr(filename, '.');
        if (!p) return false;
        const char *extension = p + 1;
        Path_t tmpdir = Path$unique_directory(Path("/tmp/tomo-module-XXXXXX"));
        tmpdir = Path$child(tmpdir, Text$from_str(mod.name));
        Path$create_directory(tmpdir, 0755);

        xsystem("curl ", mod.url, " -o ", tmpdir);
        Path$create_directory(dest, 0755);
        if (streq(extension, ".zip")) xsystem("unzip ", tmpdir, "/", filename, " -d ", dest);
        else if (streq(extension, ".tar.gz") || streq(extension, ".tar"))
            xsystem("tar xf ", tmpdir, "/", filename, " -C ", dest);
        else return false;
        xsystem("tomo -L ", dest);
        Path$remove(tmpdir, true);
        return true;
    } else if (mod.path) {
        OptionalText_t answer =
            ask(Texts("The module \"", Text$from_str(mod.name), "\" ", Text$from_str(mod.version),
                      " is not installed.\nDo you want to install it from path ", Text$from_str(mod.path), "? [Y/n] "),
                true, true);
        if (!(answer.length == 0 || Text$equal_values(answer, Text("Y")) || Text$equal_values(answer, Text("y"))))
            return false;

        print("Installing ", mod.name, " from path...");
        xsystem("ln -s ", mod.path, " ", dest);
        xsystem("tomo -L ", dest);
        return true;
    }

    return false;
}
