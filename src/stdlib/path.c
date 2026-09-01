// A lang for filesystem paths

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <ftw.h>
#include <gc.h>
#include <glob.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../unistr-fixed.h"
#include "../util.h"
#include "c_string.h"
#include "datatypes/typeinfo.h"
#include "enums.h"
#include "integers.h"
#include "list.h"
#include "optionals.h"
#include "path.h"
#include "print.h"
#include "result.h"
#include "structs.h"
#include "text.h"
#include "util.h"

static const Path_t HOME_PATH = (Path_t){"~"}, ROOT_PATH = (Path_t){"/"}, CURDIR_PATH = (Path_t){"."},
                    PARENT_PATH = (Path_t){".."};

typedef enum { PATH_ABSOLUTE, PATH_RELATIVE, PATH_HOME } pathtype_t;

static pathtype_t path_type(Path_t path) {
    if (!path) return PATH_ABSOLUTE;
    if (path[0] == '/') return PATH_ABSOLUTE;
    if (path[0] == '~' && (path[1] == '\0' || path[1] == '/')) return PATH_HOME;
    return PATH_RELATIVE;
}

static void normalize_inplace(char path[PATH_MAX]) {
    if (path[0] == '.' && path[1 + strspn(path + 1, "/")] == '\0') {
        path[1] = '\0';
        return;
    }

    static char buf[PATH_MAX];
    char *src = path, *dest = buf;

    // Leading "/"
    if (*src == '/') {
        *(dest++) = *(src++);
    }
    *dest = '\0';

    for (size_t component_len; *src != '\0' && dest < &buf[PATH_MAX - 1]; src += component_len + 1) {
        component_len = strcspn(src, "/");
        if (component_len == 0) {
            ; // Skip empty "//"s:
        } else if (component_len == 1 && src[0] == '.' && dest > buf) {
            ; // Skip "." components
        } else {
            // Add "/" if there's a previous non-slash
            if (dest > buf && dest[-1] != '/') {
                *(dest++) = '/';
                *dest = '\0';
            }
            // For ".."
            if (component_len == 2 && src[0] == '.' && src[1] == '.') {
                // Find previous component:
                char *prev_slash = dest - 2;
                while (prev_slash >= buf && *prev_slash != '/')
                    prev_slash -= 1;

                // If previous component is not "..", then pop it (keeping the
                // root "/" when the popped component was the last one):
                if (prev_slash >= buf && *prev_slash == '/'
                    && strncmp(prev_slash, "/../", (size_t)(dest - prev_slash)) != 0) {
                    dest = (prev_slash == buf) ? buf + 1 : prev_slash;
                } else {
                    // Otherwise we need to keep the ".."
                    *(dest++) = '.';
                    *(dest++) = '.';
                }
            } else {
                // Otherwise copy over the component
                memcpy(dest, src, component_len);
                dest += component_len;
            }
            *dest = '\0';
        }
        if (src[component_len] == '\0') break;
    }

    *(dest++) = '\0';

    if (dest == buf) {
        path[0] = '.';
        path[1] = '\0';
    } else {
        memcpy(path, buf, strlen(buf) + 1);
    }
}

char *path_from_buf(char buf[PATH_MAX]) {
    normalize_inplace(buf);
    char *ret = GC_MALLOC_ATOMIC(strlen(buf) + 1);
    memcpy(ret, buf, strlen(buf) + 1);
    return ret;
}

// Normalize a string built with String(). normalize_inplace() wants a PATH_MAX
// buffer, so an over-long path fails here rather than being quietly truncated
// into a path naming some other file.
static Path_t path_from_string(const char *str) {
    size_t len = strlen(str);
    if (len >= PATH_MAX) fail("Path is too long: ", str);
    static char buf[PATH_MAX];
    memcpy(buf, str, len + 1);
    return path_from_buf(buf);
}

public
PUREFUNC
Path_t Path$from_str(const char *str) {
    if (!str || str[0] == '\0' || streq(str, "/")) return ROOT_PATH;
    else if (streq(str, "~") || streq(str, "~/")) return HOME_PATH;
    else if (streq(str, ".") || streq(str, "./")) return CURDIR_PATH;
    return str;
}

public
Path_t Path$from_text(Text_t text) {
    return Path$from_str(Text$as_c_string(text));
}

static OptionalPath_t Path$_concat2(OptionalPath_t a, OptionalPath_t b) {
    if (a == NULL || b == NULL) return NULL;
    if (path_type(b) != PATH_RELATIVE)
        fail("Cannot concatenate an absolute or home-based path onto another path: (", b, ")");

    if (b[0] == '.' && b[1] == '\0') return a;
    return path_from_string(String(a, "/", b));
}

// $HOME is not guaranteed to be set. Returning NULL here would reach opendir()
// and friends by way of expand_home(), so fall back to the password database
// and fail loudly rather than crash if even that has no answer.
static const char *home_directory(void) {
    const char *home = getenv("HOME");
    if (home && home[0] != '\0') return home;
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_dir && pw->pw_dir[0] != '\0') return pw->pw_dir;
    fail("Could not determine the home directory: $HOME is unset and the user has no home");
}

public
Path_t Path$expand_home(Path_t path) {
    if (path && path_type(path) == PATH_HOME) {
        const char *home = home_directory();
        if (path[1] == '/') return Path$_concat2(home, path + 2);
        else if (path[1] == '\0') return home;
    }
    return path;
}

// The inverse of expand_home(), for methods that must hand libc a real path but
// should still hand the caller back a path in the form they wrote it.
static Path_t unexpand_home(Path_t path) {
    if (!path) return path;
    const char *home = home_directory();
    size_t home_len = strlen(home);
    while (home_len > 1 && home[home_len - 1] == '/')
        --home_len;
    if (strncmp(path, home, home_len) != 0) return path;
    if (path[home_len] == '\0') return HOME_PATH;
    if (path[home_len] != '/') return path;
    return Path$from_str(String("~", path + home_len));
}

