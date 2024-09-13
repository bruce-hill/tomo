#pragma once

// An implementation of the SipHash algorithm.

#include <stdint.h>
#include <stddef.h>

// This value will be randomized on startup in tomo_init():
extern uint64_t TOMO_HASH_KEY[2];

uint64_t siphash24(const uint8_t *src, size_t src_sz);

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
