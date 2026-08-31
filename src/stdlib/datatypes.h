// The representations of the built-in datatypes (lists, tables, text, ...)
//
// This header declares nothing itself: it is an umbrella over datatypes/,
// which holds one header per type, giving that type's memory layout and
// nothing else. Those headers declare no functions, which is what keeps them
// free of the cycle between the datatypes and the runtime type information
// that describes them. The functions that operate on each type live in its
// own header alongside this one: list.h, table.h, text.h, and so on.
//
// datatypes/typeinfo.h is deliberately not included here: TypeInfo_t is the
// description of a type rather than one of the datatypes, and a consumer that
// wants it says so (typeinfo.h for the API, datatypes/typeinfo.h for just the
// struct).

#pragma once

#include "datatypes/bool.h" // IWYU pragma: export
#include "datatypes/byte.h" // IWYU pragma: export
#include "datatypes/closure.h" // IWYU pragma: export
#include "datatypes/float.h" // IWYU pragma: export
#include "datatypes/int.h" // IWYU pragma: export
#include "datatypes/list.h" // IWYU pragma: export
#include "datatypes/num.h" // IWYU pragma: export
#include "datatypes/path.h" // IWYU pragma: export
#include "datatypes/present.h" // IWYU pragma: export
#include "datatypes/result.h" // IWYU pragma: export
#include "datatypes/table.h" // IWYU pragma: export
#include "datatypes/text.h" // IWYU pragma: export
