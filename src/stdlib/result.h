#pragma once

// Result type for Success/Failure

#include "layout/result.h" // IWYU pragma: export
// FailureResult expands to Texts() at the caller's site:
#include "text.h" // IWYU pragma: export
#include "types.h"

#define Result$Success ((Result$$type){.$tag = Result$tag$Success})
#define SuccessResult Result$Success
#define Result$tagged$Failure(msg) ((Result$$type){.$tag = Result$tag$Failure, .Failure.reason = msg})
#define FailureResult(...) Result$tagged$Failure(Texts(__VA_ARGS__))

extern const TypeInfo_t Result$Success$$info;
extern const TypeInfo_t Result$Failure$$info;
extern const TypeInfo_t Result$$info;
