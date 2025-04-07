#pragma once

#include <errno.h>
#include <gc.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unistr.h>

#define READ_END 0
#define WRITE_END 1
int run_command(Text_t exe, List_t arg_list, Table_t env_table,
                       OptionalList_t input_bytes, List_t *output_bytes, List_t *error_bytes)
{
    pthread_testcancel();

    struct sigaction sa = { .sa_handler = SIG_IGN }, oldint, oldquit;
    sigaction(SIGINT, &sa, &oldint);
    sigaction(SIGQUIT, &sa, &oldquit);
    sigaddset(&sa.sa_mask, SIGCHLD);
    sigset_t old, reset;
    sigprocmask(SIG_BLOCK, &sa.sa_mask, &old);
    sigemptyset(&reset);
    if (oldint.sa_handler != SIG_IGN) sigaddset(&reset, SIGINT);
    if (oldquit.sa_handler != SIG_IGN) sigaddset(&reset, SIGQUIT);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    posix_spawnattr_setsigmask(&attr, &old);
    posix_spawnattr_setsigdefault(&attr, &reset);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF|POSIX_SPAWN_SETSIGMASK);

    int child_inpipe[2], child_outpipe[2], child_errpipe[2];
    if (input_bytes.length >= 0) pipe(child_inpipe);
    if (output_bytes) pipe(child_outpipe);
    if (error_bytes) pipe(child_errpipe);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (input_bytes.length >= 0) {
        posix_spawn_file_actions_adddup2(&actions, child_inpipe[READ_END], STDIN_FILENO);
        posix_spawn_file_actions_addclose(&actions, child_inpipe[WRITE_END]);
    }
    if (output_bytes) {
        posix_spawn_file_actions_adddup2(&actions, child_outpipe[WRITE_END], STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&actions, child_outpipe[READ_END]);
    }
    if (error_bytes) {
        posix_spawn_file_actions_adddup2(&actions, child_errpipe[WRITE_END], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, child_errpipe[READ_END]);
    }

    const char *exe_str = Text$as_c_string(exe);

    List_t arg_strs = {};
    List$insert_value(&arg_strs, exe_str, I(0), sizeof(char*));
    for (int64_t i = 0; i < arg_list.length; i++)
        List$insert_value(&arg_strs, Text$as_c_string(*(Text_t*)(arg_list.data + i*arg_list.stride)), I(0), sizeof(char*));
    List$insert_value(&arg_strs, NULL, I(0), sizeof(char*));
    char **args = arg_strs.data;

    extern char **environ;
    char **env = environ;
    if (env_table.entries.length > 0) {
        List_t env_list = {}; // List of const char*
        for (char **e = environ; *e; e++)
            List$insert(&env_list, e, I(0), sizeof(char*));

        for (int64_t i = 0; i < env_table.entries.length; i++) {
            struct { Text_t key, value; } *entry = env_table.entries.data + env_table.entries.stride*i;
            const char *env_entry = String(entry->key, "=", entry->value);
            List$insert(&env_list, &env_entry, I(0), sizeof(char*));
        }
        List$insert_value(&env_list, NULL, I(0), sizeof(char*));
        assert(env_list.stride == sizeof(char*));
        env = env_list.data;
    }

    pid_t pid;
    int ret = exe_str[0] == '/' ?
        posix_spawn(&pid, exe_str, &actions, &attr, args, env)
        : posix_spawnp(&pid, exe_str, &actions, &attr, args, env);
    if (ret != 0)
        return -1;

    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&actions);

    if (input_bytes.length >= 0) close(child_inpipe[READ_END]);
    if (output_bytes) close(child_outpipe[WRITE_END]);
    if (error_bytes) close(child_errpipe[WRITE_END]);

    struct pollfd pollfds[3] = {};
    if (input_bytes.length >= 0) pollfds[0] = (struct pollfd){.fd=child_inpipe[WRITE_END], .events=POLLOUT};
    if (output_bytes) pollfds[1] = (struct pollfd){.fd=child_outpipe[WRITE_END], .events=POLLIN};
    if (error_bytes) pollfds[2] = (struct pollfd){.fd=child_errpipe[WRITE_END], .events=POLLIN};

    if (input_bytes.length > 0 && input_bytes.stride != 1)
        List$compact(&input_bytes, sizeof(char));
    if (output_bytes)
        *output_bytes = (List_t){.atomic=1, .stride=1, .length=0};
    if (error_bytes)
        *error_bytes = (List_t){.atomic=1, .stride=1, .length=0};

    while (input_bytes.length > 0 || output_bytes || error_bytes) {
        (void)poll(pollfds, sizeof(pollfds)/sizeof(pollfds[0]), -1);  // Wait for data or readiness
        bool did_something = false;
        if (input_bytes.length >= 0 && pollfds[0].revents) {
            if (input_bytes.length > 0) {
                ssize_t written = write(child_inpipe[WRITE_END], input_bytes.data, (size_t)input_bytes.length);
                if (written > 0) {
                    input_bytes.data += written;
                    input_bytes.length -= (int64_t)written;
                    did_something = true;
                } else if (written < 0) {
                    close(child_inpipe[WRITE_END]);
                    pollfds[0].events = 0;
                }
            }
            if (input_bytes.length <= 0) {
                close(child_inpipe[WRITE_END]);
                pollfds[0].events = 0;
            }
        }
        char buf[256];
        if (output_bytes && pollfds[1].revents) {
            ssize_t n = read(child_outpipe[READ_END], buf, sizeof(buf));
            did_something = did_something || (n > 0);
            if (n <= 0) {
                close(child_outpipe[READ_END]);
                pollfds[1].events = 0;
            } else if (n > 0) {
                if (output_bytes->free < n) {
                    output_bytes->data = GC_REALLOC(output_bytes->data, (size_t)(output_bytes->length + n));
                    output_bytes->free = 0;
                }
                memcpy(output_bytes->data + output_bytes->length, buf, (size_t)n);
                output_bytes->length += n;
            }
        }
        if (error_bytes && pollfds[2].revents) {
            ssize_t n = read(child_errpipe[READ_END], buf, sizeof(buf));
            did_something = did_something || (n > 0);
            if (n <= 0) {
                close(child_errpipe[READ_END]);
                pollfds[2].events = 0;
            } else if (n > 0) {
                if (error_bytes->free < n) {
                    error_bytes->data = GC_REALLOC(error_bytes->data, (size_t)(error_bytes->length + n));
                    error_bytes->free = 0;
                }
                memcpy(error_bytes->data + error_bytes->length, buf, (size_t)n);
                error_bytes->length += n;
            }
        }
        if (!did_something) break;
    }

    int status = 0;
    if (ret == 0) {
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            if (WIFEXITED(status) || WIFSIGNALED(status))
                break;
            else if (WIFSTOPPED(status))
                kill(pid, SIGCONT);
        }
    }

    if (input_bytes.length >= 0) close(child_inpipe[WRITE_END]);
    if (output_bytes) close(child_outpipe[READ_END]);
    if (error_bytes) close(child_errpipe[READ_END]);

    sigaction(SIGINT, &oldint, NULL);
    sigaction(SIGQUIT, &oldquit, NULL);
    sigprocmask(SIG_SETMASK, &old, NULL);

    if (ret) errno = ret;
    return status;
}

