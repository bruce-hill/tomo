// A lang for filesystem paths
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <gc.h>
#include <glob.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistr.h>

#include "arrays.h"
#include "files.h"
#include "integers.h"
#include "optionals.h"
#include "paths.h"
#include "patterns.h"
#include "text.h"
#include "types.h"
#include "util.h"

PUREFUNC public Path_t Path$escape_text(Text_t text)
{
    if (Text$has(text, Pattern("/")))
        fail("Path interpolations cannot contain slashes: %k", &text);
    else if (Text$has(text, Pattern(";")))
        fail("Path interpolations cannot contain semicolons: %k", &text);
    else if (Text$equal_values(text, Path(".")) || Text$equal_values(text, Path("..")))
        fail("Path interpolation is \"%k\" which is disallowed to prevent security vulnerabilities", &text);
    return (Path_t)text;
}

PUREFUNC public Path_t Path$escape_path(Path_t path)
{
    if (Text$starts_with(path, Path("~/")) || Text$starts_with(path, Path("/")))
        fail("Invalid path component: %k", &path);
    return path;
}

public Path_t Path$cleanup(Path_t path)
{
    if (!Text$starts_with(path, Path("/")) && !Text$starts_with(path, Path("./"))
        && !Text$starts_with(path, Path("../")) && !Text$starts_with(path, Path("~/")))
        path = Text$concat(Text("./"), path);

    // Not fully resolved, but at least get rid of some of the cruft like "/./"
    // and "/foo/../" and "//"
    bool trailing_slash = Text$ends_with(path, Path("/"));
    Array_t components = Text$split(path, Pattern("/"));
    if (components.length == 0) return Path("/");
    Path_t root = *(Path_t*)components.data;
    Array$remove_at(&components, I(1), I(1), sizeof(Path_t));

    for (int64_t i = 0; i < components.length; ) {
        Path_t component = *(Path_t*)(components.data + i*components.stride);
        if (component.length == 0 || Text$equal_values(component, Path("."))) { // Skip (//) and (/./)
            Array$remove_at(&components, I(i+1), I(1), sizeof(Path_t));
        } else if (Text$equal_values(component, Path(".."))) {
            if (i == 0) {
                if (root.length == 0) { // (/..) -> (/)
                    Array$remove_at(&components, I(i+1), I(1), sizeof(Path_t));
                    i += 1;
                } else if (Text$equal_values(root, Path("."))) { // (./..) -> (..)
                    root = Path("..");
                    Array$remove_at(&components, I(i+1), I(1), sizeof(Path_t));
                    i += 1;
                } else if (Text$equal_values(root, Path("~"))) {
                    root = Path(""); // Convert $HOME to absolute path:

                    Array$remove_at(&components, I(i+1), I(1), sizeof(Path_t));
                    // `i` is pointing to where the `..` lived

                    const char *home = getenv("HOME");
                    if (!home) fail("Could not get $HOME directory!");

                    // Insert all but the last component:
                    for (const char *p = home + 1; *p; ) {
                        const char *next_slash = strchr(p, '/');
                        if (!next_slash) break; // Skip last component
                        Path_t home_component = Text$format("%.*s", (int)(next_slash - p), p);
                        Array$insert(&components, &home_component, I(i+1), sizeof(Path_t));
                        i += 1;
                        p = next_slash + 1;
                    }
                } else { // (../..) -> (../..)
                    i += 1;
                }
            } else if (Text$equal(&component, (Path_t*)(components.data + (i-1)*components.stride))) { // (___/../..) -> (____/../..)
                i += 1;
            } else { // (___/foo/..) -> (___)
                Array$remove_at(&components, I(i), I(2), sizeof(Path_t));
                i -= 1;
            }
        } else { // (___/foo/baz) -> (___/foo/baz)
            i++;
        }
    }

    Text_t cleaned_up = Text$concat(root, Text("/"), Text$join(Text("/"), components));
    if (trailing_slash && !Text$ends_with(cleaned_up, Text("/")))
        cleaned_up = Text$concat(cleaned_up, Text("/"));
    return cleaned_up;
}

