// Logic for the Thread type, representing a pthread

#include <ctype.h>
#include <err.h>
#include <gc.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/param.h>

#include "arrays.h"
#include "datatypes.h"
#include "text.h"
#include "threads.h"
#include "types.h"
#include "util.h"

public Thread_t Thread$new(Closure_t fn)
{
    Thread_t thread = new(pthread_t);
    pthread_create(thread, NULL, fn.fn, fn.userdata);
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

Text_t Thread$as_text(const Thread_t *thread, bool colorize, const TypeInfo_t *type)
{
    (void)type;
    if (!thread) {
        return colorize ? Text("\x1b[34;1mThread\x1b[m") : Text("Thread");
    }
    return Text$format(colorize ? "\x1b[34;1mThread(%p)\x1b[m" : "Thread(%p)", *thread);
}

public const TypeInfo_t Thread$info = {
    .size=sizeof(Thread_t), .align=__alignof(Thread_t),
    .tag=CustomInfo,
    .CustomInfo={.as_text=(void*)Thread$as_text},
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
