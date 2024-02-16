#pragma once

#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

#include "string.h"

void builtin_say(CORD str, CORD end);
void builtin_fail(CORD fmt, ...);
CORD builtin_last_err();
void builtin_doctest(const char *label, CORD expr, const char *type, bool use_color, const char *expected, const char *filename, int start, int end);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
