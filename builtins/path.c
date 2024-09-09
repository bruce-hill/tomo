// A lang for filesystem paths
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
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
    struct stat sb;
    return (stat(Text$as_c_string(path), &sb) == 0);
}

public bool Path$is_file(Path_t path, bool follow_symlinks)
{
    struct stat sb;
    const char *path_str = Text$as_c_string(path);
    int status = follow_symlinks ? stat(path_str, &sb) : lstat(path_str, &sb);
    if (status != 0) return false;
    return (sb.st_mode & S_IFMT) == S_IFREG;
}

public bool Path$is_directory(Path_t path, bool follow_symlinks)
{
    struct stat sb;
    const char *path_str = Text$as_c_string(path);
    int status = follow_symlinks ? stat(path_str, &sb) : lstat(path_str, &sb);
    if (status != 0) return false;
    return (sb.st_mode & S_IFMT) == S_IFDIR;
}

public bool Path$is_pipe(Path_t path, bool follow_symlinks)
{
    struct stat sb;
    const char *path_str = Text$as_c_string(path);
    int status = follow_symlinks ? stat(path_str, &sb) : lstat(path_str, &sb);
    if (status != 0) return false;
    return (sb.st_mode & S_IFMT) == S_IFIFO;
}

public bool Path$is_socket(Path_t path, bool follow_symlinks)
{
    struct stat sb;
    const char *path_str = Text$as_c_string(path);
    int status = follow_symlinks ? stat(path_str, &sb) : lstat(path_str, &sb);
    if (status != 0) return false;
    return (sb.st_mode & S_IFMT) == S_IFSOCK;
}

public bool Path$is_symlink(Path_t path)
{
    struct stat sb;
    const char *path_str = Text$as_c_string(path);
    int status = stat(path_str, &sb);
    if (status != 0) return false;
    return (sb.st_mode & S_IFMT) == S_IFLNK;
}

static void _write(Path_t path, Text_t text, int mode, int permissions)
{
    const char *path_str = Text$as_c_string(path);
    int fd = open(path_str, mode, permissions);
    if (fd == -1)
        fail("Could not write to file: %s\n%s", strerror(errno));

    const char *str = Text$as_c_string(text);
    size_t len = strlen(str);
    ssize_t written = write(fd, str, len);
    if (written != (ssize_t)len)
        fail("Could not write to file: %s\n%s", strerror(errno));
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
    int fd = open(Text$as_c_string(path), O_RDONLY);
    if (fd == -1)
        fail("Could not read file: %k (%s)", &path, strerror(errno));

    struct stat sb;
    if (fstat(fd, &sb) != 0)
        fail("Could not read file: %k (%s)", &path, strerror(errno));

    if ((sb.st_mode & S_IFMT) == S_IFREG) { // Use memory mapping if it's a real file:
        printf("USING MMAP\n");
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
    if (mkdir(Text$as_c_string(path), (mode_t)permissions) != 0)
        fail("Could not create directory: %k (%s)", &path, strerror(errno));
}

public const TypeInfo Path$info = {
    .size=sizeof(Path_t),
    .align=__alignof__(Path_t),
    .tag=TextInfo,
    .TextInfo={.lang="Path"},
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
