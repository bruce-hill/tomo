#pragma once

// Functions that operate on channels (thread-safe arrays)

#include <stdbool.h>
#include <gc/cord.h>

#include "datatypes.h"
#include "types.h"
#include "util.h"
#include "where.h"

channel_t *Channel$new(Int_t max_size);
void Channel$give(channel_t *channel, const void *item, Where_t where, int64_t padded_item_size);
#define Channel$give_value(channel, item, where, padded_item_size) \
    ({ __typeof(item) _item = item; Channel$give(channel, &_item, where, padded_item_size); })
void Channel$give_all(channel_t *channel, array_t to_give, Where_t where, int64_t padded_item_size);
void Channel$get(channel_t *channel, void *out, Where_t where, int64_t item_size, int64_t padded_item_size);
#define Channel$get_value(channel, where, t, padded_item_size) \
    ({ t _val; Channel$get(channel, &_val, where, sizeof(t), padded_item_size); _val; })
void Channel$peek(channel_t *channel, void *out, Where_t where, int64_t item_size);
#define Channel$peek_value(channel, where, t) ({ t _val; Channel$peek(channel, &_val, where, sizeof(t)); _val; })
void Channel$clear(channel_t *channel);
array_t Channel$view(channel_t *channel);
uint32_t Channel$hash(const channel_t **channel, const TypeInfo *type);
int32_t Channel$compare(const channel_t **x, const channel_t **y, const TypeInfo *type);
bool Channel$equal(const channel_t **x, const channel_t **y, const TypeInfo *type);
CORD Channel$as_text(const channel_t **channel, bool colorize, const TypeInfo *type);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