public
OptionalPath_t Path$_concat(int n, Path_t items[n]) {
    assert(n > 0);
    OptionalPath_t result = items[0];
    for (int i = 1; i < n; i++) {
        result = Path$_concat2(result, items[i]);
    }
    return result;
}

public
Path_t Path$resolved(Path_t path, Path_t relative_to) {
    if (!path) return path;
    switch (path_type(path)) {
    case PATH_HOME: return Path$expand_home(path);
    case PATH_ABSOLUTE: return path;
    case PATH_RELATIVE: return Path$_concat2(relative_to, path);
    default: return path;
    }
}

public
Path_t Path$relative_to(Path_t path, Path_t relative_to) {
    switch (path_type(relative_to)) {
    case PATH_HOME: relative_to = Path$expand_home(relative_to); break;
    case PATH_RELATIVE: relative_to = Path$resolved(relative_to, Path$current_dir()); break;
    default: break;
    }

    path = Path$resolved(path, Path$current_dir());

    int64_t shared = 0;
    for (int64_t i = 0;; i++) {
        if ((path[i] == '/' || path[i] == '\0') && (relative_to[i] == '/' || relative_to[i] == '\0')) {
            shared = i;
        }
        if (path[i] != relative_to[i] || !path[i]) break;
    }

    Path_t path_remainder = path[shared] == '\0' ? "" : &path[shared + 1];
    Path_t relative_remainder = relative_to[shared] == '\0' ? "" : &relative_to[shared + 1];
    if (strlen(path_remainder) > 0 && strlen(relative_remainder) == 0) {
        // "/foo/baz/qux" relative to "/foo/baz" => "qux"
        return path_remainder;
    }

    static char buf[PATH_MAX];
    char *dest = buf;
    for (const char *p = relative_remainder; *p; p += strcspn(p, "/"), p += strspn(p, "/")) {
        *(dest++) = '.';
        *(dest++) = '.';
        *(dest++) = '/';
    }
    memcpy(dest, path_remainder, strlen(path_remainder));
    dest += strlen(path_remainder);
    *dest = '\0';
    return path_from_buf(buf);
}

public
bool Path$exists(Path_t path) {
    path = Path$expand_home(path);
    struct stat sb;
    return (stat(path, &sb) == 0);
}

static INLINE int path_stat(Path_t path, bool follow_symlinks, struct stat *sb) {
    path = Path$expand_home(path);
    return follow_symlinks ? stat(path, sb) : lstat(path, sb);
}

public
bool Path$is_file(Path_t path, bool follow_symlinks) {
    struct stat sb;
    int status = path_stat(path, follow_symlinks, &sb);
    if (status != 0) return false;
    return (sb.st_mode & S_IFMT) == S_IFREG;
}

public
bool Path$is_directory(Path_t path, bool follow_symlinks) {
    struct stat sb;
    int status = path_stat(path, follow_symlinks, &sb);
    if (status != 0) return false;
    return (sb.st_mode & S_IFMT) == S_IFDIR;
}

public
bool Path$is_pipe(Path_t path, bool follow_symlinks) {
    struct stat sb;
    int status = path_stat(path, follow_symlinks, &sb);
    if (status != 0) return false;
    return (sb.st_mode & S_IFMT) == S_IFIFO;
}

public
bool Path$is_socket(Path_t path, bool follow_symlinks) {
    struct stat sb;
    int status = path_stat(path, follow_symlinks, &sb);
    if (status != 0) return false;
    return (sb.st_mode & S_IFMT) == S_IFSOCK;
}

public
bool Path$is_symlink(Path_t path) {
    struct stat sb;
    int status = path_stat(path, false, &sb);
    if (status != 0) return false;
    return (sb.st_mode & S_IFMT) == S_IFLNK;
}

public
OptionalPath_t Path$link(Path_t path) {
    static char buf[PATH_MAX];
    ssize_t status = readlink(Path$expand_home(path), buf, sizeof(buf));
    if (status == -1) return NONE_PATH;
    return Path$from_str(GC_strdup(buf));
}

// AT_EACCESS makes these check against the process's *effective* user/group id
// (what would actually govern an attempted access), not the real one.

public
bool Path$can_read(Path_t path) {
    path = Path$expand_home(path);
    return (faccessat(AT_FDCWD, path, R_OK, AT_EACCESS) == 0);
}

public
bool Path$can_write(Path_t path) {
    path = Path$expand_home(path);
    return (faccessat(AT_FDCWD, path, W_OK, AT_EACCESS) == 0);
}

public
bool Path$can_execute(Path_t path) {
    path = Path$expand_home(path);
    return (faccessat(AT_FDCWD, path, X_OK, AT_EACCESS) == 0);
}

public
OptionalInt64_t Path$modified(Path_t path, bool follow_symlinks) {
    struct stat sb;
    int status = path_stat(path, follow_symlinks, &sb);
    if (status != 0) return NONE_INT64;
    return (OptionalInt64_t){.value = (int64_t)sb.st_mtime};
}

public
OptionalInt64_t Path$accessed(Path_t path, bool follow_symlinks) {
    struct stat sb;
    int status = path_stat(path, follow_symlinks, &sb);
    if (status != 0) return NONE_INT64;
    return (OptionalInt64_t){.value = (int64_t)sb.st_atime};
}

public
OptionalInt64_t Path$changed(Path_t path, bool follow_symlinks) {
    struct stat sb;
    int status = path_stat(path, follow_symlinks, &sb);
    if (status != 0) return NONE_INT64;
    return (OptionalInt64_t){.value = (int64_t)sb.st_ctime};
}

