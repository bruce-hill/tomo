// A simple text parsing primitive:
//     const char *line = "foo.txt:15";
//     const char *filename; int line;
//     if (strparse(line, &filename, ":", &line)) { success...}
// or:
//     FILE *f = ...;
//     if (fparse(f, &filename, ":", &line)) { success... }

#include <ctype.h>
#include <gc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "simpleparse.h"
#include "util.h"

static bool _match_word(const char **str, const char *target) {
    size_t len = strlen(target);
    if (strncasecmp(*str, target, len) == 0 && !isalnum((*str)[len]) && (*str)[len] != '_') {
        *str += len;
        return true;
    }
    return false;
}

public
const char *simpleparse(const char *str, int n, parse_type_e types[n], void *destinations[n]) {
    for (int i = 0; i < n; i++) {
        switch (types[i]) {
        case PARSE_SOME_OF: {
            if (destinations[i]) str += strspn(str, (char *)destinations[i]);
            break;
        }
        case PARSE_LITERAL: {
            const char *target = (const char *)destinations[i];
            if (target) {
                if (strncmp(str, target, strlen(target)) != 0) return str;
                str += strlen(target);
            }
            break;
        }
        case PARSE_STRING: {
            size_t len;
            static const char matching_pair[256] = {[(int)'('] = ')', [(int)'{'] = '}',   [(int)'['] = ']',
                                                    [(int)'"'] = '"', [(int)'\''] = '\'', [(int)'`'] = '`',
                                                    [(int)'<'] = '>'};
            if (i > 0 && i + 1 < n && types[i - 1] == PARSE_LITERAL && types[i + 1] == PARSE_LITERAL
                && destinations[i - 1] && destinations[i + 1] && strlen((char *)destinations[i - 1]) == 1
                && strlen((char *)destinations[i + 1]) == 1
                && *(char *)destinations[i + 1] == matching_pair[(int)*(char *)destinations[i - 1]]) {
                len = 0;
                char special_characters[4] = {'\\', *(char *)destinations[i - 1], *(char *)destinations[i + 1], 0};
                for (int depth = 1; depth > 0;) {
                    len += strcspn(str + len, special_characters);
                    if (str[len] == '\0') {
                        return str;
                    } else if (str[len] == '\\'
                               && (special_characters[1] == '"' || special_characters[1] == '\''
                                   || special_characters[1] == '`')) {
                        if (str[len + 1] == '\0') return str;
                        len += 2;
                    } else if (str[len] == special_characters[2]) { // Check for closing quotes before opening quotes
                        depth -= 1;
                        if (depth > 0) len += 1;
                    } else if (str[len] == special_characters[1]) {
                        depth += 1;
                        if (depth > 999999) return str;
                        len += 1;
                    }
                }
            } else if (i + 1 < n && types[i + 1] == PARSE_LITERAL) {
                const char *terminator = (const char *)destinations[i + 1];
                if (terminator) {
                    const char *end = strstr(str, terminator);
                    if (!end) return str;
                    len = (size_t)((ptrdiff_t)end - (ptrdiff_t)str);
                } else {
                    len = strlen(str);
                }
            } else if (i + 1 < n && types[i + 1] == PARSE_SOME_OF) {
                len = destinations[i + 1] ? strcspn(str, (char *)destinations[i + 1]) : strlen(str);
                ;
            } else {
                len = strlen(str);
            }
            if (destinations[i]) {
                char *matched = GC_MALLOC_ATOMIC(len + 1);
                memcpy(matched, str, len);
                matched[len] = '\0';
                *(const char **)destinations[i] = matched;
            }
            str += len;
            break;
        }
        case PARSE_DOUBLE: {
            char *end = NULL;
            double val = strtod(str, &end);
            if (end == str) return str;
            if (destinations[i]) *(double *)destinations[i] = val;
            str = end;
            break;
        }
        case PARSE_LONG: {
            char *end = NULL;
            long val = strtol(str, &end, 10);
            if (end == str) return str;
            if (destinations[i]) *(long *)destinations[i] = val;
            str = end;
            break;
        }
        case PARSE_BOOL: {
            if (_match_word(&str, "true") || _match_word(&str, "yes") || _match_word(&str, "on")
                || _match_word(&str, "1")) {
                if (destinations[i]) *(bool *)destinations[i] = true;
            } else if (_match_word(&str, "false") || _match_word(&str, "no") || _match_word(&str, "off")
                       || _match_word(&str, "0")) {
                if (destinations[i]) *(bool *)destinations[i] = false;
            } else {
                return str;
            }
            break;
        }
        default: break;
        }
    }
    return NULL;
}

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
