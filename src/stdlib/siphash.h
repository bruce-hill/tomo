#pragma once

// An implementation of the SipHash algorithm.

#include <stddef.h>
#include <stdint.h>

#include "util.h"

// This value will be randomized on startup in tomo_init():
extern uint64_t TOMO_HASH_KEY[2];

PUREFUNC uint64_t siphash24(const uint8_t *src, size_t src_sz);