static Result_t _write(Path_t path, List_t bytes, int mode, int permissions) {
    path = Path$expand_home(path);
    int fd = open(path, mode, permissions);
    if (fd == -1) {
        if (errno == EMFILE || errno == ENFILE) {
            // If we hit file handle limits, run GC collection to try to clean up any lingering file handles that
            // will be closed by GC finalizers.
            GC_gcollect();
            fd = open(path, mode, permissions);
        }
        if (fd == -1) return FailureResult("Could not write to file: ", path, " (", strerror(errno), ")");
    }

    if (bytes.stride != 1) List$compact(&bytes, 1);
    ssize_t written = write(fd, bytes.data, (size_t)bytes.length);
    if (written != (ssize_t)bytes.length)
        return FailureResult("Could not write to file: ", path, " (", strerror(errno), ")");
    close(fd);
    return SuccessResult;
}

public
Result_t Path$write(Path_t path, Text_t text, int permissions) {
    List_t bytes = Text$utf8(text);
    return _write(path, bytes, O_WRONLY | O_CREAT | O_TRUNC, permissions);
}

public
Result_t Path$write_bytes(Path_t path, List_t bytes, int permissions) {
    return _write(path, bytes, O_WRONLY | O_CREAT | O_TRUNC, permissions);
}

public
Result_t Path$append(Path_t path, Text_t text, int permissions) {
    List_t bytes = Text$utf8(text);
    return _write(path, bytes, O_WRONLY | O_APPEND | O_CREAT, permissions);
}

public
Result_t Path$append_bytes(Path_t path, List_t bytes, int permissions) {
    return _write(path, bytes, O_WRONLY | O_APPEND | O_CREAT, permissions);
}

typedef struct {
    const char *path;
    int fd;
    int mode;
    int permissions;
} writer_data_t;

static Result_t _write_bytes_to_fd(List_t bytes, bool close_file, void *userdata) {
    writer_data_t *data = userdata;
    if (bytes.length > 0) {
        if (data->fd == -1) {
            data->fd = open(data->path, data->mode, data->permissions);
            if (data->fd == -1) {
                if (errno == EMFILE || errno == ENFILE) {
                    // If we hit file handle limits, run GC collection to try to clean up any lingering file handles
                    // that will be closed by GC finalizers.
                    GC_gcollect();
                    data->fd = open(data->path, data->mode, data->permissions);
                }
                if (data->fd == -1)
                    return FailureResult("Could not write to file: ", data->path, " (", strerror(errno), ")");
            }
        }

        if (bytes.stride != 1) List$compact(&bytes, 1);
        ssize_t written = write(data->fd, bytes.data, (size_t)bytes.length);
        if (written != (ssize_t)bytes.length)
            return FailureResult("Could not write to file: ", data->path, " (", strerror(errno), ")");
    }
    // After first successful write, all writes are appends
    data->mode = (O_WRONLY | O_CREAT | O_APPEND);

    if (close_file && data->fd != -1) {
        if (close(data->fd) == -1)
            return FailureResult("Failed to close file: ", data->path, " (", strerror(errno), ")");
        data->fd = -1;
    }
    return SuccessResult;
}

static Result_t _write_text_to_fd(Text_t text, bool close_file, void *userdata) {
    return _write_bytes_to_fd(Text$utf8(text), close_file, userdata);
}

static void _writer_cleanup(writer_data_t *data) {
    if (data && data->fd != -1) {
        close(data->fd);
        data->fd = -1;
    }
}

public
Closure_t Path$byte_writer(Path_t path, bool append, int permissions) {
    path = Path$expand_home(path);
    int mode = append ? (O_WRONLY | O_CREAT | O_APPEND) : (O_WRONLY | O_CREAT | O_TRUNC);
    writer_data_t *userdata = new (writer_data_t, .fd = -1, .path = path, .mode = mode, .permissions = permissions);
    GC_register_finalizer(userdata, (void *)_writer_cleanup, NULL, NULL, NULL);
    return (Closure_t){.fn = _write_bytes_to_fd, .userdata = userdata};
}

public
Closure_t Path$writer(Path_t path, bool append, int permissions) {
    path = Path$expand_home(path);
    int mode = append ? (O_WRONLY | O_CREAT | O_APPEND) : (O_WRONLY | O_CREAT | O_TRUNC);
    writer_data_t *userdata = new (writer_data_t, .fd = -1, .path = path, .mode = mode, .permissions = permissions);
    GC_register_finalizer(userdata, (void *)_writer_cleanup, NULL, NULL, NULL);
    return (Closure_t){.fn = _write_text_to_fd, .userdata = userdata};
}

public
OptionalList_t Path$read_bytes(Path_t path, OptionalInt_t count) {
    path = Path$expand_home(path);
    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        if (errno == EMFILE || errno == ENFILE) {
            // If we hit file handle limits, run GC collection to try to clean up any lingering file handles that
            // will be closed by GC finalizers.
            GC_gcollect();
            fd = open(path, O_RDONLY);
        }
    }

    if (fd == -1) return NONE_LIST;

    struct stat sb;
    if (fstat(fd, &sb) != 0) return NONE_LIST;

    int64_t const target_count = count.small ? Int64$from_int(count, false) : INT64_MAX;
    if (target_count < 0) fail("Cannot read a negative number of bytes!");

    if ((sb.st_mode & S_IFMT) == S_IFREG) { // Use memory mapping if it's a real file:
        const char *mem = mmap(NULL, (size_t)sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        char *content = GC_MALLOC_ATOMIC((size_t)sb.st_size + 1);
        memcpy(content, mem, (size_t)sb.st_size);
        content[sb.st_size] = '\0';
        close(fd);
        if (count.small && (int64_t)sb.st_size < target_count) return NONE_LIST;
        int64_t len = count.small ? target_count : (int64_t)sb.st_size;
        return (List_t){.data = content, .atomic = 1, .stride = 1, .length = (uint64_t)len};
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
                return NONE_LIST;
            } else if (just_read == 0) {
                if (errno == EAGAIN || errno == EINTR) continue;
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
        if (count.small != 0 && (int64_t)len < target_count) return NONE_LIST;
        return (List_t){.data = content, .atomic = 1, .stride = 1, .length = (uint64_t)len};
    }
}

