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

#include "array.h"
#include "functions.h"
#include "text.h"
#include "types.h"
#include "util.h"

public pthread_t *Thread$new(Closure_t fn)
{
    pthread_t *thread = new(pthread_t);
    pthread_create(thread, NULL, fn.fn, fn.userdata);
    return thread;
}

public void Thread$join(pthread_t *thread)
{
    pthread_join(*thread, NULL);
}

public void Thread$cancel(pthread_t *thread)
{
    pthread_cancel(*thread);
}

public void Thread$detach(pthread_t *thread)
{
    pthread_detach(*thread);
}

Text_t Thread$as_text(const pthread_t **thread, bool colorize, const TypeInfo *type)
{
    (void)type;
    if (!thread) {
        return colorize ? Text("\x1b[34;1mThread\x1b[m") : Text("Thread");
    }
    return Text$format(colorize ? "\x1b[34;1mThread(%p)\x1b[m" : "Thread(%p)", *thread);
}

public const TypeInfo Thread = {
    .size=sizeof(pthread_t*), .align=__alignof(pthread_t*),
    .tag=CustomInfo,
    .CustomInfo={.as_text=(void*)Thread$as_text},
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
