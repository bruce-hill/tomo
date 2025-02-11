#pragma once

// Metamethods and type info for mutexed data

#include "types.h"
#include "optionals.h"
#include "util.h"

#define NONE_MUTEXED_DATA ((MutexedData_t)NULL)

extern const metamethods_t MutexedData$metamethods;

#define MutexedData$info(t) &((TypeInfo_t){.size=sizeof(MutexedData_t), .align=__alignof(MutexedData_t), \
                           .tag=MutexedDataInfo, .MutexedDataInfo.type=t, \
                           .metamethods=MutexedData$metamethods})

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