static INLINE Path_t Path$_expand_home(Path_t path)
{
    if (Text$starts_with(path, Path("~/"))) {
        Path_t after_tilde = Text$slice(path, I(2), I(-1));
        return Text$format("%s%k", getenv("HOME"), &after_tilde);
    } else {
        return path;
    }
}

public Path_t Path$_concat(int n, Path_t items[n])
{
    Path_t cleaned_up = Path$cleanup(Text$_concat(n, items));
    if (cleaned_up.length > PATH_MAX)
        fail("Path exceeds the maximum path length: %k", &cleaned_up);
    return cleaned_up;
}

public Text_t Path$resolved(Path_t path, Path_t relative_to)
{
    path = Path$cleanup(path);

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
        return Text$concat(Path$resolved(relative_to, Path(".")), Path("/"), path);
    }
}

public Text_t Path$relative(Path_t path, Path_t relative_to)
{
    path = Path$resolved(path, relative_to);
    relative_to = Path$resolved(relative_to, Path("."));
    if (Text$starts_with(path, Text$concat(relative_to, Text("/"))))
        return Text$slice(path, I(relative_to.length + 2), I(-1));
    return path;
}

public bool Path$exists(Path_t path)
{
    path = Path$_expand_home(path);
    struct stat sb;
    return (stat(Text$as_c_string(path), &sb) == 0);
}

static INLINE int path_stat(Path_t path, bool follow_symlinks, struct stat *sb)
{
    path = Path$_expand_home(path);
    const char *path_str = Text$as_c_string(path);
    return follow_symlinks ? stat(path_str, sb) : lstat(path_str, sb);
}

public bool Path$is_file(Path_t path, bool follow_symlinks)
{
    struct stat sb;
    int status = path_stat(path, follow_symlinks, &sb);
    if (status != 0) return false;
    return (sb.st_mode & S_IFMT) == S_IFREG;
}

public bool Path$is_directory(Path_t path, bool follow_symlinks)
{
    struct stat sb;
    int status = path_stat(path, follow_symlinks, &sb);
    if (status != 0) return false;
    return (sb.st_mode & S_IFMT) == S_IFDIR;
}

public bool Path$is_pipe(Path_t path, bool follow_symlinks)
{
    struct stat sb;
    int status = path_stat(path, follow_symlinks, &sb);
    if (status != 0) return false;
    return (sb.st_mode & S_IFMT) == S_IFIFO;
}

public bool Path$is_socket(Path_t path, bool follow_symlinks)
{
    struct stat sb;
    int status = path_stat(path, follow_symlinks, &sb);
    if (status != 0) return false;
    return (sb.st_mode & S_IFMT) == S_IFSOCK;
}

public bool Path$is_symlink(Path_t path)
{
    struct stat sb;
    int status = path_stat(path, false, &sb);
    if (status != 0) return false;
    return (sb.st_mode & S_IFMT) == S_IFLNK;
}

public OptionalDateTime_t Path$modified(Path_t path, bool follow_symlinks)
{
    struct stat sb;
    int status = path_stat(path, follow_symlinks, &sb);
    if (status != 0) return NULL_DATETIME;
    return (DateTime_t){.tv_sec=sb.st_mtime};
}

public OptionalDateTime_t Path$accessed(Path_t path, bool follow_symlinks)
{
    struct stat sb;
    int status = path_stat(path, follow_symlinks, &sb);
    if (status != 0) return NULL_DATETIME;
    return (DateTime_t){.tv_sec=sb.st_atime};
}

public OptionalDateTime_t Path$changed(Path_t path, bool follow_symlinks)
{
    struct stat sb;
    int status = path_stat(path, follow_symlinks, &sb);
    if (status != 0) return NULL_DATETIME;
    return (DateTime_t){.tv_sec=sb.st_ctime};
}

static void _write(Path_t path, Array_t bytes, int mode, int permissions)
{
    path = Path$_expand_home(path);
    const char *path_str = Text$as_c_string(path);
    int fd = open(path_str, mode, permissions);
    if (fd == -1)
        fail("Could not write to file: %s\n%s", path_str, strerror(errno));

    if (bytes.stride != 1)
        Array$compact(&bytes, 1);
    ssize_t written = write(fd, bytes.data, (size_t)bytes.length);
    if (written != (ssize_t)bytes.length)
        fail("Could not write to file: %s\n%s", path_str, strerror(errno));
}

