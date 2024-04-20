#pragma once

// All of the different builtin modules can be included by including this one
// import

#include <err.h>
#include <gc.h>
#include <gc/cord.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "array.h"
#include "bool.h"
#include "datatypes.h"
#include "functions.h"
#include "halfsiphash.h"
#include "integers.h"
#include "macros.h"
#include "memory.h"
#include "nums.h"
#include "pointer.h"
#include "table.h"
#include "text.h"
#include "types.h"

extern bool USE_COLOR;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
