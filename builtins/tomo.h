#pragma once

// All of the different builtin modules can be included by including this one
// import

#include <gc.h>
#include <gc/cord.h>
#include <gmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/param.h>

#include "array.h"
#include "bool.h"
#include "c_string.h"
#include "channel.h"
#include "datatypes.h"
#include "functions.h"
#include "integers.h"
#include "macros.h"
#include "memory.h"
#include "nums.h"
#include "optionals.h"
#include "path.h"
#include "pointer.h"
#include "range.h"
#include "shell.h"
#include "siphash.h"
#include "table.h"
#include "text.h"
#include "thread.h"
#include "types.h"

// This value will be randomized on startup in tomo_init():
extern uint64_t TOMO_HASH_KEY[2];

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
