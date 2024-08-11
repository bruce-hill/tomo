#pragma once

// Functions that operate on channels (thread-safe arrays)

#include <stdbool.h>
#include <gc/cord.h>

#include "datatypes.h"
#include "types.h"
#include "util.h"

channel_t *Channel$new(int64_t max_size);
void Channel$push(channel_t *channel, const void *item, int64_t padded_item_size);
#define Channel$push_value(channel, item, padded_item_size) ({ __typeof(item) _item = item; Channel$push(channel, &_item, padded_item_size); })
void Channel$push_all(channel_t *channel, array_t to_push, int64_t padded_item_size);
void Channel$pop(channel_t *channel, void *out, int64_t item_size, int64_t padded_item_size);
#define Channel$pop_value(channel, t, padded_item_size) ({ t _val; Channel$pop(channel, &_val, sizeof(t), padded_item_size); _val; })
void Channel$clear(channel_t *channel);
array_t Channel$view(channel_t *channel);
uint32_t Channel$hash(const channel_t **channel, const TypeInfo *type);
int32_t Channel$compare(const channel_t **x, const channel_t **y, const TypeInfo *type);
bool Channel$equal(const channel_t **x, const channel_t **y, const TypeInfo *type);
CORD Channel$as_text(const channel_t **channel, bool colorize, const TypeInfo *type);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
