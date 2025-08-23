#pragma once
#include <stdio.h>

__attribute__((noinline)) void print_stacktrace(FILE *out, int offset);
