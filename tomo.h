#pragma once
// Compiler interface

#include <stdbool.h>

int transpile(const char *filename, bool force_retranspile);
int compile_object_file(const char *filename, bool force_recompile);
int run_program(const char *filename);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