public
OptionalText_t Path$read(Path_t path) {
    List_t bytes = Path$read_bytes(path, NONE_INT);
    if (bytes.data == NULL) return NONE_TEXT;
    return Text$from_utf8(bytes);
}

public
OptionalText_t Path$owner(Path_t path, bool follow_symlinks) {
    struct stat sb;
    int status = path_stat(path, follow_symlinks, &sb);
    if (status != 0) return NONE_TEXT;
    struct passwd *pw = getpwuid(sb.st_uid);
    return pw ? Text$from_str(pw->pw_name) : NONE_TEXT;
}

public
OptionalText_t Path$group(Path_t path, bool follow_symlinks) {
    struct stat sb;
    int status = path_stat(path, follow_symlinks, &sb);
    if (status != 0) return NONE_TEXT;
    struct group *gr = getgrgid(sb.st_uid);
    return gr ? Text$from_str(gr->gr_name) : NONE_TEXT;
}

public
Result_t Path$set_owner(Path_t path, OptionalText_t owner, OptionalText_t group, bool follow_symlinks) {
    // (uid_t)-1 tells chown(2) to leave that half alone, which is what a none
    // owner or group means here.
    uid_t owner_id = (uid_t)-1;
    if (owner.tag != TEXT_NONE) {
        struct passwd *pwd = getpwnam(Text$as_c_string(owner));
        if (pwd == NULL) return FailureResult("Not a valid user: ", owner);
        owner_id = pwd->pw_uid;
    }

    gid_t group_id = (gid_t)-1;
    if (group.tag != TEXT_NONE) {
        struct group *grp = getgrnam(Text$as_c_string(group));
        if (grp == NULL) return FailureResult("Not a valid group: ", group);
        group_id = grp->gr_gid;
    }
    path = Path$expand_home(path);
    int result = follow_symlinks ? chown(path, owner_id, group_id) : lchown(path, owner_id, group_id);
    if (result < 0) return FailureResult("Could not set owner!");
    return SuccessResult;
}

static int _remove_files(const char *path, const struct stat *sbuf, int type, struct FTW *ftwb) {
    (void)sbuf, (void)ftwb;
    switch (type) {
    case FTW_F:
    case FTW_SL:
    case FTW_SLN:
        if (remove(path) < 0) {
            fail("Could not remove file: ", path, " (", strerror(errno), ")");
            return -1;
        }
        return 0;
    case FTW_DP:
        if (rmdir(path) != 0) fail("Could not remove directory: ", path, " (", strerror(errno), ")");
        return 0;
    default: fail("Could not remove path: ", path, " (not a file or directory)"); return -1;
    }
}

public
Result_t Path$remove(Path_t path, bool ignore_missing) {
    path = Path$expand_home(path);
    struct stat sb;
    if (lstat(path, &sb) != 0) {
        if (!ignore_missing) return FailureResult("Could not remove file: ", path, " (", strerror(errno), ")");
        return SuccessResult;
    }

    if ((sb.st_mode & S_IFMT) == S_IFREG || (sb.st_mode & S_IFMT) == S_IFLNK) {
        if (unlink(path) != 0 && !ignore_missing)
            return FailureResult("Could not remove file: ", path, " (", strerror(errno), ")");
    } else if ((sb.st_mode & S_IFMT) == S_IFDIR) {
        const int num_open_fd = 10;
        if (nftw(path, _remove_files, num_open_fd, FTW_DEPTH | FTW_MOUNT | FTW_PHYS) < 0)
            return FailureResult("Could not remove directory: ", path, " (", strerror(errno), ")");
    } else {
        return FailureResult("Could not remove path: ", path, " (not a file or directory)");
    }
    return SuccessResult;
}

Result_t Path$move(Path_t src, Path_t dest, bool allow_overwriting) {
    src = Path$expand_home(src);
    dest = Path$expand_home(dest);
    // rename(2) replaces an existing destination without complaint, so
    // refusing to overwrite has to be decided before the call. Waiting for the
    // EEXIST below is not enough: Linux does not return it for this.
    struct stat sb;
    if (!allow_overwriting && lstat(dest, &sb) == 0)
        return FailureResult("Could not move file ", src, " to ", dest, " (the destination already exists)");
    int status = rename(src, dest);
    if (status != 0) {
        if (errno == EEXIST && allow_overwriting) {
            Result_t result = Path$remove(dest, true);
            if (result.Failure.reason.tag != TEXT_NONE) return result;
            return Path$move(src, dest, allow_overwriting);
        }
        return FailureResult("Could not move file ", src, " to ", dest, " (", strerror(errno), ")");
    }
    return SuccessResult;
}

Result_t Path$copy_to(Path_t src, Path_t dest, bool allow_overwriting) {
    // There is no shell in between to expand a "~" for us:
    src = Path$expand_home(src);
    dest = Path$expand_home(dest);
    pid_t child = fork();
    if (child == 0) {
        const char *args[] = {"cp", allow_overwriting ? "-rf" : "-r", "-T", src, dest, NULL};
        execvp("cp", (char **)args);
        exit(0);
    }
    int status;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
        if (WIFEXITED(status) || WIFSIGNALED(status)) break;
        else if (WIFSTOPPED(status)) kill(child, SIGCONT);
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return FailureResult("Failed to copy ", src, " to ", dest);
    }
    return SuccessResult;
}

