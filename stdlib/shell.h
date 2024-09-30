#pragma once

// A lang for Shell Command Language

#include <stdbool.h>
#include <stdint.h>

#include "arrays.h"
#include "datatypes.h"
#include "optionals.h"
#include "text.h"
#include "types.h"

#define Shell_t Text_t
#define OptionalShell_t Text_t
#define Shell(text) ((Shell_t)Text(text))
#define Shells(...) ((Shell_t)Texts(__VA_ARGS__))

OptionalClosure_t Shell$by_line(Shell_t command);
Shell_t Shell$escape_text(Text_t text);
Shell_t Shell$escape_text_array(Array_t texts);
OptionalArray_t Shell$run_bytes(Shell_t command);
OptionalText_t Shell$run(Shell_t command);

#define Shell$hash Text$hash
#define Shell$compare Text$compare
#define Shell$equal Text$equal

extern const TypeInfo_t Shell$info;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0

