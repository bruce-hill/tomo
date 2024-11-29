#pragma once

// Functions that operate on channels (thread-safe arrays)

#include <stdbool.h>

#include "datatypes.h"
#include "types.h"
#include "util.h"

Channel_t *Channel$new(Int_t max_size);
void Channel$give(Channel_t *channel, const void *item, bool front, int64_t padded_item_size);
#define Channel$give_value(channel, item, front, padded_item_size) \
    ({ __typeof(item) _item = item; Channel$give(channel, &_item, front, padded_item_size); })
void Channel$give_all(Channel_t *channel, Array_t to_give, bool front, int64_t padded_item_size);
void Channel$get(Channel_t *channel, void *out, bool front, int64_t item_size, int64_t padded_item_size);
#define Channel$get_value(channel, front, t, padded_item_size) \
    ({ t _val; Channel$get(channel, &_val, front, sizeof(t), padded_item_size); _val; })
void Channel$peek(Channel_t *channel, void *out, bool front, int64_t item_size);
#define Channel$peek_value(channel, front, t) ({ t _val; Channel$peek(channel, &_val, front, sizeof(t)); _val; })
void Channel$clear(Channel_t *channel);
Array_t Channel$view(Channel_t *channel);
PUREFUNC uint64_t Channel$hash(const void *channel, const TypeInfo_t *type);
PUREFUNC int32_t Channel$compare(const void *x, const void *y, const TypeInfo_t *type);
PUREFUNC bool Channel$equal(const void *x, const void *y, const TypeInfo_t *type);
Text_t Channel$as_text(const void *channel, bool colorize, const TypeInfo_t *type);
PUREFUNC bool Channel$is_none(const void *obj, const TypeInfo_t*);
void Channel$serialize(const void *obj, FILE *out, Table_t *pointers, const TypeInfo_t*);
void Channel$deserialize(FILE *in, void *outval, Array_t *pointers, const TypeInfo_t*);

#define Channel$metamethods ((metamethods_t){ \
    .as_text=Channel$as_text, \
    .compare=Channel$compare, \
    .equal=Channel$equal, \
    .hash=Channel$hash, \
    .is_none=Channel$is_none, \
    .serialize=Channel$serialize, \
    .deserialize=Channel$deserialize, \
})

#define Channel$info(item_info) &((TypeInfo_t){.size=sizeof(Channel_t), .align=__alignof__(Channel_t), \
                                .tag=ChannelInfo, .ChannelInfo.item=item_info, \
                                .metamethods=Channel$metamethods})

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
