#pragma once

// A lang for Shell Command Language

#include <gc/cord.h>
#include <stdbool.h>
#include <stdint.h>

#include "types.h"
#include "datatypes.h"

#define Shell_t Text_t
#define Shell(text) ((Shell_t)Text(text))
#define Shells(...) ((Shell_t)Texts(__VA_ARGS__))

Text_t Shell$run(Shell_t command, int32_t *status);
Shell_t Shell$escape_text(Text_t text);

#define Shell$hash Text$hash
#define Shell$compare Text$compare
#define Shell$equal Text$equal

extern const TypeInfo Shell$info;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0