public
Result_t Path$create_directory(Path_t path, int permissions, bool recursive) {
retry:
    path = Path$expand_home(path);
    const char *c_path = Path$as_c_string(path);
    int status = mkdir(c_path, (mode_t)permissions);
    if (status != 0) {
        if (recursive && errno == ENOENT) {
            Path$create_directory(Path$parent(path), permissions, recursive);
            goto retry;
        } else if (errno != EEXIST) {
            return FailureResult("Could not create directory: ", c_path, " (", strerror(errno), ")");
        }
    }
    return SuccessResult;
}

static OptionalList_t _filtered_children(Path_t path, bool include_hidden, mode_t filter) {
    List_t children = EMPTY_LIST;
    size_t path_len = strlen(path);
    DIR *d = opendir(Path$expand_home(path));
    if (!d) return NONE_LIST;

    if (path_len > 0 && path[path_len - 1] == '/') --path_len;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (!include_hidden && ent->d_name[0] == '.') continue;
        if (streq(ent->d_name, ".") || streq(ent->d_name, "..")) continue;

        Path_t child = Path$from_str(String(string_slice(path, path_len), "/", ent->d_name));
        struct stat sb;
        if (stat(Path$expand_home(child), &sb) != 0) continue;
        if (!((sb.st_mode & S_IFMT) & filter)) continue;

        List$insert(&children, &child, I(0), sizeof(Path_t));
    }
    closedir(d);
    return children;
}

public
OptionalList_t Path$children(Path_t path, bool include_hidden) {
    return _filtered_children(path, include_hidden, (mode_t)-1);
}

public
OptionalList_t Path$files(Path_t path, bool include_hidden) {
    return _filtered_children(path, include_hidden, S_IFREG);
}

public
OptionalList_t Path$subdirectories(Path_t path, bool include_hidden) {
    return _filtered_children(path, include_hidden, S_IFDIR);
}

typedef struct {
    Path_t path;
    DIR *dir;
    bool include_hidden : 1;
} child_info_t;

static OptionalPath_t _next_child(child_info_t *info) {
    if (!info->dir) return NONE_PATH;
    for (struct dirent *ent; (ent = readdir(info->dir)) != NULL;) {
        if (!info->include_hidden && ent->d_name[0] == '.') continue;
        if (streq(ent->d_name, ".") || streq(ent->d_name, "..")) continue;

        Path_t child = Path$_concat2(info->path, ent->d_name);
        return child;
    }
    closedir(info->dir);
    info->dir = NULL;
    return NONE_PATH;
}

public
OptionalClosure_t Path$each_child(Path_t path, bool include_hidden) {
    DIR *d = opendir(Path$expand_home(path));
    if (!d) return NONE_CLOSURE;

    child_info_t *info = GC_malloc(sizeof(child_info_t));
    info->path = path;
    info->dir = d;
    info->include_hidden = include_hidden;
    return (Closure_t){.fn = (void *)_next_child, .userdata = info};
}

public
OptionalPath_t Path$unique_directory(Path_t path) {
    bool home_based = (path_type(path) == PATH_HOME);
    path = Path$expand_home(path);
    size_t len = strlen(path);
    if (len >= PATH_MAX) fail("Path is too long: ", path);
    static char buf[PATH_MAX] = {};
    memcpy(buf, path, len);
    buf[len] = '\0';
    if (buf[len - 1] == '/') buf[--len] = '\0';
    char *created = mkdtemp(buf);
    if (!created) return NULL;
    // Copy out of `buf`: Path$from_str() does not, and the next call to this
    // function would otherwise rewrite the path this one just returned.
    Path_t path_created = Path$from_str(GC_strdup(created));
    return home_based ? unexpand_home(path_created) : path_created;
}

public
OptionalPath_t Path$write_unique_bytes(Path_t path, List_t bytes) {
    bool home_based = (path_type(path) == PATH_HOME);
    path = Path$expand_home(path);
    size_t len = strlen(path);
    if (len >= PATH_MAX) fail("Path is too long: ", path);
    static char buf[PATH_MAX] = {};
    memcpy(buf, path, len);
    buf[len] = '\0';

    // Count the number of trailing characters leading up to the last "X"
    // (e.g. "foo_XXXXXX.tmp" would yield suffixlen = 4)
    size_t suffixlen = 0;
    while (suffixlen < len && buf[len - 1 - suffixlen] != 'X')
        ++suffixlen;

    int fd = mkstemps(buf, suffixlen);
    if (fd == -1) return NULL;

    if (bytes.stride != 1) List$compact(&bytes, 1);

    ssize_t written = write(fd, bytes.data, (size_t)bytes.length);
    if (written != (ssize_t)bytes.length) fail("Could not write to file: ", buf, " (", strerror(errno), ")");
    close(fd);
    Path_t unique = Path$from_str(GC_strdup(buf)); // Copy out of the static buffer
    return home_based ? unexpand_home(unique) : unique;
}

public
OptionalPath_t Path$write_unique(Path_t path, Text_t text) {
    return Path$write_unique_bytes(path, Text$utf8(text));
}

public
OptionalPath_t Path$parent(Path_t path) {
    if (!path || path[0] == '\0' || strspn(path, "/") == strlen(path)) {
        // root dir has no parent
        return NULL;
    }
    if (streq(path, ".")) return PARENT_PATH;
    return path_from_string(String(path, "/.."));
}

static const char *base_name_start(Path_t path) {
    if (!path || path[0] == '\0') return "";

    const char *end = path + strlen(path);
    // Strip trailing slash
    while (end > path && end[0] == '/')
        end -= 1;

    // Get component up to end, excluding trailing slash
    while (end > path && end[-1] != '/')
        end -= 1;

    return end;
}

