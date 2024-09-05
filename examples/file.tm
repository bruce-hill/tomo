# A module for interacting with files
extern builtin_last_err:func()->Text

use <fcntl.h>
use <stdio.h>
use <sys/mman.h>
use <sys/stat.h>
use <unistd.h>

enum FileReadResult(Success(text:Text), Failure(reason:Text))

func _wrap_with_finalizer(obj:@Memory, finalizer:func(obj:@Memory))->@Memory:
    return inline C (
        ({
            FILE **wrapper = GC_MALLOC(sizeof(FILE*));
            *wrapper = $obj;
            GC_register_finalizer(wrapper, (void*)$finalizer.fn, wrapper, NULL, NULL);
            wrapper;
        })
    ):@Memory

func _close_file(fp:@Memory):
    inline C {
        if (*(FILE**)$fp)
            fclose(*(FILE**)$fp);
        *(FILE**)$fp = NULL;
    }

func read(path:Text)->FileReadResult:
    inline C {
        int fd = open(Text$as_c_string($path), O_RDONLY);
        if (fd != -1) {
            struct stat sb;
            if (fstat(fd, &sb) == -1) {
                const char *mem = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
                char *gc_mem = GC_MALLOC_ATOMIC(sb.st_size+1);
                memcpy(gc_mem, mem, sb.st_size);
                gc_mem[sb.st_size] = '\0';
                return file$FileReadResult$tagged$Success(Text$from_strn(gc_mem, sb.st_size));
            } else {
                const int chunk_size = 256;
                char *buf = GC_MALLOC_ATOMIC(chunk_size);
                Text_t contents = Text("");
                size_t just_read;
                do {
                    just_read = read(fd, buf, chunk_size);
                    if (just_read > 0) {
                        contents = Texts(contents, Text$from_strn(buf, just_read));
                        buf = GC_MALLOC_ATOMIC(chunk_size);
                    }
                } while (just_read > 0);
                close(fd);
                return file$FileReadResult$tagged$Success(contents);
            }
        }
    }
    return Failure(builtin_last_err())

struct WriteHandle(_file:@Memory):
    func write(h:WriteHandle, text:Text, flush=yes):
        inline C {
            fputs(Text$as_c_string($text), *(FILE**)$h.$_file);
            if ($flush)
                fflush(*(FILE**)$h.$_file);
        }

    func close(h:WriteHandle):
        _close_file(h._file)

enum FileWriteResult(Open(h:WriteHandle), Failure(reason:Text))
func writing(path:Text)->FileWriteResult:
    maybe_f := inline C (
        fopen(Text$as_c_string($path), "w")
    ):@Memory?
    when maybe_f is @f:
        obj := _wrap_with_finalizer(f, _close_file)
        return Open(WriteHandle(obj))
    else:
        return Failure(builtin_last_err())

func appending(path:Text)->FileWriteResult:
    maybe_f := inline C (
        fopen(Text$as_c_string($path), "a")
    ):@Memory?
    when maybe_f is @f:
        return Open(WriteHandle(f))
    else:
        return Failure(builtin_last_err())

struct LineReader(_file:@Memory):
    func stdin()->LineReader:
        f := inline C (
            stdin
        ):@Memory
        return LineReader(_wrap_with_finalizer(f, _close_file))

    func is_finished(r:LineReader)->Bool:
        return inline C (
            feof(*(FILE**)$r.$_file) != 0;
        ):Bool

    func next_line(r:LineReader)->FileReadResult:
        line := inline C (
            ({
                if (*(FILE**)$r.$_file == NULL) fail("File has already been closed!");
                char *buf = NULL;
                size_t space = 0;
                ssize_t len = getline(&buf, &space, *(FILE**)$r.$_file);
                if (len < 0) return (file$FileReadResult_t){1, .$Failure={Text("End of file")}};
                if (len > 0 && buf[len-1] == '\n') --len;
                char *line = GC_MALLOC_ATOMIC(len + 1);
                memcpy(line, buf, len);
                line[len] = '\0';
                if (buf) free(buf);
                Text$from_strn(line, len);
            })
        ):Text
        return Success(line)

    func from_file(path:Text)->FileLineReaderResult:
        maybe_f := inline C (
            fopen(Text$as_c_string($path), "r")
        ):@Memory?
        when maybe_f is @f:
            obj := _wrap_with_finalizer(f, _close_file)
            return Open(LineReader(obj))
        else:
            return Failure(builtin_last_err())

    func from_command(cmd:Text)->FileLineReaderResult:
        maybe_f := inline C (
            popen(Text$as_c_string($cmd), "r")
        ):@Memory?
        when maybe_f is @f:
            obj := _wrap_with_finalizer(f, _close_file)
            return Open(LineReader(obj))
        else:
            return Failure(builtin_last_err())

    func close(r:LineReader):
        _close_file(r._file)

enum FileLineReaderResult(Open(reader:LineReader), Failure(reason:Text))

func command(cmd:Text)->FileReadResult:
    maybe_f := inline C (
        popen(Text$as_c_string($cmd), "r")
    ):@Memory?
        
    when maybe_f is @f:
        text := inline C (
            ({
                const int chunk_size = 256;
                char *buf = GC_MALLOC_ATOMIC(chunk_size);
                Text_t contents = Text("");
                size_t just_read;
                do {
                    just_read = fread(buf, sizeof(char), chunk_size, $f);
                    if (just_read > 0) {
                        contents = Texts(contents, Text$from_strn(buf, just_read));
                        buf = GC_MALLOC_ATOMIC(chunk_size);
                    }
                } while (just_read > 0);
                contents;
            })
        ):Text
        text = text:replace($/$(\n){end}/, "")
        return Success(text)
    else:
        return Failure(builtin_last_err())

func main():
    word := ""
    when command("shuf -n 1 /usr/share/dict/words") is Success(w):
        >> word = w
    is Failure(msg):
        fail(msg)

    # when writing("test.txt") is Open(f):
    #     say("Writing {word} to test.txt")
    #     f:write("Hello {word}!{\n}")
    # is Failure(msg):
    #     fail(msg)

    # when read("test.txt") is Success(text):
    #     say("Roundtrip: {text}")
    # is Failure(msg):
    #     fail(msg)

    # say("Reading stdin:")
    # reader := LineReader.stdin()
    # while yes:
    #     when reader:next_line() is Success(line):
    #         >> line
    #     else: stop

    # say("Reading cmd:")
    # when LineReader.from_command("ping google.com") is Open(reader):
    #     while yes:
    #         when reader:next_line() is Success(line):
    #             >> line
    #         else: stop
    # is Failure(msg):
    #     fail("{msg}")

    # say("Reading /dev/stdin:")
    # when LineReader.from_file("/dev/stdin") is Open(reader):
    #     while yes:
    #         when reader:next_line() is Success(line):
    #             >> line
    #         else: stop
    # is Failure(msg):
    #     fail("{msg}")
