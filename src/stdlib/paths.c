// A lang for filesystem paths
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <gc.h>
#include <glob.h>
#include <grp.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistr.h>

#include "print.h"
#include "arrays.h"
#include "enums.h"
#include "files.h"
#include "integers.h"
#include "optionals.h"
#include "paths.h"
#include "patterns.h"
#include "structs.h"
#include "text.h"
#include "types.h"
#include "util.h"

// Use inline version of the siphash code for performance:
#include "siphash.h"
#include "siphash-internals.h"

static const Path_t HOME_PATH = {.type.$tag=PATH_HOME},
             ROOT_PATH = {.type.$tag=PATH_ABSOLUTE},
             CURDIR_PATH = {.type.$tag=PATH_RELATIVE};

static void clean_components(Array_t *components)
{
    for (int64_t i = 0; i < components->length; ) {
        Text_t *component = (Text_t*)(components->data + i*components->stride);
        if (component->length == 0 || Text$equal_values(*component, Text("."))) {
            Array$remove_at(components, I(i+1), I(1), sizeof(Text_t));
        } else if (i > 0 && Text$equal_values(*component, Text(".."))) {
            Text_t *prev = (Text_t*)(components->data + (i-1)*components->stride);
            if (!Text$equal_values(*prev, Text(".."))) {
                Array$remove_at(components, I(i), I(2), sizeof(Text_t));
                i -= 1;
            } else {
                i += 1;
            }
        } else {
            i += 1;
        }
    }
}

public Path_t Path$from_str(const char *str)
{
    if (!str || str[0] == '\0' || streq(str, "/")) return ROOT_PATH;
    else if (streq(str, "~")) return HOME_PATH;
    else if (streq(str, ".")) return CURDIR_PATH;

    Path_t result = {.components={}};
    if (str[0] == '/') {
        result.type.$tag = PATH_ABSOLUTE;
        str += 1;
    } else if (str[0] == '~' && str[1] == '/') {
        result.type.$tag = PATH_HOME;
        str += 2;
    } else if (str[0] == '.' && str[1] == '/') {
        result.type.$tag = PATH_RELATIVE;
        str += 2;
    } else {
        result.type.$tag = PATH_RELATIVE;
    }

    while (str && *str) {
        size_t component_len = strcspn(str, "/");
        if (component_len > 0) {
            if (component_len == 1 && str[0] == '.') {
                // ignore /./
            } else if (component_len == 2 && strncmp(str, "..", 2) == 0
                       && result.components.length > 1
                       && !Text$equal_values(Text(".."), *(Text_t*)(result.components.data + result.components.stride*(result.components.length-1)))) {
                // Pop off /foo/baz/.. -> /foo
                Array$remove_at(&result.components, I(result.components.length), I(1), sizeof(Text_t));
            } else { 
                Text_t component = Text$from_strn(str, component_len);
                Array$insert_value(&result.components, component, I(0), sizeof(Text_t));
            }
            str += component_len;
        }
        str += strspn(str, "/");
    }
    return result;
}

public Path_t Path$from_text(Text_t text)
{
    return Path$from_str(Text$as_c_string(text));
}

public Path_t Path$expand_home(Path_t path)
{
    if (path.type.$tag == PATH_HOME) {
        Path_t pwd = Path$from_str(getenv("HOME"));
        Array_t components = Array$concat(pwd.components, path.components, sizeof(Text_t));
        assert(components.length == path.components.length + pwd.components.length);
        clean_components(&components);
        path = (Path_t){.type.$tag=PATH_ABSOLUTE, .components=components};
    }
    return path;
}

public Path_t Path$_concat(int n, Path_t items[n])
{
    assert(n > 0);
    Path_t result = items[0];
    ARRAY_INCREF(result.components);
    for (int i = 1; i < n; i++) {
        if (items[i].type.$tag != PATH_RELATIVE)
            fail("Cannot concatenate an absolute or home-based path onto another path: (", items[i], ")");
        Array$insert_all(&result.components, items[i].components, I(0), sizeof(Text_t));
    }
    clean_components(&result.components);
    return result;
}

public Path_t Path$resolved(Path_t path, Path_t relative_to)
{
    if (path.type.$tag == PATH_RELATIVE && !(relative_to.type.$tag == PATH_RELATIVE && relative_to.components.length == 0)) {
        Path_t result = {.type.$tag=relative_to.type.$tag};
        result.components = relative_to.components;
        ARRAY_INCREF(result.components);
        Array$insert_all(&result.components, path.components, I(0), sizeof(Text_t));
        clean_components(&result.components);
        return result;
    }
    return path;
}

