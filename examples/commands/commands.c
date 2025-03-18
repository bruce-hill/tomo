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

#define READ_END 0
#define WRITE_END 1
public int run_command(Text_t exe, List_t arg_list, Table_t env_table,
                       List_t input_bytes, List_t *output_bytes, List_t *error_bytes)
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
    pipe(child_inpipe);
    pipe(child_outpipe);
    pipe(child_errpipe);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, child_inpipe[READ_END], STDIN_FILENO);
    posix_spawn_file_actions_addclose(&actions, child_inpipe[WRITE_END]);
    posix_spawn_file_actions_adddup2(&actions, child_outpipe[WRITE_END], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, child_outpipe[READ_END]);
    posix_spawn_file_actions_adddup2(&actions, child_errpipe[WRITE_END], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, child_errpipe[READ_END]);

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
            const char *env_entry = heap_strf("%k=%k", &entry->key, &entry->value);
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

    close(child_inpipe[READ_END]);
    close(child_outpipe[WRITE_END]);
    close(child_errpipe[WRITE_END]);

    struct pollfd pollfds[3] = {
        {.fd=child_inpipe[WRITE_END], .events=POLLOUT},
        {.fd=child_outpipe[WRITE_END], .events=POLLIN},
        {.fd=child_errpipe[WRITE_END], .events=POLLIN},
    };

    if (input_bytes.length > 0 && input_bytes.stride != 1)
        List$compact(&input_bytes, sizeof(char));
    if (output_bytes)
        *output_bytes = (List_t){.atomic=1, .stride=1, .length=0};
    if (error_bytes)
        *error_bytes = (List_t){.atomic=1, .stride=1, .length=0};

    for (;;) {
        (void)poll(pollfds, sizeof(pollfds)/sizeof(pollfds[0]), -1);  // Wait for data or readiness
        bool did_something = false;
        if (pollfds[0].revents) {
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
        if (pollfds[1].revents) {
            ssize_t n = read(child_outpipe[READ_END], buf, sizeof(buf));
            did_something = did_something || (n > 0);
            if (n <= 0) {
                close(child_outpipe[READ_END]);
                pollfds[1].events = 0;
            } else if (n > 0 && output_bytes) {
                if (output_bytes->free < n) {
                    output_bytes->data = GC_REALLOC(output_bytes->data, (size_t)(output_bytes->length + n));
                    output_bytes->free = 0;
                }
                memcpy(output_bytes->data + output_bytes->length, buf, (size_t)n);
                output_bytes->length += n;
            }
        }
        if (pollfds[2].revents) {
            ssize_t n = read(child_errpipe[READ_END], buf, sizeof(buf));
            did_something = did_something || (n > 0);
            if (n <= 0) {
                close(child_errpipe[READ_END]);
                pollfds[2].events = 0;
            } else if (n > 0 && error_bytes) {
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

    sigaction(SIGINT, &oldint, NULL);
    sigaction(SIGQUIT, &oldquit, NULL);
    sigprocmask(SIG_SETMASK, &old, NULL);

    if (ret) errno = ret;
    return status;
}

#undef READ_END
#undef WRITE_END
