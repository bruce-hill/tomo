// A lang for filesystem paths
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistr.h>

#include "array.h"
#include "files.h"
#include "functions.h"
#include "integers.h"
#include "path.h"
#include "text.h"
#include "types.h"
#include "util.h"

PUREFUNC public Path_t Path$escape_text(Text_t text)
{
    if (Text$has(text, Pattern("/")) || Text$has(text, Pattern(";")))
        fail("Invalid path component: %k", &text);
    else if (Text$matches(text, Pattern(".")) || Text$matches(text, Pattern("..")))
        fail("Invalid path component: %k", &text);
    return (Path_t)text;
}

PUREFUNC public Path_t Path$concat(Path_t a, Path_t b)
{
    Path_t path = Text$concat(a, b);
    while (Text$has(path, Pattern("/../")))
        path = Text$replace(path, Pattern("{!/}/../"), Text(""), Text(""), false);

    while (Text$has(path, Pattern("/./")))
        path = Text$replace(path, Pattern("/./"), Text("/"), Text(""), false);

    return path;
}

public Text_t Path$resolved(Path_t path, Path_t relative_to)
{
    while (Text$has(path, Pattern("/../")))
        path = Text$replace(path, Pattern("{!/}/../"), Text(""), Text(""), false);

    while (Text$has(path, Pattern("/./")))
        path = Text$replace(path, Pattern("/./"), Text("/"), Text(""), false);

    const char *path_str = Text$as_c_string(path);
    const char *relative_to_str = Text$as_c_string(relative_to);
    const char *resolved_path = resolve_path(path_str, relative_to_str, relative_to_str);
    if (resolved_path) {
        return (Path_t)(Text$from_str(resolved_path));
    } else if (path_str[0] == '/') {
        return path;
    } else if (path_str[0] == '~' && path_str[1] == '/') {
        return (Path_t)Text$format("%s%s", getenv("HOME"), path_str + 1);
    } else {
        return Paths(Path$resolved(relative_to, Path(".")), Path("/"), path);
    }
}

public Text_t Path$relative(Path_t path, Path_t relative_to)
{
    path = Path$resolved(path, relative_to);
    relative_to = Path$resolved(relative_to, Path("."));
    if (Text$matches(path, Patterns(Pattern("{start}"), relative_to, Pattern("{..}"))))
        return Text$slice(path, I(relative_to.length + 2), I(-1));
    return path;
}

public bool Path$exists(Path_t path)
{
    if (Text$matches(path, Pattern("~/{..}")))
        path = Paths(Text$format("%s", getenv("HOME")), Text$slice(path, I(2), I(-1)));
    struct stat sb;
    return (stat(Text$as_c_string(path), &sb) == 0);
}

public bool Path$is_file(Path_t path, bool follow_symlinks)
{
    if (Text$matches(path, Pattern("~/{..}")))
        path = Paths(Text$format("%s", getenv("HOME")), Text$slice(path, I(2), I(-1)));
    struct stat sb;
    const char *path_str = Text$as_c_string(path);
    int status = follow_symlinks ? stat(path_str, &sb) : lstat(path_str, &sb);
    if (status != 0) return false;
    return (sb.st_mode & S_IFMT) == S_IFREG;
}

public bool Path$is_directory(Path_t path, bool follow_symlinks)
{
    if (Text$matches(path, Pattern("~/{..}")))
        path = Paths(Text$format("%s", getenv("HOME")), Text$slice(path, I(2), I(-1)));
    struct stat sb;
    const char *path_str = Text$as_c_string(path);
    int status = follow_symlinks ? stat(path_str, &sb) : lstat(path_str, &sb);
    if (status != 0) return false;
    return (sb.st_mode & S_IFMT) == S_IFDIR;
}

public bool Path$is_pipe(Path_t path, bool follow_symlinks)
{
    if (Text$matches(path, Pattern("~/{..}")))
        path = Paths(Text$format("%s", getenv("HOME")), Text$slice(path, I(2), I(-1)));
    struct stat sb;
    const char *path_str = Text$as_c_string(path);
    int status = follow_symlinks ? stat(path_str, &sb) : lstat(path_str, &sb);
    if (status != 0) return false;
    return (sb.st_mode & S_IFMT) == S_IFIFO;
}

public bool Path$is_socket(Path_t path, bool follow_symlinks)
{
    if (Text$matches(path, Pattern("~/{..}")))
        path = Paths(Text$format("%s", getenv("HOME")), Text$slice(path, I(2), I(-1)));
    struct stat sb;
    const char *path_str = Text$as_c_string(path);
    int status = follow_symlinks ? stat(path_str, &sb) : lstat(path_str, &sb);
    if (status != 0) return false;
    return (sb.st_mode & S_IFMT) == S_IFSOCK;
}

