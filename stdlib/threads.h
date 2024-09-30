#pragma once

// Logic for the Thread type, representing a pthread

#include <pthread.h>
#include <stdbool.h>

#include "datatypes.h"
#include "types.h"
#include "util.h"

#define Thread_t pthread_t*

Thread_t Thread$new(Closure_t fn);
void Thread$cancel(Thread_t thread);
void Thread$join(Thread_t thread);
void Thread$detach(Thread_t thread);
Text_t Thread$as_text(const Thread_t *thread, bool colorize, const TypeInfo_t *type);

extern const TypeInfo_t Thread$info;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