public
PUREFUNC OptionalText_t Path$base_name(Path_t path) {
    // A POSIX filename is any byte sequence, so this is none when the name is
    // not valid UTF-8. Text$from_strn() already reports that; it used to be
    // discarded by a non-optional return type.
    const char *base = base_name_start(path);
    return Text$from_strn(base, strcspn(base, "/"));
}

public
OptionalText_t Path$extension(Path_t path, bool full) {
    const char *base = base_name_start(path);
    if (!base || base[0] == '\0') return NONE_TEXT;
    if (base[0] == '.') base += 1;
    // Nothing after the leading ".", as in (.) or (..): no extension. Without
    // this, the searches below would start one byte past the terminator.
    if (base[0] == '\0') return NONE_TEXT;
    const char *dot = full ? strchr(base + 1, '.') : strrchr(base + 1, '.');
    if (!dot) return NONE_TEXT; // No "." in the name at all
    const char *extension = dot + 1;
    size_t len = strcspn(extension, "/");
    if (len == 0) return NONE_TEXT; // A trailing "." is not an extension
    return Text$from_strn(extension, len);
}

public
bool Path$has_extension(Path_t path, Text_t extension) {
    const char *base = base_name_start(path);
    // A path with no base name, such as the root, has no extension, so it
    // answers yes only to the "does it lack one?" question:
    if (!base || base[0] == '\0') return extension.length == 0;
    if (base[0] == '.') base += 1;
    const char *end = base;
    while (*end && *end != '/')
        end += 1;
    int64_t base_len = (int64_t)(end - base);
    // Nothing after the leading ".", as in (.) itself: no extension.
    if (base_len <= 0) return extension.length == 0;
    if (extension.length == 0) {
        const char *dot = strrchr(base, '.');
        return dot == NULL || dot[1] == '\0' || dot == base;
    }
    const char *ext = Text$as_c_string(extension);
    if (ext[0] == '.') {
        if (1 + (int64_t)extension.length > base_len) return false;
        return strncmp(base + base_len - extension.length, ext, extension.length) == 0;
    } else {
        if (1 + 1 + (int64_t)extension.length > base_len) return false;
        return base[base_len - 1 - extension.length] == '.'
               && strncmp(base + base_len - extension.length, ext, extension.length) == 0;
    }
}

public
OptionalList_t Path$components(Path_t path) {
    char buf[PATH_MAX + 1] = {};
    size_t len = MIN(strlen(path), PATH_MAX);
    memcpy(buf, path, len);
    buf[len] = '\0';
    List_t components = EMPTY_LIST;
    if (path[0] == '/') {
        Text_t root = Text("/");
        List$insert(&components, &root, I(0), sizeof(root));
    }
    for (char *comp = buf, *next = buf; (comp = strsep(&next, "/"));) {
        if (comp[0] != '\0') {
            OptionalText_t comp_text = Text$from_str(comp);
            // One undecodable component makes the whole split meaningless,
            // rather than leaving a silent "" in the middle of the list:
            if (comp_text.tag == TEXT_NONE) return NONE_LIST;
            List$insert(&components, &comp_text, I(0), sizeof(comp_text));
        }
    }
    return components;
}

public
Path_t Path$child(Path_t path, Text_t name) {
    return path_from_string(String(path, "/", Text$as_c_string(name)));
}

public
Path_t Path$sibling(Path_t path, Text_t name) {
    return path_from_string(String(path, "/../", Text$as_c_string(name)));
}

public
Path_t Path$with_extension(Path_t path, Text_t extension, bool replace) {
    // Path$from_str() maps "" to "/", so a well-typed Path is never empty and
    // this never fires; the declared type stays non-optional rather than making
    // every caller unwrap a failure that cannot happen.
    if (!path || path[0] == '\0') fail("Cannot set the extension of an empty path");

    const char *ext = Text$as_c_string(extension);
    const char *dot_or_empty = (ext[0] == '.' || ext[0] == '\0') ? "" : ".";
    if (replace) {
        const char *base = base_name_start(path);
        const char *dot = base;
        while (*dot && *dot != '.')
            dot += 1;
        return path_from_string(String(string_slice(path, (size_t)(dot - path)), dot_or_empty, ext));
    } else {
        return path_from_string(String(path, dot_or_empty, ext));
    }
}

static void _line_reader_cleanup(FILE **f) {
    if (f && *f) {
        fclose(*f);
        *f = NULL;
    }
}

static Text_t _next_line(FILE **f) {
    if (!f || !*f) return NONE_TEXT;

    char *line = NULL;
    size_t size = 0;
next_line:;
    ssize_t len = getline(&line, &size, *f);
    if (len <= 0) {
        if (line != NULL) free(line);
        _line_reader_cleanup(f);
        return NONE_TEXT;
    }

    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n'))
        --len;

    if (u8_check((uint8_t *)line, (size_t)len) != NULL) {
        // If there's invalid UTF8, skip this line and move to the next
        goto next_line;
    }

    Text_t line_text = Text$from_strn(line, (size_t)len);
    free(line);
    return line_text;
}

public
OptionalClosure_t Path$by_line(Path_t path) {
    path = Path$expand_home(path);

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        if (errno == EMFILE || errno == ENFILE) {
            // If we hit file handle limits, run GC collection to try to clean up any lingering file handles that
            // will be closed by GC finalizers.
            GC_gcollect();
            f = fopen(path, "r");
        }
    }

    if (f == NULL) return NONE_CLOSURE;

    FILE **wrapper = GC_MALLOC(sizeof(FILE *));
    *wrapper = f;
    GC_register_finalizer(wrapper, (void *)_line_reader_cleanup, NULL, NULL, NULL);
    return (Closure_t){.fn = (void *)_next_line, .userdata = wrapper};
}

