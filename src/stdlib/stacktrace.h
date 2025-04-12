#pragma once
#include <stdio.h>

void initialize_stacktrace(const char *program);
void print_stacktrace(FILE *out, int offset);