public bool Path$is_symlink(Path_t path)
{
    if (Text$matches(path, Pattern("~/{..}")))
        path = Paths(Text$format("%s", getenv("HOME")), Text$slice(path, I(2), I(-1)));
    struct stat sb;
    const char *path_str = Text$as_c_string(path);
    int status = stat(path_str, &sb);
    if (status != 0) return false;
    return (sb.st_mode & S_IFMT) == S_IFLNK;
}

static void _write(Path_t path, Text_t text, int mode, int permissions)
{
    if (Text$matches(path, Pattern("~/{..}")))
        path = Paths(Text$format("%s", getenv("HOME")), Text$slice(path, I(2), I(-1)));
    const char *path_str = Text$as_c_string(path);
    int fd = open(path_str, mode, permissions);
    if (fd == -1)
        fail("Could not write to file: %s\n%s", path_str, strerror(errno));

    const char *str = Text$as_c_string(text);
    size_t len = strlen(str);
    ssize_t written = write(fd, str, len);
    if (written != (ssize_t)len)
        fail("Could not write to file: %s\n%s", path_str, strerror(errno));
}

public void Path$write(Path_t path, Text_t text, int permissions)
{
    _write(path, text, O_WRONLY | O_CREAT, permissions);
}

public void Path$append(Path_t path, Text_t text, int permissions)
{
    _write(path, text, O_WRONLY | O_APPEND | O_CREAT, permissions);
}

