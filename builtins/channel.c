// Functions that operate on channels

#include <ctype.h>
#include <err.h>
#include <gc.h>
#include <gc/cord.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/param.h>

#include "array.h"
#include "functions.h"
#include "halfsiphash.h"
#include "integers.h"
#include "types.h"
#include "util.h"
#include "where.h"

public channel_t *Channel$new(Int_t max_size)
{
    if (Int$compare_value(max_size, I(0)) <= 0)
        fail("Cannot create a channel with a size less than one: %ld", max_size);
    channel_t *channel = new(channel_t);
    channel->items = (array_t){};
    channel->mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    channel->cond = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
    channel->max_size = Int_to_Int64(max_size, false);
    return channel;
}

public void Channel$give(channel_t *channel, const void *item, Where_t where, int64_t padded_item_size)
{
    (void)pthread_mutex_lock(&channel->mutex);
    while (channel->items.length >= channel->max_size)
        pthread_cond_wait(&channel->cond, &channel->mutex);
    Int_t index = (where.tag == $tag$Where$Start) ? I(1) : I(0);
    Array$insert(&channel->items, item, index, padded_item_size);
    (void)pthread_mutex_unlock(&channel->mutex);
    (void)pthread_cond_signal(&channel->cond);
}

public void Channel$give_all(channel_t *channel, array_t to_give, Where_t where, int64_t padded_item_size)
{
    if (to_give.length == 0) return;
    (void)pthread_mutex_lock(&channel->mutex);
    Int_t index = (where.tag == $tag$Where$Start) ? I(1) : I(0);
    if (channel->items.length + to_give.length >= channel->max_size) {
        for (int64_t i = 0; i < to_give.length; i++) {
            while (channel->items.length >= channel->max_size)
                pthread_cond_wait(&channel->cond, &channel->mutex);
            Array$insert(&channel->items, to_give.data + i*to_give.stride, index, padded_item_size);
        }
    } else {
        Array$insert_all(&channel->items, to_give, index, padded_item_size);
    }
    (void)pthread_mutex_unlock(&channel->mutex);
    (void)pthread_cond_signal(&channel->cond);
}

public void Channel$get(channel_t *channel, void *out, Where_t where, int64_t item_size, int64_t padded_item_size)
{
    (void)pthread_mutex_lock(&channel->mutex);
    while (channel->items.length == 0)
        pthread_cond_wait(&channel->cond, &channel->mutex);
    memcpy(out, channel->items.data, item_size);
    Int_t index = (where.tag == $tag$Where$End) ? I(0) : I(1);
    Array$remove_at(&channel->items, index, I(1), padded_item_size);
    (void)pthread_mutex_unlock(&channel->mutex);
    (void)pthread_cond_signal(&channel->cond);
}

public void Channel$peek(channel_t *channel, void *out, Where_t where, int64_t item_size)
{
    (void)pthread_mutex_lock(&channel->mutex);
    while (channel->items.length == 0)
        pthread_cond_wait(&channel->cond, &channel->mutex);
    int64_t index = (where.tag == $tag$Where$End) ? channel->items.length-1 : 0;
    memcpy(out, channel->items.data + channel->items.stride*index, item_size);
    (void)pthread_mutex_unlock(&channel->mutex);
    (void)pthread_cond_signal(&channel->cond);
}

public array_t Channel$view(channel_t *channel)
{
    (void)pthread_mutex_lock(&channel->mutex);
    ARRAY_INCREF(channel->items);
    array_t ret = channel->items;
    (void)pthread_mutex_unlock(&channel->mutex);
    return ret;
}

public void Channel$clear(channel_t *channel)
{
    (void)pthread_mutex_lock(&channel->mutex);
    Array$clear(&channel->items);
    (void)pthread_mutex_unlock(&channel->mutex);
    (void)pthread_cond_signal(&channel->cond);
}

public uint32_t Channel$hash(const channel_t **channel, const TypeInfo *type)
{
    (void)type;
    uint32_t hash;
    halfsiphash(*channel, sizeof(channel_t*), TOMO_HASH_KEY, (uint8_t*)&hash, sizeof(hash));
    return hash;
}

public int32_t Channel$compare(const channel_t **x, const channel_t **y, const TypeInfo *type)
{
    (void)type;
    return (*x > *y) - (*x < *y);
}

bool Channel$equal(const channel_t **x, const channel_t **y, const TypeInfo *type)
{
    (void)type;
    return (*x == *y);
}

CORD Channel$as_text(const channel_t **channel, bool colorize, const TypeInfo *type)
{
    const TypeInfo *item_type = type->ChannelInfo.item;
    if (!channel) {
        CORD typename = generic_as_text(NULL, false, item_type);
        return colorize ? CORD_asprintf("\x1b[34;1m|:%s|\x1b[m", typename) : CORD_all("|:", typename, "|");
    }
    CORD typename = generic_as_text(NULL, false, item_type);
    return CORD_asprintf(colorize ? "\x1b[34;1m|:%s|<%p>\x1b[m" : "|:%s|<%p>", typename, *channel);
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
