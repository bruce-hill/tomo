// Failure functions

#pragma once

#include "layout/text.h"

_Noreturn void fail_text(Text_t message);
_Noreturn void fail_source(const char *filename, int start, int end, Text_t message);
Text_t builtin_last_err();

#ifndef fail
#define fail(...) fail_text(Texts(__VA_ARGS__))
#endif
