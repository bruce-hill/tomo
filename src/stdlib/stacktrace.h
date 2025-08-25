// This file defines some code to print stack traces.

#pragma once

#include <stdio.h>

__attribute__((noinline)) void print_stacktrace(FILE *out, int offset);
