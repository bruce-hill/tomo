// Representation of the Text type, which uses a struct inspired by Raku's
// string representation and libunistr

#pragma once

#include <stdint.h>

enum text_type { TEXT_NONE, TEXT_ASCII, TEXT_GRAPHEMES, TEXT_CONCAT, TEXT_BLOB };

typedef struct Text_s {
    uint64_t length : 53; // Number of grapheme clusters
    uint8_t tag : 3;
    uint8_t depth : 8;
    union {
        struct {
            const char *ascii;
            // char ascii_buf[8];
        };
        struct {
            const int32_t *graphemes;
            // int32_t grapheme_buf[2];
        };
        struct {
            const struct Text_s *left, *right;
        };
        struct {
            const int32_t *map;
            const uint8_t *bytes;
        } blob;
    };
} Text_t;

typedef Text_t OptionalText_t;