public
OptionalList_t Path$lines(Path_t path) {
    path = Path$expand_home(path);
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        if (errno == EMFILE || errno == ENFILE) {
            // If we hit file handle limits, run GC collection to try to clean up any lingering file handles that
            // will be closed by GC finalizers.
            GC_gcollect();
            f = fopen(path, "r");
        }
    }

    if (f == NULL) return NONE_LIST;

    List_t lines = EMPTY_LIST;
    for (OptionalText_t line; (line = _next_line(&f)).tag != TEXT_NONE;) {
        List$insert(&lines, &line, I(0), sizeof(line));
    }
    return lines;
}

// Split a path or a glob pattern into its "/"-separated pieces. Nothing here
// decodes: a filename that is not valid UTF-8 still has to be matchable, for
// the same reason Path$has_extension works on bytes.
//
// A leading "/" becomes a piece of its own. That is what anchors absolute
// patterns: no other piece can ever equal "/", so a pattern beginning with one
// can only line up at the front of the path.
static size_t split_components(const char *str, string_slice_t **out) {
    size_t count = (str[0] == '/') ? 1 : 0;
    for (const char *p = str; *p;) {
        p += strspn(p, "/"); // Skip separators, including a run of them
        if (!*p) break;
        count += 1;
        p += strcspn(p, "/");
    }

    string_slice_t *comps = GC_MALLOC(count * sizeof(string_slice_t));
    size_t i = 0;
    if (str[0] == '/') comps[i++] = string_slice("/", 1);
    for (const char *p = str; *p && i < count;) {
        p += strspn(p, "/");
        if (!*p) break;
        size_t len = strcspn(p, "/");
        comps[i++] = string_slice(p, len);
        p += len;
    }
    *out = comps;
    return count;
}

static bool component_matches(string_slice_t path, string_slice_t glob) {
    // fnmatch() needs NUL-terminated strings, and a slice is not one:
    return fnmatch(String(glob), String(path), FNM_PATHNAME | FNM_PERIOD) == 0;
}

static bool is_double_star(string_slice_t comp) {
    return comp.length == 2 && comp.str[0] == '*' && comp.str[1] == '*';
}

// The pattern has to consume every component it is given. "**" stands for zero
// or more components, so it tries each split point.
static bool match_components(string_slice_t *path, size_t n_path, string_slice_t *glob, size_t n_glob) {
    if (n_glob == 0) return n_path == 0;
    if (is_double_star(glob[0])) {
        for (size_t skip = 0; skip <= n_path; skip++)
            if (match_components(path + skip, n_path - skip, glob + 1, n_glob - 1)) return true;
        return false;
    }
    if (n_path == 0) return false;
    if (!component_matches(path[0], glob[0])) return false;
    return match_components(path + 1, n_path - 1, glob + 1, n_glob - 1);
}

// Path$matches_glob() lets a pattern match any trailing run of components. A
// glob is anchored instead: the pattern is relative to the directory being
// globbed, so it has to account for every component below it.
static bool matches_relative_glob(const char *path, const char *pattern) {
    string_slice_t *path_comps, *glob_comps;
    size_t n_path = split_components(path, &path_comps);
    size_t n_glob = split_components(pattern, &glob_comps);
    if (n_glob == 0) return false;
    return match_components(path_comps, n_path, glob_comps, n_glob);
}

static bool pattern_has_double_star(const char *pattern) {
    string_slice_t *comps;
    size_t n = split_components(pattern, &comps);
    for (size_t i = 0; i < n; i++)
        if (is_double_star(comps[i])) return true;
    return false;
}

// Backslash-escape every glob(3) metacharacter in `str`, so that the bytes are
// matched literally. glob(3) honours these escapes unless GLOB_NOESCAPE is set.
static const char *glob_escaped(const char *str, size_t len) {
    char *escaped = GC_MALLOC_ATOMIC(2 * len + 1); // Worst case: every byte escaped
    char *dest = escaped;
    for (const char *src = str, *end = str + len; src < end; src++) {
        if (*src == '*' || *src == '?' || *src == '[' || *src == ']' || *src == '\\') *(dest++) = '\\';
        *(dest++) = *src;
    }
    *dest = '\0';
    return escaped;
}

// glob(3) has no way to say "any number of directories", so a pattern with a
// "**" in it is answered by walking the tree and testing each path instead.
// Every entry is visited, hidden ones included, and the matcher decides: "**"
// spans any component, while "*" still will not match a leading ".".
static void glob_walk(Path_t dir, const char *relative, const char *pattern, List_t *out) {
    DIR *d = opendir(Path$expand_home(dir));
    if (!d) return;

    size_t dir_len = strlen(dir);
    if (dir_len > 0 && dir[dir_len - 1] == '/') --dir_len;

    for (struct dirent *ent; (ent = readdir(d)) != NULL;) {
        if (streq(ent->d_name, ".") || streq(ent->d_name, "..")) continue;

        Path_t child = Path$from_str(String(string_slice(dir, dir_len), "/", ent->d_name));
        const char *child_relative = relative[0] ? String(relative, "/", ent->d_name) : ent->d_name;
        if (matches_relative_glob(child_relative, pattern)) List$insert(out, &child, I(0), sizeof(Path_t));
        if (Path$is_directory(child, false)) glob_walk(child, child_relative, pattern, out);
    }
    closedir(d);
}

