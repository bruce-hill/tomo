#include "modules.h"
#include "stdlib/memory.h"
#include "stdlib/paths.h"
#include "stdlib/simpleparse.h"
#include "stdlib/pointers.h"
#include "stdlib/tables.h"
#include "stdlib/text.h"
#include "stdlib/types.h"

#define xsystem(...) ({ int _status = system(String(__VA_ARGS__)); if (!WIFEXITED(_status) || WEXITSTATUS(_status) != 0) errx(1, "Failed to run command: %s", String(__VA_ARGS__)); })

static void read_modules_ini(Path_t ini_file, module_info_t *info)
{
    OptionalClosure_t by_line = Path$by_line(ini_file);
    if (by_line.fn == NULL) return;
    OptionalText_t (*next_line)(void*) = by_line.fn;
  find_section:;
    for (Text_t line; (line=next_line(by_line.userdata)).length >= 0; ) {
        char *line_str = Text$as_c_string(line);
        if (line_str[0] == '[' && strncmp(line_str+1, info->name, strlen(info->name)) == 0
            && line_str[1+strlen(info->name)] == ']')
            break;
    }
    for (Text_t line; (line=next_line(by_line.userdata)).length >= 0; ) {
        char *line_str = Text$as_c_string(line);
        if (line_str[0] == '[') goto find_section;
        if (!strparse(line_str, "version=", &info->version)
            || !strparse(line_str, "url=", &info->url)
            || !strparse(line_str, "git=", &info->git)
            || !strparse(line_str, "path=", &info->path)
            || !strparse(line_str, "revision=", &info->revision))
            continue;
    }
}

module_info_t get_module_info(ast_t *use)
{
    static Table_t cache = {};
    TypeInfo_t *cache_type = Table$info(Pointer$info("@", &Memory$info), Pointer$info("@", &Memory$info));
    module_info_t **cached = Table$get(cache, &use, cache_type);
    if (cached) return **cached;
    const char *name = Match(use, Use)->path;
    module_info_t *info = new(module_info_t, .name=name);
    if (streq(name, "commands")) info->version = "v1.0";
    else if (streq(name, "random")) info->version = "v1.0";
    else if (streq(name, "base64")) info->version = "v1.0";
    else if (streq(name, "core")) info->version = "v1.0";
    else if (streq(name, "patterns")) info->version = "v1.0";
    else if (streq(name, "pthreads")) info->version = "v1.0";
    else if (streq(name, "shell")) info->version = "v1.0";
    else if (streq(name, "time")) info->version = "v1.0";
    else if (streq(name, "uuid")) info->version = "v1.0";
    else {
        read_modules_ini(Path$sibling(Path$from_str(use->file->filename), Text("modules.ini")), info);
        read_modules_ini(Path$with_extension(Path$from_str(use->file->filename), Text(":modules.ini"), false), info);
    }
    Table$set(&cache, &use, &info, cache_type);
    return *info;

}

bool try_install_module(module_info_t mod)
{
    if (mod.git) {
        OptionalText_t answer = ask(
            Texts(Text("The module \""), Text$from_str(mod.name), Text("\" is not installed.\nDo you want to install it from git URL "),
                  Text$from_str(mod.git), Text("? [Y/n] ")),
            true, true);
        if (!(answer.length == 0 || Text$equal_values(answer, Text("Y")) || Text$equal_values(answer, Text("y"))))
            return false;
        print("Installing ", mod.name, " from git...");
        Path_t tmpdir = Path$unique_directory(Path("/tmp/tomo-module-XXXXXX"));
        if (mod.revision) xsystem("git clone --depth=1 --revision ", mod.revision, " ", mod.git, " ", tmpdir);
        else xsystem("git clone --depth=1 ", mod.git, " ", tmpdir);
        if (mod.path) xsystem("tomo -IL ", tmpdir, "/", mod.path);
        else xsystem("tomo -IL ", tmpdir);
        Path$remove(tmpdir, true);
        return true;
    } else if (mod.url) {
        OptionalText_t answer = ask(
            Texts(Text("The module "), Text$from_str(mod.name), Text(" is not installed.\nDo you want to install it from URL "),
                  Text$from_str(mod.url), Text("? [Y/n] ")),
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
        xsystem("curl ", mod.url, " -o ", tmpdir);
        if (streq(extension, ".zip"))
            xsystem("unzip ", tmpdir, "/", filename);
        else if (streq(extension, ".tar.gz") || streq(extension, ".tar"))
            xsystem("tar xf ", tmpdir, "/", filename);
        else
            return false;
        const char *basename = String(string_slice(filename, strcspn(filename, ".")));
        if (mod.path) xsystem("tomo -IL ", tmpdir, "/", basename, "/", mod.path);
        else xsystem("tomo -IL ", tmpdir, "/", basename);
        Path$remove(tmpdir, true);
        return true;
    } else if (mod.path) {
        OptionalText_t answer = ask(
            Texts(Text("The module "), Text$from_str(mod.name), Text(" is not installed.\nDo you want to install it from path "),
                  Text$from_str(mod.path), Text("? [Y/n] ")),
            true, true);
        if (!(answer.length == 0 || Text$equal_values(answer, Text("Y")) || Text$equal_values(answer, Text("y"))))
            return false;

        print("Installing ", mod.name, " from path...");
        xsystem("tomo -IL ", mod.path);
        return true;
    }

    return false;
}


// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
