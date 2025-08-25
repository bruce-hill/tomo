#pragma once

// This is a workaround fix for an issue on some systems that don't have `__GLIBC__` defined
// and run into problems with <unistr.h>

#ifndef __GLIBC__
#define __GLIBC__ 2
#include <unistr.h> // IWYU pragma: export
#undef __GLIBC__
#else
#include <unistr.h> // IWYU pragma: export
#endif
