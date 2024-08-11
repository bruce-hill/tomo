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
#include "types.h"
#include "util.h"

public channel_t *Channel$new(void)
{
    channel_t *channel = new(channel_t);
    channel->items = (array_t){};
    channel->mutex = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
    channel->cond = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
    return channel;
}

public void Channel$push(channel_t *channel, const void *item, int64_t padded_item_size)
{
    (void)pthread_mutex_lock(&channel->mutex);
    Array$insert(&channel->items, item, 0, padded_item_size);
    (void)pthread_mutex_unlock(&channel->mutex);
    (void)pthread_cond_signal(&channel->cond);
}

public void Channel$push_all(channel_t *channel, array_t to_push, int64_t padded_item_size)
{
    (void)pthread_mutex_lock(&channel->mutex);
    Array$insert_all(&channel->items, to_push, 0, padded_item_size);
    (void)pthread_mutex_unlock(&channel->mutex);
    (void)pthread_cond_signal(&channel->cond);
}

public void Channel$pop(channel_t *channel, void *out, int64_t item_size, int64_t padded_item_size)
{
    (void)pthread_mutex_lock(&channel->mutex);
    while (channel->items.length == 0)
        pthread_cond_wait(&channel->cond, &channel->mutex);
    memcpy(out, channel->items.data, item_size);
    Array$remove(&channel->items, 1, 1, padded_item_size);
    (void)pthread_mutex_unlock(&channel->mutex);
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