public Path_t Path$relative_to(Path_t path, Path_t relative_to)
{
    if (path.type.$tag != relative_to.type.$tag)
        fail("Cannot create a path relative to a different path with a mismatching type: (", path, ") relative to (", relative_to, ")");

    Path_t result = {.type.$tag=PATH_RELATIVE};
    int64_t shared = 0;
    for (; shared < path.components.length && shared < relative_to.components.length; shared++) {
        Text_t *p = (Text_t*)(path.components.data + shared*path.components.stride);
        Text_t *r = (Text_t*)(relative_to.components.data + shared*relative_to.components.stride);
        if (!Text$equal_values(*p, *r))
            break;
    }

    for (int64_t i = shared; i < relative_to.components.length; i++)
        Array$insert_value(&result.components, Text(".."), I(1), sizeof(Text_t));

    for (int64_t i = shared; i < path.components.length; i++) {
        Text_t *p = (Text_t*)(path.components.data + i*path.components.stride);
        Array$insert(&result.components, p, I(0), sizeof(Text_t));
    }
    //clean_components(&result.components);
    return result;
}

public bool Path$exists(Path_t path)
{
    path = Path$expand_home(path);
    struct stat sb;
    return (stat(Path$as_c_string(path), &sb) == 0);
}

static INLINE int path_stat(Path_t path, bool follow_symlinks, struct stat *sb)
{
    path = Path$expand_home(path);
    const char *path_str = Path$as_c_string(path);
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

public bool Path$can_read(Path_t path)
{
    path = Path$expand_home(path);
    const char *path_str = Path$as_c_string(path);
#ifdef _GNU_SOURCE
    return (euidaccess(path_str, R_OK) == 0);
#else
    return (access(path_str, R_OK) == 0);
#endif
}

public bool Path$can_write(Path_t path)
{
    path = Path$expand_home(path);
    const char *path_str = Path$as_c_string(path);
#ifdef _GNU_SOURCE
    return (euidaccess(path_str, W_OK) == 0);
#else
    return (access(path_str, W_OK) == 0);
#endif
}

public bool Path$can_execute(Path_t path)
{
    path = Path$expand_home(path);
    const char *path_str = Path$as_c_string(path);
#ifdef _GNU_SOURCE
    return (euidaccess(path_str, X_OK) == 0);
#else
    return (access(path_str, X_OK) == 0);
#endif
}

public OptionalMoment_t Path$modified(Path_t path, bool follow_symlinks)
{
    struct stat sb;
    int status = path_stat(path, follow_symlinks, &sb);
    if (status != 0) return NONE_MOMENT;
    return (Moment_t){.tv_sec=sb.st_mtime};
}

public OptionalMoment_t Path$accessed(Path_t path, bool follow_symlinks)
{
    struct stat sb;
    int status = path_stat(path, follow_symlinks, &sb);
    if (status != 0) return NONE_MOMENT;
    return (Moment_t){.tv_sec=sb.st_atime};
}

public OptionalMoment_t Path$changed(Path_t path, bool follow_symlinks)
{
    struct stat sb;
    int status = path_stat(path, follow_symlinks, &sb);
    if (status != 0) return NONE_MOMENT;
    return (Moment_t){.tv_sec=sb.st_ctime};
}

static void _write(Path_t path, Array_t bytes, int mode, int permissions)
{
    path = Path$expand_home(path);
    const char *path_str = Path$as_c_string(path);
    int fd = open(path_str, mode, permissions);
    if (fd == -1)
        fail("Could not write to file: ", path_str, "\n", strerror(errno));

    if (bytes.stride != 1)
        Array$compact(&bytes, 1);
    ssize_t written = write(fd, bytes.data, (size_t)bytes.length);
    if (written != (ssize_t)bytes.length)
        fail("Could not write to file: ", path_str, "\n", strerror(errno));
    close(fd);
}

public void Path$write(Path_t path, Text_t text, int permissions)
{
    Array_t bytes = Text$utf8_bytes(text);
    _write(path, bytes, O_WRONLY | O_CREAT | O_TRUNC, permissions);
}

public void Path$write_bytes(Path_t path, Array_t bytes, int permissions)
{
    _write(path, bytes, O_WRONLY | O_CREAT | O_TRUNC, permissions);
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

public OptionalArray_t Path$read_bytes(Path_t path, OptionalInt_t count)
{
    path = Path$expand_home(path);
    int fd = open(Path$as_c_string(path), O_RDONLY);
    if (fd == -1)
        return NONE_ARRAY;

    struct stat sb;
    if (fstat(fd, &sb) != 0)
        return NONE_ARRAY;

    int64_t const target_count = count.small ? Int64$from_int(count, false) : INT64_MAX;
    if (target_count < 0)
        fail("Cannot read a negative number of bytes!");

    if ((sb.st_mode & S_IFMT) == S_IFREG) { // Use memory mapping if it's a real file:
        const char *mem = mmap(NULL, (size_t)sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        char *content = GC_MALLOC_ATOMIC((size_t)sb.st_size+1);
        memcpy(content, mem, (size_t)sb.st_size);
        content[sb.st_size] = '\0';
        close(fd);
        if (count.small && (int64_t)sb.st_size < target_count)
            fail("Could not read ", target_count, " bytes from ", path, " (only got ", sb.st_size, ")");
        int64_t len = count.small ? target_count : (int64_t)sb.st_size;
        return (Array_t){.data=content, .atomic=1, .stride=1, .length=len};
    } else {
        size_t capacity = 256, len = 0;
        char *content = GC_MALLOC_ATOMIC(capacity);
        int64_t count_remaining = target_count;
        for (;;) {
            char chunk[256];
            size_t to_read = count_remaining < (int64_t)sizeof(chunk) ? (size_t)count_remaining : sizeof(chunk);
            ssize_t just_read = read(fd, chunk, to_read);
            if (just_read < 0) {
                close(fd);
                return NONE_ARRAY;
            } else if (just_read == 0) {
                if (errno == EAGAIN || errno == EINTR)
                    continue;
                break;
            }
            count_remaining -= (int64_t)just_read;

            if (len + (size_t)just_read >= capacity) {
                content = GC_REALLOC(content, (capacity *= 2));
            }

            memcpy(&content[len], chunk, (size_t)just_read);
            len += (size_t)just_read;
        }
        close(fd);
        if (count.small != 0 && (int64_t)len < target_count)
            fail("Could not read ", target_count, " bytes from ", path, " (only got ", len, ")");
        return (Array_t){.data=content, .atomic=1, .stride=1, .length=len};
    }
}

public OptionalText_t Path$read(Path_t path)
{
    Array_t bytes = Path$read_bytes(path, NONE_INT);
    if (bytes.length < 0) return NONE_TEXT;
    return Text$from_bytes(bytes);
}

public OptionalText_t Path$owner(Path_t path, bool follow_symlinks)
{
    struct stat sb;
    int status = path_stat(path, follow_symlinks, &sb);
    if (status != 0) return NONE_TEXT;
    struct passwd *pw = getpwuid(sb.st_uid);
    return pw ? Text$from_str(pw->pw_name) : NONE_TEXT;
}

public OptionalText_t Path$group(Path_t path, bool follow_symlinks)
{
    struct stat sb;
    int status = path_stat(path, follow_symlinks, &sb);
    if (status != 0) return NONE_TEXT;
    struct group *gr = getgrgid(sb.st_uid);
    return gr ? Text$from_str(gr->gr_name) : NONE_TEXT;
}

public void Path$set_owner(Path_t path, OptionalText_t owner, OptionalText_t group, bool follow_symlinks)
{
    uid_t owner_id = (uid_t)-1;
    if (owner.length >= 0) {
        struct passwd *pwd = getpwnam(Text$as_c_string(owner));
        if (pwd == NULL) fail("Not a valid user: ", owner);
        owner_id = pwd->pw_uid;
    }

    gid_t group_id = (gid_t)-1;
    if (group.length >= 0) {
        struct group *grp = getgrnam(Text$as_c_string(group));
        if (grp == NULL) fail("Not a valid group: ", group);
        group_id = grp->gr_gid;
    }
    const char *path_str = Path$as_c_string(path);
    int result = follow_symlinks ? chown(path_str, owner_id, group_id) : lchown(path_str, owner_id, group_id);
    if (result < 0)
        fail("Could not set owner!");
}

static int _remove_files(const char *path, const struct stat *sbuf, int type, struct FTW *ftwb)
{
    (void)sbuf, (void)ftwb;
    switch (type) {
    case FTW_F: case FTW_SL: case FTW_SLN:
        if (remove(path) < 0) {
            fail("Could not remove file: ", path, " (", strerror(errno), ")");
            return -1;
        }
        return 0;
    case FTW_DP:
        if (rmdir(path) != 0)
            fail("Could not remove directory: ", path, " (", strerror(errno), ")");
        return 0;
    default:
        fail("Could not remove path: ", path, " (not a file or directory)");
        return -1;
    }
}

public void Path$remove(Path_t path, bool ignore_missing)
{
    path = Path$expand_home(path);
    const char *path_str = Path$as_c_string(path);
    struct stat sb;
    if (lstat(path_str, &sb) != 0) {
        if (!ignore_missing)
            fail("Could not remove file: ", path_str, " (", strerror(errno), ")");
        return;
    }

    if ((sb.st_mode & S_IFMT) == S_IFREG || (sb.st_mode & S_IFMT) == S_IFLNK) {
        if (unlink(path_str) != 0 && !ignore_missing)
            fail("Could not remove file: ", path_str, " (", strerror(errno), ")");
    } else if ((sb.st_mode & S_IFMT) == S_IFDIR) {
        const int num_open_fd = 10;
        if (nftw(path_str, _remove_files, num_open_fd, FTW_DEPTH|FTW_MOUNT|FTW_PHYS) < 0)
            fail("Could not remove directory: %s (%s)", path_str, strerror(errno));
    } else {
        fail("Could not remove path: ", path_str, " (not a file or directory)");
    }
}

public void Path$create_directory(Path_t path, int permissions)
{
    path = Path$expand_home(path);
    const char *c_path = Path$as_c_string(path);
    int status = mkdir(c_path, (mode_t)permissions);
    if (status != 0 && errno != EEXIST)
        fail("Could not create directory: ", c_path, " (", strerror(errno), ")");
}

static Array_t _filtered_children(Path_t path, bool include_hidden, mode_t filter)
{
    path = Path$expand_home(path);
    struct dirent *dir;
    Array_t children = {};
    const char *path_str = Path$as_c_string(path);
    size_t path_len = strlen(path_str);
    DIR *d = opendir(path_str);
    if (!d)
        fail("Could not open directory: ", path, " (", strerror(errno), ")");

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

        Path_t child = Path$from_str(child_str);
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
    path = Path$expand_home(path);
    const char *path_str = Path$as_c_string(path);
    size_t len = strlen(path_str);
    if (len >= PATH_MAX) fail("Path is too long: ", path_str);
    char buf[PATH_MAX] = {};
    strcpy(buf, path_str);
    if (buf[len-1] == '/')
        buf[--len] = '\0';
    char *created = mkdtemp(buf);
    if (!created) fail("Failed to create temporary directory: ", path_str, " (", strerror(errno), ")");
    return Path$from_str(created);
}

public Path_t Path$write_unique_bytes(Path_t path, Array_t bytes)
{
    path = Path$expand_home(path);
    const char *path_str = Path$as_c_string(path);
    size_t len = strlen(path_str);
    if (len >= PATH_MAX) fail("Path is too long: ", path_str);
    char buf[PATH_MAX] = {};
    strcpy(buf, path_str);

    // Count the number of trailing characters leading up to the last "X"
    // (e.g. "foo_XXXXXX.tmp" would yield suffixlen = 4)
    size_t suffixlen = 0;
    while (suffixlen < len && buf[len - 1 - suffixlen] != 'X')
        ++suffixlen;

    int fd = mkstemps(buf, suffixlen);
    if (fd == -1)
        fail("Could not write to unique file: ", buf, "\n", strerror(errno));

    if (bytes.stride != 1)
        Array$compact(&bytes, 1);

    ssize_t written = write(fd, bytes.data, (size_t)bytes.length);
    if (written != (ssize_t)bytes.length)
        fail("Could not write to file: ", buf, "\n", strerror(errno));
    close(fd);
    return Path$from_str(buf);
}

public Path_t Path$write_unique(Path_t path, Text_t text)
{
    return Path$write_unique_bytes(path, Text$utf8_bytes(text));
}

public Path_t Path$parent(Path_t path)
{
    if (path.type.$tag == PATH_ABSOLUTE && path.components.length == 0) {
        return path;
    } else if (path.components.length > 0 && !Text$equal_values(*(Text_t*)(path.components.data + path.components.stride*(path.components.length-1)),
                                                         Text(".."))) {
        return (Path_t){.type.$tag=path.type.$tag, .components=Array$slice(path.components, I(1), I(-2))};
    } else {
        Path_t result = {.type.$tag=path.type.$tag, .components=path.components};
        ARRAY_INCREF(result.components);
        Array$insert_value(&result.components, Text(".."), I(0), sizeof(Text_t));
        return result;
    }
}

public PUREFUNC Text_t Path$base_name(Path_t path)
{
    if (path.components.length >= 1)
        return *(Text_t*)(path.components.data + path.components.stride*(path.components.length-1));
    else if (path.type.$tag == PATH_HOME)
        return Text("~");
    else if (path.type.$tag == PATH_RELATIVE)
        return Text(".");
    else
        return EMPTY_TEXT;
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

public Path_t Path$with_component(Path_t path, Text_t component)
{
    Path_t result = {
        .type.$tag=path.type.$tag,
        .components=path.components,
    };
    ARRAY_INCREF(result.components);
    Array$insert(&result.components, &component, I(0), sizeof(Text_t));
    clean_components(&result.components);
    return result;
}

public Path_t Path$with_extension(Path_t path, Text_t extension, bool replace)
{
    if (path.components.length == 0)
        fail("A path with no components can't have an extension!");

    Path_t result = {
        .type.$tag=path.type.$tag,
        .components=path.components,
    };
    ARRAY_INCREF(result.components);
    Text_t last = *(Text_t*)(path.components.data + path.components.stride*(path.components.length-1));
    Array$remove_at(&result.components, I(-1), I(1), sizeof(Text_t));
    if (replace) {
        if (Text$starts_with(last, Text(".")))
            last = Text$replace(last, Pattern(".{!.}.{..}"), Text(".@1"), Pattern("@"), false);
        else
            last = Text$replace(last, Pattern("{!.}.{..}"), Text("@1"), Pattern("@"), false);
    }

    last = Text$concat(last, extension);
    Array$insert(&result.components, &last, I(0), sizeof(Text_t));
    return result;
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
    if (!f || !*f) return NONE_TEXT;

    char *line = NULL;
    size_t size = 0;
    ssize_t len = getline(&line, &size, *f);
    if (len <= 0) {
        _line_reader_cleanup(f);
        return NONE_TEXT;
    }

    while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n'))
        --len;

    if (u8_check((uint8_t*)line, (size_t)len) != NULL)
        fail("Invalid UTF8!");

    Text_t line_text = Text$from_strn(line, (size_t)len);
    free(line);
    return line_text;
}

public OptionalClosure_t Path$by_line(Path_t path)
{
    path = Path$expand_home(path);

    FILE *f = fopen(Path$as_c_string(path), "r");
    if (f == NULL)
        return NONE_CLOSURE;

    FILE **wrapper = GC_MALLOC(sizeof(FILE*));
    *wrapper = f;
    GC_register_finalizer(wrapper, (void*)_line_reader_cleanup, NULL, NULL, NULL);
    return (Closure_t){.fn=(void*)_next_line, .userdata=wrapper};
}

public Array_t Path$glob(Path_t path)
{
    glob_t glob_result;
    int status = glob(Path$as_c_string(path), GLOB_BRACE | GLOB_TILDE | GLOB_TILDE_CHECK, NULL, &glob_result);
    if (status != 0 && status != GLOB_NOMATCH)
        fail("Failed to perform globbing");

    Array_t glob_files = {};
    for (size_t i = 0; i < glob_result.gl_pathc; i++) {
        size_t len = strlen(glob_result.gl_pathv[i]);
        if ((len >= 2 && glob_result.gl_pathv[i][len-1] == '.' && glob_result.gl_pathv[i][len-2] == '/')
            || (len >= 2 && glob_result.gl_pathv[i][len-1] == '.' && glob_result.gl_pathv[i][len-2] == '.' && glob_result.gl_pathv[i][len-3] == '/'))
            continue;
        Path_t p = Path$from_str(glob_result.gl_pathv[i]);
        Array$insert(&glob_files, &p, I(0), sizeof(Path_t));
    }
    return glob_files;
}

public PUREFUNC uint64_t Path$hash(const void *obj, const TypeInfo_t *type)
{
    (void)type;
    Path_t *path = (Path_t*)obj;
    siphash sh;
    siphashinit(&sh, (uint64_t)path->type.$tag);
    for (int64_t i = 0; i < path->components.length; i++) {
        uint64_t item_hash = Text$hash(path->components.data + i*path->components.stride, &Text$info);
        siphashadd64bits(&sh, item_hash);
    }
    return siphashfinish_last_part(&sh, (uint64_t)path->components.length);
}

public PUREFUNC int32_t Path$compare(const void *va, const void *vb, const TypeInfo_t *type)
{
    (void)type;
    Path_t *a = (Path_t*)va, *b = (Path_t*)vb;
    int diff = ((int)a->type.$tag - (int)b->type.$tag);
    if (diff != 0) return diff;
    return Array$compare(&a->components, &b->components, Array$info(&Text$info));
}

public PUREFUNC bool Path$equal(const void *va, const void *vb, const TypeInfo_t *type)
{
    (void)type;
    Path_t *a = (Path_t*)va, *b = (Path_t*)vb;
    if (a->type.$tag != b->type.$tag) return false;
    return Array$equal(&a->components, &b->components, Array$info(&Text$info));
}

public PUREFUNC bool Path$equal_values(Path_t a, Path_t b)
{
    if (a.type.$tag != b.type.$tag) return false;
    return Array$equal(&a.components, &b.components, Array$info(&Text$info));
}

public int Path$print(FILE *f, Path_t path)
{
    if (path.components.length == 0) {
        if (path.type.$tag == PATH_ABSOLUTE) return fputs("/", f);
        else if (path.type.$tag == PATH_RELATIVE) return fputs(".", f);
        else if (path.type.$tag == PATH_HOME) return fputs("~", f);
    }

    int n = 0;
    if (path.type.$tag == PATH_ABSOLUTE) {
        n += fputc('/', f);
    } else if (path.type.$tag == PATH_HOME) {
        n += fputs("~/", f);
    } else if (path.type.$tag == PATH_RELATIVE) {
        if (!Text$equal_values(*(Text_t*)path.components.data, Text("..")))
            n += fputs("./", f);
    }

    for (int64_t i = 0; i < path.components.length; i++) {
        Text_t *comp = (Text_t*)(path.components.data + i*path.components.stride);
        n += Text$print(f, *comp);
        if (i + 1 < path.components.length)
            n += fputc('/', f);
    }
    return n;
}

public const char *Path$as_c_string(Path_t path)
{
    return String(path);
}

public Text_t Path$as_text(const void *obj, bool color, const TypeInfo_t *type)
{
    (void)type;
    if (!obj) return Text("Path");
    Path_t *path = (Path_t*)obj;
    Text_t text = Text$join(Text("/"), path->components);
    if (path->type.$tag == PATH_HOME)
        text = Text$concat(path->components.length > 0 ? Text("~/") : Text("~"), text);
    else if (path->type.$tag == PATH_ABSOLUTE)
        text = Text$concat(Text("/"), text);
    else if (path->type.$tag == PATH_RELATIVE && (path->components.length == 0 || !Text$equal_values(*(Text_t*)(path->components.data), Text(".."))))
        text = Text$concat(path->components.length > 0 ? Text("./") : Text("."), text);

    if (color)
        text = Texts(Text("\033[32;1m"), text, Text("\033[m"));

    return text;
}

public CONSTFUNC bool Path$is_none(const void *obj, const TypeInfo_t *type)
{
    (void)type;
    return ((Path_t*)obj)->type.$tag == PATH_NONE;
}

public void Path$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t *type)
{
    (void)type;
    Path_t *path = (Path_t*)obj;
    fputc((int)path->type.$tag, out);
    Array$serialize(&path->components, out, pointers, Array$info(&Text$info));
}

public void Path$deserialize(FILE *in, void *obj, Array_t *pointers, const TypeInfo_t *type)
{
    (void)type;
    Path_t path = {};
    path.type.$tag = fgetc(in);
    Array$deserialize(in, &path.components, pointers, Array$info(&Text$info));
    *(Path_t*)obj = path;
}

public const TypeInfo_t Path$info = {
    .size=sizeof(Path_t),
    .align=__alignof__(Path_t),
    .tag=OpaqueInfo,
    .metamethods={
        .as_text=Path$as_text,
        .hash=Path$hash,
        .compare=Path$compare,
        .equal=Path$equal,
        .is_none=Path$is_none,
        .serialize=Path$serialize,
        .deserialize=Path$deserialize,
    }
};

public const TypeInfo_t PathType$info = {
    .size=sizeof(PathType_t),
    .align=__alignof__(PathType_t),
    .metamethods=PackedDataEnum$metamethods,
    .tag=EnumInfo,
    .EnumInfo={
        .name="PathType",
        .num_tags=3,
        .tags=((NamedType_t[3]){{.name="Relative"}, {.name="Absolute"}, {.name="Home"}}),
    },
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