public Text_t Path$read(Path_t path)
{
    if (Text$matches(path, Pattern("~/{..}")))
        path = Paths(Text$format("%s", getenv("HOME")), Text$slice(path, I(2), I(-1)));
    int fd = open(Text$as_c_string(path), O_RDONLY);
    if (fd == -1)
        fail("Could not read file: %k (%s)", &path, strerror(errno));

    struct stat sb;
    if (fstat(fd, &sb) != 0)
        fail("Could not read file: %k (%s)", &path, strerror(errno));

    if ((sb.st_mode & S_IFMT) == S_IFREG) { // Use memory mapping if it's a real file:
        const char *mem = mmap(NULL, (size_t)sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        char *gc_mem = GC_MALLOC_ATOMIC((size_t)sb.st_size+1);
        memcpy(gc_mem, mem, (size_t)sb.st_size);
        gc_mem[sb.st_size] = '\0';
        close(fd);
        return Text$from_strn(gc_mem, (size_t)sb.st_size);
    } else {
        const size_t chunk_size = 256;
        char *buf = GC_MALLOC_ATOMIC(chunk_size);
        Text_t contents = Text("");
        ssize_t just_read;
        do {
            just_read = read(fd, buf, chunk_size);
            if (just_read < 0)
                fail("Failed while reading file: %k (%s)", &path, strerror(errno));
            else if (just_read == 0)
                break;

            if (u8_check((uint8_t*)buf, (size_t)just_read) != NULL)
                fail("File does not contain valid UTF8 data!");
            contents = Texts(contents, Text$from_strn(buf, (size_t)just_read));
            buf = GC_MALLOC_ATOMIC(chunk_size);
        } while (just_read > 0);
        close(fd);
        return contents;
    }
}

public void Path$remove(Path_t path, bool ignore_missing)
{
    if (Text$matches(path, Pattern("~/{..}")))
        path = Paths(Text$format("%s", getenv("HOME")), Text$slice(path, I(2), I(-1)));
    const char *path_str = Text$as_c_string(path);
    struct stat sb;
    if (lstat(path_str, &sb) != 0) {
        if (!ignore_missing)
            fail("Could not remove file: %s (%s)", path_str, strerror(errno));
    }

    if ((sb.st_mode & S_IFMT) == S_IFREG || (sb.st_mode & S_IFMT) == S_IFLNK) {
        if (unlink(path_str) != 0 && !ignore_missing)
            fail("Could not remove file: %s (%s)", path_str, strerror(errno));
    } else if ((sb.st_mode & S_IFMT) == S_IFDIR) {
        if (rmdir(path_str) != 0 && !ignore_missing)
            fail("Could not remove directory: %s (%s)", path_str, strerror(errno));
    } else {
        fail("Could not remove path: %s (not a file or directory)", path_str, strerror(errno));
    }
}

public void Path$create_directory(Path_t path, int permissions)
{
    if (Text$matches(path, Pattern("~/{..}")))
        path = Paths(Text$format("%s", getenv("HOME")), Text$slice(path, I(2), I(-1)));
    if (mkdir(Text$as_c_string(path), (mode_t)permissions) != 0)
        fail("Could not create directory: %k (%s)", &path, strerror(errno));
}

static Array_t _filtered_children(Path_t path, bool include_hidden, mode_t filter)
{
    if (Text$matches(path, Pattern("~/{..}")))
        path = Paths(Text$format("%s", getenv("HOME")), Text$slice(path, I(2), I(-1)));
    struct dirent *dir;
    Array_t children = {};
    const char *path_str = Text$as_c_string(path);
    size_t path_len = strlen(path_str);
    DIR *d = opendir(path_str);
    if (!d)
        fail("Could not open directory: %k (%s)", &path, strerror(errno));

    if (path_str[path_len-1] == '/')
        --path_len;

    while ((dir = readdir(d)) != NULL) {
        if (!include_hidden && dir->d_name[0] == '.')
            continue;
        if (streq(dir->d_name, ".") || streq(dir->d_name, ".."))
            continue;

        const char *child_str = heap_strf("%.*s/%s", path_len, path_str, dir->d_name);
        struct stat sb;
        if (stat(child_str, &sb) != 0)
            continue;
        if (!((sb.st_mode & S_IFMT) & filter))
            continue;

        Path_t child = Text$format("%s%s", child_str, ((sb.st_mode & S_IFMT) == S_IFDIR) ? "/" : ""); // Trailing slash for dirs
        Array$insert(&children, &child, I(0), sizeof(Path_t));
    }
    closedir(d);
    return children;
}

public Array_t Path$children(Path_t path, bool include_hidden)
{
    return _filtered_children(path, include_hidden, (mode_t)-1);
}

public Array_t Path$files(Path_t path, bool include_hidden)
{
    return _filtered_children(path, include_hidden, S_IFREG);
}

public Array_t Path$subdirectories(Path_t path, bool include_hidden)
{
    return _filtered_children(path, include_hidden, S_IFDIR);
}

public Path_t Path$unique_directory(Path_t path)
{
    if (Text$matches(path, Pattern("~/{..}")))
        path = Paths(Text$format("%s", getenv("HOME")), Text$slice(path, I(2), I(-1)));
    const char *path_str = Text$as_c_string(path);
    size_t len = strlen(path_str);
    if (len >= PATH_MAX) fail("Path is too long: %s", path_str);
    char buf[PATH_MAX] = {};
    strcpy(buf, path_str);
    if (buf[len-1] == '/')
        buf[--len] = '\0';
    char *created = mkdtemp(buf);
    if (!created) fail("Failed to create temporary directory: %s (%s)", path_str, strerror(errno));
    return Text$format("%s/", created);
}

public Text_t Path$write_unique(Path_t path, Text_t text)
{
    if (Text$matches(path, Pattern("~/{..}")))
        path = Paths(Text$format("%s", getenv("HOME")), Text$slice(path, I(2), I(-1)));
    const char *path_str = Text$as_c_string(path);
    size_t len = strlen(path_str);
    if (len >= PATH_MAX) fail("Path is too long: %s", path_str);
    char buf[PATH_MAX] = {};
    strcpy(buf, path_str);

    int64_t suffixlen = 0;
    (void)Text$find(path, Pattern("{0+!X}{end}"), I(1), &suffixlen);
    if (suffixlen < 0) suffixlen = 0;

    int fd = mkstemps(buf, suffixlen);
    if (fd == -1)
        fail("Could not write to unique file: %s\n%s", buf, strerror(errno));

    const char *str = Text$as_c_string(text);
    size_t write_len = strlen(str);
    ssize_t written = write(fd, str, write_len);
    if (written != (ssize_t)write_len)
        fail("Could not write to file: %s\n%s", buf, strerror(errno));
    return Text$format("%s", buf);
}

public Path_t Path$parent(Path_t path)
{
    return Text$replace(path, Pattern("{0+..}/{!/}{end}"), Text("@1"), Text("@"), false);
}

public Text_t Path$base_name(Path_t path)
{
    if (Text$matches(path, Pattern("/{end}")))
        return Text$replace(path, Pattern("{0+..}/{!/}/{end}"), Text("@2"), Text("@"), false);
    else
        return Text$replace(path, Pattern("{0+..}/{!/}{end}"), Text("@2"), Text("@"), false);
}

public Text_t Path$extension(Path_t path, bool full)
{
    Text_t base = Path$base_name(path);
    if (Text$matches(base, Pattern(".{!.}.{..}")))
        return Text$replace(base, full ? Pattern(".{!.}.{..}") : Pattern(".{..}.{!.}{end}"), Text("@2"), Text("@"), false);
    else if (Text$matches(base, Pattern("{!.}.{..}")))
        return Text$replace(base, full ? Pattern("{!.}.{..}") : Pattern("{..}.{!.}{end}"), Text("@2"), Text("@"), false);
    else
        return Text("");
}

public const TypeInfo Path$info = {
    .size=sizeof(Path_t),
    .align=__alignof__(Path_t),
    .tag=TextInfo,
    .TextInfo={.lang="Path"},
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
