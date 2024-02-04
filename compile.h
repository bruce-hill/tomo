#pragma once

#include <gc/cord.h>
#include <gc.h>
#include <stdio.h>

#include "util.h"

CORD compile(ast_t *ast);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
