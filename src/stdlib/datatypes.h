// The representations of the built-in datatypes (lists, tables, text, ...)
//
// This header declares nothing itself: it is an umbrella over layout/, which
// holds one header per type, giving that type's memory layout and nothing
// else. Those headers declare no functions and never mention TypeInfo_t,
// which is what keeps them free of the cycle between the datatypes and the
// runtime type information that describes them (see types.h). The functions
// that operate on each type live in its own header alongside this one:
// lists.h, tables.h, text.h, and so on.

#pragma once

#include "layout/bool.h" // IWYU pragma: export
#include "layout/byte.h" // IWYU pragma: export
#include "layout/closure.h" // IWYU pragma: export
#include "layout/float.h" // IWYU pragma: export
#include "layout/int.h" // IWYU pragma: export
#include "layout/list.h" // IWYU pragma: export
#include "layout/num.h" // IWYU pragma: export
#include "layout/path.h" // IWYU pragma: export
#include "layout/present.h" // IWYU pragma: export
#include "layout/result.h" // IWYU pragma: export
#include "layout/table.h" // IWYU pragma: export
#include "layout/text.h" // IWYU pragma: export
