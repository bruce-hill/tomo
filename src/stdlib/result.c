// Result (Success/Failure) type info
#include <err.h>
#include <gc.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/param.h>

#include "enums.h"
#include "structs.h"
#include "text.h"
#include "util.h"

public
const TypeInfo_t Result$Success$$info = {
    .size = sizeof(Result$Success$$type),
    .align = __alignof__(Result$Success$$type),
    .tag = StructInfo,
    .StructInfo =
        {
            .name = "Success",
            .num_fields = 0,
        },
    .metamethods = Struct$metamethods,
};

public
const TypeInfo_t Result$Failure$$info = {
    .size = sizeof(Result$Failure$$type),
    .align = __alignof__(Result$Failure$$type),
    .tag = StructInfo,
    .StructInfo =
        {
            .name = "Failure",
            .num_fields = 1,
            .fields =
                (NamedType_t[1]){
                    {.name = "reason", .type = &Text$info},
                },
        },
    .metamethods = Struct$metamethods,
};

public
const TypeInfo_t Result$$info = {
    .size = sizeof(Result_t),
    .align = __alignof__(Result_t),
    .tag = EnumInfo,
    .EnumInfo =
        {
            .name = "Result",
            .num_tags = 2,
            .tags =
                (NamedType_t[2]){
                    {
                        .name = "Success",
                        .type = &Result$Success$$info,
                    },
                    {
                        .name = "Failure",
                        .type = &Result$Failure$$info,
                    },
                },
        },
    .metamethods = Enum$metamethods,
};