public void Path$write(Path_t path, Text_t text, int permissions)
{
    Array_t bytes = Text$utf8_bytes(text);
    _write(path, bytes, O_WRONLY | O_CREAT, permissions);
}

public void Path$write_bytes(Path_t path, Array_t bytes, int permissions)
{
    _write(path, bytes, O_WRONLY | O_CREAT, permissions);
}

public void Path$append(Path_t path, Text_t text, int permissions)
{
    Array_t bytes = Text$utf8_bytes(text);
    _write(path, bytes, O_WRONLY | O_APPEND | O_CREAT, permissions);
}

public void Path$append_bytes(Path_t path, Array_t bytes, int permissions)
{
    _write(path, bytes, O_WRONLY | O_APPEND | O_CREAT, permissions);
}

public OptionalArray_t Path$read_bytes(Path_t path)
{
    path = Path$_expand_home(path);
    int fd = open(Text$as_c_string(path), O_RDONLY);
    if (fd == -1)
        return NULL_ARRAY;

    struct stat sb;
    if (fstat(fd, &sb) != 0)
        return NULL_ARRAY;

    if ((sb.st_mode & S_IFMT) == S_IFREG) { // Use memory mapping if it's a real file:
        const char *mem = mmap(NULL, (size_t)sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        char *content = GC_MALLOC_ATOMIC((size_t)sb.st_size+1);
        memcpy(content, mem, (size_t)sb.st_size);
        content[sb.st_size] = '\0';
        close(fd);
        return (Array_t){.data=content, .atomic=1, .stride=1, .length=(int64_t)sb.st_size};
    } else {
        size_t capacity = 256, len = 0;
        char *content = GC_MALLOC_ATOMIC(capacity);
        for (;;) {
            char chunk[256];
            ssize_t just_read = read(fd, chunk, sizeof(chunk));
            if (just_read < 0)
                return NULL_ARRAY;
            else if (just_read == 0) {
                if (errno == EAGAIN || errno == EINTR)
                    continue;
                break;
            }

            if (len + (size_t)just_read >= capacity) {
                content = GC_REALLOC(content, (capacity *= 2));
            }

            memcpy(&content[len], chunk, (size_t)just_read);
            len += (size_t)just_read;

            if ((size_t)just_read < sizeof(chunk))
                break;
        }
        close(fd);
        return (Array_t){.data=content, .atomic=1, .stride=1, .length=len};
    }
}

public OptionalText_t Path$read(Path_t path)
{
    Array_t bytes = Path$read_bytes(path);
    if (bytes.length < 0) return NULL_TEXT;
    return Text$from_bytes(bytes);
}

public void Path$remove(Path_t path, bool ignore_missing)
{
    path = Path$_expand_home(path);
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
    path = Path$_expand_home(path);
    char *c_path = Text$as_c_string(path);
    char *end = c_path + strlen(c_path);
    if (*end == '/' && end > c_path) {
        *end = '\0';
        --end;
    }
    char *end_of_component = strchrnul(c_path + 1, '/');
    for (;;) {
        if (end_of_component < end)
            *end_of_component = '\0';

        int status = mkdir(c_path, (mode_t)permissions);
        if (status != 0 && errno != EEXIST)
            fail("Could not create directory: %s (%s)", c_path, strerror(errno));

        if (end_of_component >= end)
            break;

        *end_of_component = '/';
        end_of_component = strchrnul(end_of_component + 1, '/');
    }
}

static Array_t _filtered_children(Path_t path, bool include_hidden, mode_t filter)
{
    path = Path$_expand_home(path);
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
    path = Path$_expand_home(path);
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

public Text_t Path$write_unique_bytes(Path_t path, Array_t bytes)
{
    path = Path$_expand_home(path);
    const char *path_str = Text$as_c_string(path);
    size_t len = strlen(path_str);
    if (len >= PATH_MAX) fail("Path is too long: %s", path_str);
    char buf[PATH_MAX] = {};
    strcpy(buf, path_str);

    // Count the number of trailing characters leading up to the last "X"
    // (e.g. "foo_XXXXXX.tmp" would yield suffixlen = 4)
    size_t suffixlen = 0;
    while (suffixlen < len && buf[len - 1 - suffixlen] != 'X')
        ++suffixlen;

    int fd = mkstemps(buf, suffixlen);
    if (fd == -1)
        fail("Could not write to unique file: %s\n%s", buf, strerror(errno));

    if (bytes.stride != 1)
        Array$compact(&bytes, 1);

    ssize_t written = write(fd, bytes.data, (size_t)bytes.length);
    if (written != (ssize_t)bytes.length)
        fail("Could not write to file: %s\n%s", buf, strerror(errno));
    return Text$format("%s", buf);
}

public Text_t Path$write_unique(Path_t path, Text_t text)
{
    return Path$write_unique_bytes(path, Text$utf8_bytes(text));
}

public Path_t Path$parent(Path_t path)
{
    return Path$cleanup(Text$concat(path, Path("/../")));
}

public Text_t Path$base_name(Path_t path)
{
    path = Path$cleanup(path);
    if (Text$ends_with(path, Path("/")))
        return Text$replace(path, Pattern("{0+..}/{!/}/{end}"), Text("@2"), Text("@"), false);
    else
        return Text$replace(path, Pattern("{0+..}/{!/}{end}"), Text("@2"), Text("@"), false);
}

public Text_t Path$extension(Path_t path, bool full)
{
    Text_t base = Path$base_name(path);
    Array_t results = Text$matches(base, full ? Pattern(".{!.}.{..}") : Pattern(".{..}.{!.}{end}"));
    if (results.length > 0)
        return *((Text_t*)(results.data + results.stride*1));
    results = Text$matches(base, full ? Pattern("{!.}.{..}") : Pattern("{..}.{!.}{end}"));
    if (results.length > 0)
        return *((Text_t*)(results.data + results.stride*1));
    else
        return Text("");
}

static void _line_reader_cleanup(FILE **f)
{
    if (f && *f) {
        fclose(*f);
        *f = NULL;
    }
}

static Text_t _next_line(FILE **f)
{
    if (!f || !*f) return NULL_TEXT;

    char *line = NULL;
    size_t size = 0;
    ssize_t len = getline(&line, &size, *f);
    if (len <= 0) {
        _line_reader_cleanup(f);
        return NULL_TEXT;
    }

    while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n'))
        --len;

    if (u8_check((uint8_t*)line, (size_t)len) != NULL)
        fail("Invalid UTF8!");

    Text_t line_text = Text$format("%.*s", len, line);
    free(line);
    return line_text;
}

public OptionalClosure_t Path$by_line(Path_t path)
{
    path = Path$_expand_home(path);

    FILE *f = fopen(Text$as_c_string(path), "r");
    if (f == NULL)
        return NULL_CLOSURE;

    FILE **wrapper = GC_MALLOC(sizeof(FILE*));
    *wrapper = f;
    GC_register_finalizer(wrapper, (void*)_line_reader_cleanup, NULL, NULL, NULL);
    return (Closure_t){.fn=(void*)_next_line, .userdata=wrapper};
}

public Array_t Path$glob(Path_t path)
{
    glob_t glob_result;
    int status = glob(Text$as_c_string(path), GLOB_BRACE | GLOB_TILDE | GLOB_TILDE_CHECK, NULL, &glob_result);
    if (status != 0 && status != GLOB_NOMATCH)
        fail("Failed to perform globbing");

    Array_t glob_files = {};
    for (size_t i = 0; i < glob_result.gl_pathc; i++) {
        size_t len = strlen(glob_result.gl_pathv[i]);
        if ((len >= 2 && glob_result.gl_pathv[i][len-1] == '.' && glob_result.gl_pathv[i][len-2] == '/')
            || (len >= 2 && glob_result.gl_pathv[i][len-1] == '.' && glob_result.gl_pathv[i][len-2] == '.' && glob_result.gl_pathv[i][len-3] == '/'))
            continue;
        Array$insert(&glob_files, (Text_t[1]){Text$from_str(glob_result.gl_pathv[i])}, I(0), sizeof(Text_t));
    }
    return glob_files;
}

public const TypeInfo_t Path$info = {
    .size=sizeof(Path_t),
    .align=__alignof__(Path_t),
    .tag=TextInfo,
    .TextInfo={.lang="Path"},
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
