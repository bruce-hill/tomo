// Logic for the Thread type, representing a pthread

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <gc.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/param.h>
#include <sys/random.h>

#include "arrays.h"
#include "datatypes.h"
#include "metamethods.h"
#include "rng.h"
#include "text.h"
#include "threads.h"
#include "types.h"
#include "util.h"

static void *run_thread(Closure_t *closure)
{
    uint8_t *random_bytes = GC_MALLOC_ATOMIC(40);
    getrandom(random_bytes, 40, 0);
    Array_t rng_seed = {.length=40, .data=random_bytes, .stride=1, .atomic=1};
    default_rng = RNG$new(rng_seed);
    ((void(*)(void*))closure->fn)(closure->userdata);
    return NULL;
}

public Thread_t Thread$new(Closure_t fn)
{
    Thread_t thread = GC_MALLOC(sizeof(pthread_t));
    Closure_t *closure = new(Closure_t, .fn=fn.fn, .userdata=fn.userdata);
    pthread_create(thread, NULL, (void*)run_thread, closure);
    return thread;
}

public void Thread$join(Thread_t thread)
{
    pthread_join(*thread, NULL);
}

public void Thread$cancel(Thread_t thread)
{
    pthread_cancel(*thread);
}

public void Thread$detach(Thread_t thread)
{
    pthread_detach(*thread);
}

Text_t Thread$as_text(const void *thread, bool colorize, const TypeInfo_t*)
{
    if (!thread) {
        return colorize ? Text("\x1b[34;1mThread\x1b[m") : Text("Thread");
    }
    return Text$format(colorize ? "\x1b[34;1mThread(%p)\x1b[m" : "Thread(%p)", *(Thread_t**)thread);
}

static bool Thread$is_none(const void *obj, const TypeInfo_t*)
{
    return *(Thread_t*)obj == NULL;
}

public const TypeInfo_t Thread$info = {
    .size=sizeof(Thread_t), .align=__alignof(Thread_t),
    .metamethods={
        .as_text=Thread$as_text,
        .is_none=Thread$is_none,
        .serialize=cannot_serialize,
        .deserialize=cannot_deserialize,
    },
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
