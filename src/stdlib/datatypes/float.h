// Representation of the Float32/Float64 types

#pragma once

typedef double Float64_t;
typedef float Float32_t;

// `none` is any NaN (Float64$is_none is just isnan), so an optional float is
// the same width as a float. There is no NONE_FLOAT constant to go with the
// others: a none is built by Float64$nan(), which carries a tag in the
// payload for error messages.
typedef double OptionalFloat64_t;
typedef float OptionalFloat32_t;
