#pragma once

// Logic for the Thread type, representing a pthread

#include <pthread.h>
#include <stdbool.h>
#include <gc/cord.h>

#include "datatypes.h"
#include "types.h"
#include "util.h"

pthread_t *Thread$new(closure_t fn);
void Thread$cancel(pthread_t *thread);
void Thread$join(pthread_t *thread);
void Thread$detach(pthread_t *thread);
CORD Thread$as_text(const pthread_t **thread, bool colorize, const TypeInfo *type);

extern TypeInfo Thread;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