typedef struct {
    pid_t pid;
    FILE *out;
} child_info_t;

static void _line_reader_cleanup(child_info_t *child)
{
    if (child && child->out) {
        fclose(child->out);
        child->out = NULL;
    }
    if (child->pid) {
        kill(child->pid, SIGTERM);
        child->pid = 0;
    }
}

static Text_t _next_line(child_info_t *child)
{
    if (!child || !child->out) return NONE_TEXT;

    char *line = NULL;
    size_t size = 0;
    ssize_t len = getline(&line, &size, child->out);
    if (len <= 0) {
        _line_reader_cleanup(child);
        return NONE_TEXT;
    }

    while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n'))
        --len;

    if (u8_check((uint8_t*)line, (size_t)len) != NULL)
        fail("Invalid UTF8!");

    Text_t line_text = Text$from_strn(line, len);
    free(line);
    return line_text;
}

OptionalClosure_t command_by_line(Text_t exe, List_t arg_list, Table_t env_table)
{
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);

    int child_outpipe[2];
    pipe(child_outpipe);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, child_outpipe[WRITE_END], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, child_outpipe[READ_END]);

    const char *exe_str = Text$as_c_string(exe);

    List_t arg_strs = {};
    List$insert_value(&arg_strs, exe_str, I(0), sizeof(char*));
    for (int64_t i = 0; i < arg_list.length; i++)
        List$insert_value(&arg_strs, Text$as_c_string(*(Text_t*)(arg_list.data + i*arg_list.stride)), I(0), sizeof(char*));
    List$insert_value(&arg_strs, NULL, I(0), sizeof(char*));
    char **args = arg_strs.data;

    extern char **environ;
    char **env = environ;
    if (env_table.entries.length > 0) {
        List_t env_list = {}; // List of const char*
        for (char **e = environ; *e; e++)
            List$insert(&env_list, e, I(0), sizeof(char*));

        for (int64_t i = 0; i < env_table.entries.length; i++) {
            struct { Text_t key, value; } *entry = env_table.entries.data + env_table.entries.stride*i;
            const char *env_entry = String(entry->key, "=", entry->value);
            List$insert(&env_list, &env_entry, I(0), sizeof(char*));
        }
        List$insert_value(&env_list, NULL, I(0), sizeof(char*));
        assert(env_list.stride == sizeof(char*));
        env = env_list.data;
    }

    pid_t pid;
    int ret = exe_str[0] == '/' ?
        posix_spawn(&pid, exe_str, &actions, &attr, args, env)
        : posix_spawnp(&pid, exe_str, &actions, &attr, args, env);
    if (ret != 0)
        return NONE_CLOSURE;

    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&actions);

    close(child_outpipe[WRITE_END]);

    child_info_t *child_info = GC_MALLOC(sizeof(child_info_t));
    child_info->out = fdopen(child_outpipe[READ_END], "r");
    child_info->pid = pid;
    GC_register_finalizer(child_info, (void*)_line_reader_cleanup, NULL, NULL, NULL);
    return (Closure_t){.fn=(void*)_next_line, .userdata=child_info};
}

#undef READ_END
#undef WRITE_END