public
OptionalList_t Path$glob(Path_t path, Text_t pattern) {
    // The directory being globbed has to be readable. Anything else is a
    // failure rather than an empty result, the same as Path$children.
    DIR *base = opendir(Path$expand_home(path));
    if (!base) return NONE_LIST;
    closedir(base);

    const char *pat = Text$as_c_string(pattern);
    if (pat[0] == '\0') return EMPTY_LIST; // An empty pattern matches nothing

    List_t glob_files = EMPTY_LIST;
    if (pattern_has_double_star(pat)) {
        glob_walk(path, "", pat, &glob_files);
        Closure_t comparison = {.fn = (void *)CString$compare, .userdata = (void *)&Path$info};
        List$sort(&glob_files, comparison, sizeof(Path_t));
        return glob_files;
    }

    // No "**", so let glob(3) prune as it descends instead of walking it all.
    bool home_based = (path_type(path) == PATH_HOME);
    Path_t expanded = Path$expand_home(path);
    size_t dir_len = strlen(expanded);
    if (dir_len > 0 && expanded[dir_len - 1] == '/') --dir_len;
    // Only `pattern` is a pattern: the directory is a literal path, so a
    // directory named "foo[1]" has to be escaped or glob(3) would read the
    // "[1]" as a character class and match nothing inside it.
    const char *joined = String(glob_escaped(expanded, dir_len), "/", pat);

    glob_t glob_result;
    int status = glob(joined, 0, NULL, &glob_result);
    if (status != 0 && status != GLOB_NOMATCH) fail("Failed to perform globbing: ", joined);

    for (size_t i = 0; i < glob_result.gl_pathc; i++) {
        size_t len = strlen(glob_result.gl_pathv[i]);
        if ((len >= 2 && glob_result.gl_pathv[i][len - 1] == '.' && glob_result.gl_pathv[i][len - 2] == '/')
            || (len >= 2 && glob_result.gl_pathv[i][len - 1] == '.' && glob_result.gl_pathv[i][len - 2] == '.'
                && glob_result.gl_pathv[i][len - 3] == '/'))
            continue;
        // Copy: Path$from_str() does not, and globfree() frees gl_pathv.
        Path_t p = Path$from_str(GC_strdup(glob_result.gl_pathv[i]));
        if (home_based) p = unexpand_home(p);
        List$insert(&glob_files, &p, I(0), sizeof(Path_t));
    }
    globfree(&glob_result);
    // glob(3) sorts with strcoll() and the walk above does not sort at all, so
    // both are put in the same order here rather than the answer depending on
    // which branch produced it.
    Closure_t comparison = {.fn = (void *)CString$compare, .userdata = (void *)&Path$info};
    List$sort(&glob_files, comparison, sizeof(Path_t));
    return glob_files;
}

public
bool Path$matches_glob(Path_t path, Text_t glob) {
    string_slice_t *path_comps, *glob_comps;
    size_t n_path = split_components(path, &path_comps);
    size_t n_glob = split_components(Text$as_c_string(glob), &glob_comps);

    if (n_glob == 0) return false; // An empty pattern matches nothing

    // A pattern matches any trailing run of components, so "*.txt" asks about
    // the file's name wherever it sits, while "/tmp/*.txt" pins the whole path.
    for (size_t start = 0; start <= n_path; start++)
        if (match_components(path_comps + start, n_path - start, glob_comps, n_glob)) return true;
    return false;
}

public
Path_t Path$current_dir(void) {
    static char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) fail("Could not get current working directory");
    return Path$from_str(GC_strdup(cwd)); // Copy out of the static buffer
}

typedef struct {
    List_t dir_stack;
    OptionalPath_t current;
    DIR *dir;
    bool include_hidden : 1, follow_symlinks : 1;
} walk_info_t;

static OptionalPath_t _walk_next_path(walk_info_t *info) {
    while (info->dir == NULL) {
        if (info->dir_stack.length == 0) return NONE_PATH;

        Path_t p = *(Path_t *)info->dir_stack.data;
        List$remove_at(&info->dir_stack, I(1), I(1), sizeof(Path_t));
        info->dir = opendir(Path$expand_home(p));
        info->current = p;
        return p;
    }

    for (struct dirent *ent; (ent = readdir(info->dir)) != NULL;) {
        if (!info->include_hidden && ent->d_name[0] == '.') continue;
        if (streq(ent->d_name, ".") || streq(ent->d_name, "..")) continue;

        Path_t path = Path$_concat2(info->current, Path$from_str(ent->d_name));
        if (Path$is_directory(path, info->follow_symlinks)) {
            List$insert(&info->dir_stack, &path, I(0), sizeof(Path_t));
            continue;
        }
        return path;
    }

    closedir(info->dir);
    info->dir = NULL;
    return _walk_next_path(info);
}

public
Closure_t Path$walk(Path_t dir, bool include_hidden, bool follow_symlinks) {
    walk_info_t *info = GC_malloc(sizeof(walk_info_t));
    info->dir_stack = List(dir);
    info->current = dir;
    info->dir = NULL;
    info->include_hidden = include_hidden;
    info->follow_symlinks = follow_symlinks;
    return (Closure_t){.fn = (void *)_walk_next_path, .userdata = info};
}

public
CONSTFUNC
const char *Path$as_c_string(Path_t path) {
    return path;
}

public
List_t Path$bytes(Path_t path) {
    return CString$bytes(path);
}

public
Text_t Path$as_text(const void *obj, bool color, const TypeInfo_t *type) {
    (void)type;
    if (!obj) return Text("Path");
    Path_t *path = (Path_t *)obj;
    Text_t text = Text$from_str(*path);
    if (color) text = Text$concat(Text("\033[92;1m"), text, Text("\033[m"));
    return text;
}

public
const TypeInfo_t Path$info = {
    .size = sizeof(Path_t),
    .align = __alignof__(Path_t),
    .tag = OpaqueInfo,
    .metamethods =
        {
            .as_text = Path$as_text,
            .compare = CString$compare,
            .equal = CString$equal,
            .hash = CString$hash,
            .is_none = CString$is_none,
            .serialize = CString$serialize,
            .deserialize = CString$deserialize,
        },
};
