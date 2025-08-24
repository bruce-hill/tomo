// Logic for text pattern matching

#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <sys/param.h>
#include <unictype.h>
#include <uniname.h>
#include <unistring/version.h>

#define MAX_BACKREFS 100

typedef struct {
    Text_t text;
    Int_t index;
    List_t captures;
} PatternMatch;

typedef struct {
    Text_t text;
    Int_t index;
    List_t captures;
    bool is_none : 1;
} OptionalPatternMatch;

#define NONE_MATCH ((OptionalPatternMatch){.is_none = true})

typedef struct {
    int64_t index, length;
    bool occupied, recursive;
} capture_t;

typedef struct {
    enum { PAT_START, PAT_END, PAT_ANY, PAT_GRAPHEME, PAT_PROPERTY, PAT_QUOTE, PAT_PAIR, PAT_FUNCTION } tag;
    bool negated, non_capturing;
    int64_t min, max;
    union {
        int32_t grapheme;
        uc_property_t property;
        int64_t (*fn)(TextIter_t *, int64_t);
        int32_t quote_graphemes[2];
        int32_t pair_graphemes[2];
    };
} pat_t;

static Text_t replace_list(Text_t text, List_t replacements, Text_t backref_pat, bool recursive);

static INLINE void skip_whitespace(TextIter_t *state, int64_t *i) {
    while (*i < state->stack[0].text.length) {
        int32_t grapheme = Text$get_grapheme_fast(state, *i);
        if (grapheme > 0 && !uc_is_property_white_space((ucs4_t)grapheme)) return;
        *i += 1;
    }
}

static INLINE bool match_grapheme(TextIter_t *state, int64_t *i, int32_t grapheme) {
    if (*i < state->stack[0].text.length && Text$get_grapheme_fast(state, *i) == grapheme) {
        *i += 1;
        return true;
    }
    return false;
}

static INLINE bool match_str(TextIter_t *state, int64_t *i, const char *str) {
    int64_t matched = 0;
    while (matched[str]) {
        if (*i + matched >= state->stack[0].text.length || Text$get_grapheme_fast(state, *i + matched) != str[matched])
            return false;
        matched += 1;
    }
    *i += matched;
    return true;
}

static int64_t parse_int(TextIter_t *state, int64_t *i) {
    int64_t value = 0;
    for (;; *i += 1) {
        uint32_t grapheme = Text$get_main_grapheme_fast(state, *i);
        int digit = uc_digit_value(grapheme);
        if (digit < 0) break;
        if (value >= INT64_MAX / 10) break;
        value = 10 * value + digit;
    }
    return value;
}

static const char *get_property_name(TextIter_t *state, int64_t *i) {
    skip_whitespace(state, i);
    char *name = GC_MALLOC_ATOMIC(UNINAME_MAX);
    char *dest = name;
    while (*i < state->stack[0].text.length) {
        int32_t grapheme = Text$get_grapheme_fast(state, *i);
        if (!(grapheme & ~0xFF) && (isalnum(grapheme) || grapheme == ' ' || grapheme == '_' || grapheme == '-')) {
            *dest = (char)grapheme;
            ++dest;
            if (dest >= name + UNINAME_MAX - 1) break;
        } else {
            break;
        }
        *i += 1;
    }

    while (dest > name && dest[-1] == ' ')
        *(dest--) = '\0';

    if (dest == name) return NULL;
    *dest = '\0';
    return name;
}

#define EAT1(state, index, cond)                                                                                       \
    ({                                                                                                                 \
        int32_t grapheme = Text$get_grapheme_fast(state, index);                                                       \
        bool success = (cond);                                                                                         \
        if (success) index += 1;                                                                                       \
        success;                                                                                                       \
    })

#define EAT2(state, index, cond1, cond2)                                                                               \
    ({                                                                                                                 \
        int32_t grapheme = Text$get_grapheme_fast(state, index);                                                       \
        bool success = (cond1);                                                                                        \
        if (success) {                                                                                                 \
            grapheme = Text$get_grapheme_fast(state, index + 1);                                                       \
            success = (cond2);                                                                                         \
            if (success) index += 2;                                                                                   \
        }                                                                                                              \
        success;                                                                                                       \
    })

#define EAT_MANY(state, index, cond)                                                                                   \
    ({                                                                                                                 \
        int64_t _n = 0;                                                                                                \
        while (EAT1(state, index, cond)) {                                                                             \
            _n += 1;                                                                                                   \
        }                                                                                                              \
        _n;                                                                                                            \
    })

static int64_t match_email(TextIter_t *state, int64_t index) {
    // email = local "@" domain
    // local = 1-64 ([a-zA-Z0-9!#$%&‘*+–/=?^_`.{|}~] | non-ascii)
    // domain = dns-label ("." dns-label)*
    // dns-label = 1-63 ([a-zA-Z0-9-] | non-ascii)

    if (index > 0) {
        uint32_t prev_codepoint = Text$get_main_grapheme_fast(state, index - 1);
        if (uc_is_property_alphabetic(prev_codepoint)) return -1;
    }

    int64_t start_index = index;

    // Local part:
    int64_t local_len = 0;
    static const char *allowed_local = "!#$%&‘*+–/=?^_`.{|}~";
    while (EAT1(state, index, (grapheme & ~0x7F) || isalnum((char)grapheme) || strchr(allowed_local, (char)grapheme))) {
        local_len += 1;
        if (local_len > 64) return -1;
    }

    if (!EAT1(state, index, grapheme == '@')) return -1;

    // Host
    int64_t host_len = 0;
    do {
        int64_t label_len = 0;
        while (EAT1(state, index, (grapheme & ~0x7F) || isalnum((char)grapheme) || grapheme == '-')) {
            label_len += 1;
            if (label_len > 63) return -1;
        }

        if (label_len == 0) return -1;

        host_len += label_len;
        if (host_len > 255) return -1;
        host_len += 1;
    } while (EAT1(state, index, grapheme == '.'));

    return index - start_index;
}

static int64_t match_ipv6(TextIter_t *state, int64_t index) {
    if (index > 0) {
        int32_t prev_codepoint = Text$get_grapheme_fast(state, index - 1);
        if ((prev_codepoint & ~0x7F) && (isxdigit(prev_codepoint) || prev_codepoint == ':')) return -1;
    }
    int64_t start_index = index;
    const int NUM_CLUSTERS = 8;
    bool double_colon_used = false;
    for (int cluster = 0; cluster < NUM_CLUSTERS; cluster++) {
        for (int digits = 0; digits < 4; digits++) {
            if (!EAT1(state, index, ~(grapheme & ~0x7F) && isxdigit((char)grapheme))) break;
        }
        if (EAT1(state, index, ~(grapheme & ~0x7F) && isxdigit((char)grapheme))) return -1; // Too many digits

        if (cluster == NUM_CLUSTERS - 1) {
            break;
        } else if (!EAT1(state, index, grapheme == ':')) {
            if (double_colon_used) break;
            return -1;
        }

        if (EAT1(state, index, grapheme == ':')) {
            if (double_colon_used) return -1;
            double_colon_used = true;
        }
    }
    return index - start_index;
}

static int64_t match_ipv4(TextIter_t *state, int64_t index) {
    if (index > 0) {
        int32_t prev_codepoint = Text$get_grapheme_fast(state, index - 1);
        if ((prev_codepoint & ~0x7F) && (isdigit(prev_codepoint) || prev_codepoint == '.')) return -1;
    }
    int64_t start_index = index;

    const int NUM_CLUSTERS = 4;
    for (int cluster = 0; cluster < NUM_CLUSTERS; cluster++) {
        for (int digits = 0; digits < 3; digits++) {
            if (!EAT1(state, index, ~(grapheme & ~0x7F) && isdigit((char)grapheme))) {
                if (digits == 0) return -1;
                break;
            }
        }

        if (EAT1(state, index, ~(grapheme & ~0x7F) && isdigit((char)grapheme))) return -1; // Too many digits

        if (cluster == NUM_CLUSTERS - 1) break;
        else if (!EAT1(state, index, grapheme == '.')) return -1;
    }
    return (index - start_index);
}

static int64_t match_ip(TextIter_t *state, int64_t index) {
    int64_t len = match_ipv6(state, index);
    if (len >= 0) return len;
    len = match_ipv4(state, index);
    return (len >= 0) ? len : -1;
}

static int64_t match_host(TextIter_t *state, int64_t index) {
    int64_t ip_len = match_ip(state, index);
    if (ip_len > 0) return ip_len;

    int64_t start_index = index;
    if (match_grapheme(state, &index, '[')) {
        ip_len = match_ip(state, index);
        if (ip_len <= 0) return -1;
        index += ip_len;
        if (match_grapheme(state, &index, ']')) return (index - start_index);
        return -1;
    }

    if (!EAT1(state, index, isalpha(grapheme))) return -1;

    static const char *non_host_chars = "/#?:@ \t\r\n<>[]{}\\^|\"`";
    EAT_MANY(state, index, (grapheme & ~0x7F) || !strchr(non_host_chars, (char)grapheme));
    return (index - start_index);
}

static int64_t match_authority(TextIter_t *state, int64_t index) {
    int64_t authority_start = index;
    static const char *non_segment_chars = "/#?:@ \t\r\n<>[]{}\\^|\"`.";

    // Optional user@ prefix:
    int64_t username_len = EAT_MANY(state, index, (grapheme & ~0x7F) || !strchr(non_segment_chars, (char)grapheme));
    if (username_len < 1 || !EAT1(state, index, grapheme == '@')) index = authority_start; // No user@ part

    // Host:
    int64_t host_len = match_host(state, index);
    if (host_len <= 0) return -1;
    index += host_len;

    // Port:
    if (EAT1(state, index, grapheme == ':')) {
        if (EAT_MANY(state, index, !(grapheme & ~0x7F) && isdigit(grapheme)) == 0) return -1;
    }
    return (index - authority_start);
}

static int64_t match_uri(TextIter_t *state, int64_t index) {
    // URI = scheme ":" ["//" authority] path ["?" query] ["#" fragment]
    // scheme = [a-zA-Z] [a-zA-Z0-9+.-]
    // authority = [userinfo "@"] host [":" port]

    if (index > 0) {
        // Don't match if we're not at a word edge:
        uint32_t prev_codepoint = Text$get_main_grapheme_fast(state, index - 1);
        if (uc_is_property_alphabetic(prev_codepoint)) return -1;
    }

    int64_t start_index = index;

    // Scheme:
    if (!EAT1(state, index, isalpha(grapheme))) return -1;
    EAT_MANY(state, index,
             !(grapheme & ~0x7F) && (isalnum(grapheme) || grapheme == '+' || grapheme == '.' || grapheme == '-'));
    if (!match_grapheme(state, &index, ':')) return -1;

    // Authority:
    int64_t authority_len;
    if (match_str(state, &index, "//")) {
        authority_len = match_authority(state, index);
        if (authority_len > 0) index += authority_len;
    } else {
        authority_len = 0;
    }

    // Path:
    int64_t path_start = index;
    if (EAT1(state, index, grapheme == '/') || authority_len <= 0) {
        static const char *non_path = " \"#?<>[]{}\\^`|";
        EAT_MANY(state, index, (grapheme & ~0x7F) || !strchr(non_path, (char)grapheme));

        if (EAT1(state, index, grapheme == '?')) { // Query
            static const char *non_query = " \"#<>[]{}\\^`|";
            EAT_MANY(state, index, (grapheme & ~0x7F) || !strchr(non_query, (char)grapheme));
        }

        if (EAT1(state, index, grapheme == '#')) { // Fragment
            static const char *non_fragment = " \"#<>[]{}\\^`|";
            EAT_MANY(state, index, (grapheme & ~0x7F) || !strchr(non_fragment, (char)grapheme));
        }
    }

    if (authority_len <= 0 && index == path_start) return -1;

    return index - start_index;
}

static int64_t match_url(TextIter_t *state, int64_t index) {
    int64_t lookahead = index;
    if (!(match_str(state, &lookahead, "https:") || match_str(state, &lookahead, "http:")
          || match_str(state, &lookahead, "ftp:") || match_str(state, &lookahead, "wss:")
          || match_str(state, &lookahead, "ws:")))
        return -1;

    return match_uri(state, index);
}

static int64_t match_id(TextIter_t *state, int64_t index) {
    if (!EAT1(state, index, uc_is_property((ucs4_t)grapheme, UC_PROPERTY_XID_START))) return -1;
    return 1 + EAT_MANY(state, index, uc_is_property((ucs4_t)grapheme, UC_PROPERTY_XID_CONTINUE));
}

static int64_t match_int(TextIter_t *state, int64_t index) {
    int64_t negative = EAT1(state, index, grapheme == '-') ? 1 : 0;
    int64_t len = EAT_MANY(state, index, uc_is_property((ucs4_t)grapheme, UC_PROPERTY_DECIMAL_DIGIT));
    return len > 0 ? negative + len : -1;
}

static int64_t match_alphanumeric(TextIter_t *state, int64_t index) {
    return EAT1(state, index, uc_is_property_alphabetic((ucs4_t)grapheme) || uc_is_property_numeric((ucs4_t)grapheme))
               ? 1
               : -1;
}

static int64_t match_num(TextIter_t *state, int64_t index) {
    bool negative = EAT1(state, index, grapheme == '-') ? 1 : 0;
    int64_t pre_decimal = EAT_MANY(state, index, uc_is_property((ucs4_t)grapheme, UC_PROPERTY_DECIMAL_DIGIT));
    bool decimal = (EAT1(state, index, grapheme == '.') == 1);
    int64_t post_decimal =
        decimal ? EAT_MANY(state, index, uc_is_property((ucs4_t)grapheme, UC_PROPERTY_DECIMAL_DIGIT)) : 0;
    if (pre_decimal == 0 && post_decimal == 0) return -1;
    return negative + pre_decimal + decimal + post_decimal;
}

static int64_t match_newline(TextIter_t *state, int64_t index) {
    if (index >= state->stack[0].text.length) return -1;

    uint32_t grapheme = index >= state->stack[0].text.length ? 0 : Text$get_main_grapheme_fast(state, index);
    if (grapheme == '\n') return 1;
    if (grapheme == '\r' && Text$get_grapheme_fast(state, index + 1) == '\n') return 2;
    return -1;
}

static int64_t match_pat(TextIter_t *state, int64_t index, pat_t pat) {
    Text_t text = state->stack[0].text;
    int32_t grapheme = index >= text.length ? 0 : Text$get_grapheme_fast(state, index);

    switch (pat.tag) {
    case PAT_START: {
        if (index == 0) return pat.negated ? -1 : 0;
        return pat.negated ? 0 : -1;
    }
    case PAT_END: {
        if (index >= text.length) return pat.negated ? -1 : 0;
        return pat.negated ? 0 : -1;
    }
    case PAT_ANY: {
        assert(!pat.negated);
        return (index < text.length) ? 1 : -1;
    }
    case PAT_GRAPHEME: {
        if (index >= text.length) return -1;
        else if (grapheme == pat.grapheme) return pat.negated ? -1 : 1;
        return pat.negated ? 1 : -1;
    }
    case PAT_PROPERTY: {
        if (index >= text.length) return -1;
        else if (uc_is_property((ucs4_t)grapheme, pat.property)) return pat.negated ? -1 : 1;
        return pat.negated ? 1 : -1;
    }
    case PAT_PAIR: {
        // Nested punctuation: (?), [?], etc
        if (index >= text.length) return -1;

        int32_t open = pat.pair_graphemes[0];
        if (grapheme != open) return pat.negated ? 1 : -1;

        int32_t close = pat.pair_graphemes[1];
        int64_t depth = 1;
        int64_t match_len = 1;
        for (; depth > 0; match_len++) {
            if (index + match_len >= text.length) return pat.negated ? 1 : -1;

            int32_t c = Text$get_grapheme_fast(state, index + match_len);
            if (c == open) depth += 1;
            else if (c == close) depth -= 1;
        }
        return pat.negated ? -1 : match_len;
    }
    case PAT_QUOTE: {
        // Nested quotes: "?", '?', etc
        if (index >= text.length) return -1;

        int32_t open = pat.quote_graphemes[0];
        if (grapheme != open) return pat.negated ? 1 : -1;

        int32_t close = pat.quote_graphemes[1];
        for (int64_t i = index + 1; i < text.length; i++) {
            int32_t c = Text$get_grapheme_fast(state, i);
            if (c == close) {
                return pat.negated ? -1 : (i - index) + 1;
            } else if (c == '\\' && index + 1 < text.length) {
                i += 1; // Skip ahead an extra step
            }
        }
        return pat.negated ? 1 : -1;
    }
    case PAT_FUNCTION: {
        int64_t match_len = pat.fn(state, index);
        if (match_len >= 0) return pat.negated ? -1 : match_len;
        return pat.negated ? 1 : -1;
    }
    default: errx(1, "Invalid pattern");
    }
    errx(1, "Unreachable");
    return 0;
}

static pat_t parse_next_pat(TextIter_t *state, int64_t *index) {
    if (EAT2(state, *index, uc_is_property((ucs4_t)grapheme, UC_PROPERTY_QUOTATION_MARK), grapheme == '?')) {
        // Quotations: "?", '?', etc
        int32_t open = Text$get_grapheme_fast(state, *index - 2);
        int32_t close = open;
        uc_mirror_char((ucs4_t)open, (ucs4_t *)&close);
        if (!match_grapheme(state, index, close)) fail("Pattern's closing quote is missing: ", state->stack[0].text);

        return (pat_t){
            .tag = PAT_QUOTE,
            .min = 1,
            .max = 1,
            .quote_graphemes = {open, close},
        };
    } else if (EAT2(state, *index, uc_is_property((ucs4_t)grapheme, UC_PROPERTY_PAIRED_PUNCTUATION), grapheme == '?')) {
        // Nested punctuation: (?), [?], etc
        int32_t open = Text$get_grapheme_fast(state, *index - 2);
        int32_t close = open;
        uc_mirror_char((ucs4_t)open, (ucs4_t *)&close);
        if (!match_grapheme(state, index, close)) fail("Pattern's closing brace is missing: ", state->stack[0].text);

        return (pat_t){
            .tag = PAT_PAIR,
            .min = 1,
            .max = 1,
            .pair_graphemes = {open, close},
        };
    } else if (EAT1(state, *index, grapheme == '{')) { // named patterns {id}, {2-3 hex}, etc.
        skip_whitespace(state, index);
        int64_t min, max;
        if (uc_is_digit((ucs4_t)Text$get_grapheme_fast(state, *index))) {
            min = parse_int(state, index);
            skip_whitespace(state, index);
            if (match_grapheme(state, index, '+')) {
                max = INT64_MAX;
            } else if (match_grapheme(state, index, '-')) {
                max = parse_int(state, index);
            } else {
                max = min;
            }
            if (min > max) fail("Minimum repetitions (", min, ") is less than the maximum (", max, ")");
        } else {
            min = -1, max = -1;
        }

        skip_whitespace(state, index);

        bool negated = match_grapheme(state, index, '!');
#define PAT(_tag, ...) ((pat_t){.min = min, .max = max, .negated = negated, .tag = _tag, __VA_ARGS__})
        const char *prop_name;
        if (match_str(state, index, "..")) prop_name = "..";
        else prop_name = get_property_name(state, index);

        if (!prop_name) {
            // Literal character, e.g. {1?}
            skip_whitespace(state, index);
            int32_t grapheme = Text$get_grapheme_fast(state, (*index)++);
            if (!match_grapheme(state, index, '}')) fail("Missing closing '}' in pattern: ", state->stack[0].text);
            return PAT(PAT_GRAPHEME, .grapheme = grapheme);
        } else if (strlen(prop_name) == 1) {
            // Single letter names: {1+ A}
            skip_whitespace(state, index);
            if (!match_grapheme(state, index, '}')) fail("Missing closing '}' in pattern: ", state->stack[0].text);
            return PAT(PAT_GRAPHEME, .grapheme = prop_name[0]);
        }

        skip_whitespace(state, index);
        if (!match_grapheme(state, index, '}')) fail("Missing closing '}' in pattern: ", state->stack[0].text);

        switch (tolower(prop_name[0])) {
        case '.':
            if (prop_name[1] == '.') {
                if (negated) return ((pat_t){.tag = PAT_END, .min = min, .max = max, .non_capturing = true});
                else return PAT(PAT_ANY);
            }
            break;
        case 'a':
            if (strcasecmp(prop_name, "authority") == 0) {
                return PAT(PAT_FUNCTION, .fn = match_authority);
            } else if (strcasecmp(prop_name, "alphanum") == 0 || strcasecmp(prop_name, "anum") == 0
                       || strcasecmp(prop_name, "alphanumeric") == 0) {
                return PAT(PAT_FUNCTION, .fn = match_alphanumeric);
            }
            break;
        case 'c':
            if (strcasecmp(prop_name, "crlf") == 0) return PAT(PAT_FUNCTION, .fn = match_newline);
            break;
        case 'd':
            if (strcasecmp(prop_name, "digit") == 0) {
                return PAT(PAT_PROPERTY, .property = UC_PROPERTY_DECIMAL_DIGIT);
            }
            break;
        case 'e':
            if (strcasecmp(prop_name, "end") == 0) {
                return PAT(PAT_END, .non_capturing = !negated);
            } else if (strcasecmp(prop_name, "email") == 0) {
                return PAT(PAT_FUNCTION, .fn = match_email);
            }
#if _LIBUNISTRING_VERSION >= 0x0100000
            else if (strcasecmp(prop_name, "emoji") == 0) {
                return PAT(PAT_PROPERTY, .property = UC_PROPERTY_EMOJI);
            }
#endif
            break;
        case 'h':
            if (strcasecmp(prop_name, "host") == 0) {
                return PAT(PAT_FUNCTION, .fn = match_host);
            }
            break;
        case 'i':
            if (strcasecmp(prop_name, "id") == 0) {
                return PAT(PAT_FUNCTION, .fn = match_id);
            } else if (strcasecmp(prop_name, "int") == 0) {
                return PAT(PAT_FUNCTION, .fn = match_int);
            } else if (strcasecmp(prop_name, "ipv4") == 0) {
                return PAT(PAT_FUNCTION, .fn = match_ipv4);
            } else if (strcasecmp(prop_name, "ipv6") == 0) {
                return PAT(PAT_FUNCTION, .fn = match_ipv6);
            } else if (strcasecmp(prop_name, "ip") == 0) {
                return PAT(PAT_FUNCTION, .fn = match_ip);
            }
            break;
        case 'n':
            if (strcasecmp(prop_name, "nl") == 0 || strcasecmp(prop_name, "newline") == 0) {
                return PAT(PAT_FUNCTION, .fn = match_newline);
            } else if (strcasecmp(prop_name, "num") == 0) {
                return PAT(PAT_FUNCTION, .fn = match_num);
            }
            break;
        case 's':
            if (strcasecmp(prop_name, "start") == 0) {
                return PAT(PAT_START, .non_capturing = !negated);
            }
            break;
        case 'u':
            if (strcasecmp(prop_name, "uri") == 0) {
                return PAT(PAT_FUNCTION, .fn = match_uri);
            } else if (strcasecmp(prop_name, "url") == 0) {
                return PAT(PAT_FUNCTION, .fn = match_url);
            }
            break;
        case 'w':
            if (strcasecmp(prop_name, "word") == 0) {
                return PAT(PAT_FUNCTION, .fn = match_id);
            } else if (strcasecmp(prop_name, "ws") == 0 || strcasecmp(prop_name, "whitespace") == 0) {
                return PAT(PAT_PROPERTY, .property = UC_PROPERTY_WHITE_SPACE);
            }
            break;
        default: break;
        }

        uc_property_t prop = uc_property_byname(prop_name);
        if (uc_property_is_valid(prop)) return PAT(PAT_PROPERTY, .property = prop);

        ucs4_t grapheme = unicode_name_character(prop_name);
        if (grapheme == UNINAME_INVALID) fail("Not a valid property or character name: ", prop_name);
        return PAT(PAT_GRAPHEME, .grapheme = (int32_t)grapheme);
#undef PAT
    } else {
        return (pat_t){.tag = PAT_GRAPHEME,
                       .non_capturing = true,
                       .min = 1,
                       .max = 1,
                       .grapheme = Text$get_grapheme_fast(state, (*index)++)};
    }
}

static int64_t match(Text_t text, int64_t text_index, Text_t pattern, int64_t pattern_index, capture_t *captures,
                     int64_t capture_index) {
    if (pattern_index >= pattern.length) // End of the pattern
        return 0;

    int64_t start_index = text_index;
    TextIter_t pattern_state = NEW_TEXT_ITER_STATE(pattern), text_state = NEW_TEXT_ITER_STATE(text);
    pat_t pat = parse_next_pat(&pattern_state, &pattern_index);

    if (pat.min == -1 && pat.max == -1) {
        if (pat.tag == PAT_ANY && pattern_index >= pattern.length) {
            pat.min = pat.max = MAX(1, text.length - text_index);
        } else {
            pat.min = 1;
            pat.max = INT64_MAX;
        }
    }

    int64_t capture_start = text_index;
    int64_t count = 0, capture_len = 0, next_match_len = 0;

    if (pat.tag == PAT_ANY && pattern_index >= pattern.length) {
        int64_t remaining = text.length - text_index;
        capture_len = remaining >= pat.min ? MIN(remaining, pat.max) : -1;
        text_index += capture_len;
        goto success;
    }

    if (pat.min == 0 && pattern_index < pattern.length) {
        next_match_len =
            match(text, text_index, pattern, pattern_index, captures, capture_index + (pat.non_capturing ? 0 : 1));
        if (next_match_len >= 0) {
            capture_len = 0;
            goto success;
        }
    }

    while (count < pat.max) {
        int64_t match_len = match_pat(&text_state, text_index, pat);
        if (match_len < 0) break;
        capture_len += match_len;
        text_index += match_len;
        count += 1;

        if (pattern_index < pattern.length) { // More stuff after this
            if (count < pat.min) next_match_len = -1;
            else
                next_match_len = match(text, text_index, pattern, pattern_index, captures,
                                       capture_index + (pat.non_capturing ? 0 : 1));
        } else {
            next_match_len = 0;
        }

        if (match_len == 0) {
            if (next_match_len >= 0) {
                // If we're good to go, no need to keep re-matching zero-length
                // matches till we hit max:
                count = pat.max;
                break;
            } else {
                return -1;
            }
        }

        if (pattern_index < pattern.length && next_match_len >= 0) break; // Next guy exists and wants to stop here

        if (text_index >= text.length) break;
    }

    if (count < pat.min || next_match_len < 0) return -1;

success:
    if (captures && capture_index < MAX_BACKREFS && !pat.non_capturing) {
        if (pat.tag == PAT_PAIR || pat.tag == PAT_QUOTE) {
            assert(capture_len > 0);
            captures[capture_index] = (capture_t){
                .index = capture_start + 1, // Skip leading quote/paren
                .length = capture_len - 2, // Skip open/close
                .occupied = true,
                .recursive = (pat.tag == PAT_PAIR),
            };
        } else {
            captures[capture_index] = (capture_t){
                .index = capture_start,
                .length = capture_len,
                .occupied = true,
                .recursive = false,
            };
        }
    }
    return (text_index - start_index) + next_match_len;
}

#undef EAT1
#undef EAT2
#undef EAT_MANY

static int64_t _find(Text_t text, Text_t pattern, int64_t first, int64_t last, int64_t *match_length,
                     capture_t *captures) {
    int32_t first_grapheme = Text$get_grapheme(pattern, 0);
    bool find_first = (first_grapheme != '{' && !uc_is_property((ucs4_t)first_grapheme, UC_PROPERTY_QUOTATION_MARK)
                       && !uc_is_property((ucs4_t)first_grapheme, UC_PROPERTY_PAIRED_PUNCTUATION));

    TextIter_t text_state = NEW_TEXT_ITER_STATE(text);
    for (int64_t i = first; i <= last; i++) {
        // Optimization: quickly skip ahead to first char in pattern:
        if (find_first) {
            while (i < text.length && Text$get_grapheme_fast(&text_state, i) != first_grapheme)
                ++i;
        }

        int64_t m = match(text, i, pattern, 0, captures, 0);
        if (m >= 0) {
            if (match_length) *match_length = m;
            return i;
        }
    }
    if (match_length) *match_length = -1;
    return -1;
}

static OptionalPatternMatch find(Text_t text, Text_t pattern, Int_t from_index) {
    int64_t first = Int64$from_int(from_index, false);
    if (first == 0) fail("Invalid index: 0");
    if (first < 0) first = text.length + first + 1;
    if (first > text.length || first < 1) return NONE_MATCH;

    capture_t captures[MAX_BACKREFS] = {};
    int64_t len = 0;
    int64_t found = _find(text, pattern, first - 1, text.length - 1, &len, captures);
    if (found == -1) return NONE_MATCH;

    List_t capture_list = {};
    for (int i = 0; captures[i].occupied; i++) {
        Text_t capture = Text$slice(text, I(captures[i].index + 1), I(captures[i].index + captures[i].length));
        List$insert(&capture_list, &capture, I(0), sizeof(Text_t));
    }
    return (OptionalPatternMatch){
        .text = Text$slice(text, I(found + 1), I(found + len)),
        .index = I(found + 1),
        .captures = capture_list,
    };
}

PUREFUNC static bool Pattern$has(Text_t text, Text_t pattern) {
    if (Text$starts_with(pattern, Text("{start}"), &pattern)) {
        int64_t m = match(text, 0, pattern, 0, NULL, 0);
        return m >= 0;
    } else if (Text$ends_with(text, Text("{end}"), NULL)) {
        for (int64_t i = text.length - 1; i >= 0; i--) {
            int64_t match_len = match(text, i, pattern, 0, NULL, 0);
            if (match_len >= 0 && i + match_len == text.length) return true;
        }
        return false;
    } else {
        int64_t found = _find(text, pattern, 0, text.length - 1, NULL, NULL);
        return (found >= 0);
    }
}

static bool Pattern$matches(Text_t text, Text_t pattern) {
    capture_t captures[MAX_BACKREFS] = {};
    int64_t match_len = match(text, 0, pattern, 0, NULL, 0);
    return (match_len == text.length);
}

static bool Pattern$match_at(Text_t text, Text_t pattern, Int_t pos, PatternMatch *dest) {
    int64_t start = Int64$from_int(pos, false) - 1;
    capture_t captures[MAX_BACKREFS] = {};
    int64_t match_len = match(text, start, pattern, 0, captures, 0);
    if (match_len < 0) return false;

    List_t capture_list = {};
    for (int i = 0; captures[i].occupied; i++) {
        Text_t capture = Text$slice(text, I(captures[i].index + 1), I(captures[i].index + captures[i].length));
        List$insert(&capture_list, &capture, I(0), sizeof(Text_t));
    }
    dest->text = Text$slice(text, I(start + 1), I(start + match_len));
    dest->index = I(start + 1);
    dest->captures = capture_list;
    return true;
}

static OptionalList_t Pattern$captures(Text_t text, Text_t pattern) {
    capture_t captures[MAX_BACKREFS] = {};
    int64_t match_len = match(text, 0, pattern, 0, captures, 0);
    if (match_len != text.length) return NONE_LIST;

    List_t capture_list = {};
    for (int i = 0; captures[i].occupied; i++) {
        Text_t capture = Text$slice(text, I(captures[i].index + 1), I(captures[i].index + captures[i].length));
        List$insert(&capture_list, &capture, I(0), sizeof(Text_t));
    }
    return capture_list;
}

static List_t Pattern$find_all(Text_t text, Text_t pattern) {
    if (pattern.length == 0) // special case
        return (List_t){.length = 0};

    List_t matches = {};
    for (int64_t i = 1;;) {
        OptionalPatternMatch m = find(text, pattern, I(i));
        if (m.is_none) break;
        i = Int64$from_int(m.index, false) + m.text.length;
        List$insert(&matches, &m, I_small(0), sizeof(PatternMatch));
    }
    return matches;
}

typedef struct {
    TextIter_t state;
    Int_t i;
    Text_t pattern;
} match_iter_state_t;

static OptionalPatternMatch next_match(match_iter_state_t *state) {
    if (Int64$from_int(state->i, false) > state->state.stack[0].text.length) return NONE_MATCH;

    OptionalPatternMatch m = find(state->state.stack[0].text, state->pattern, state->i);
    if (m.is_none) // No match
        state->i = I(state->state.stack[0].text.length + 1);
    else state->i = Int$plus(m.index, I(MAX(1, m.text.length)));
    return m;
}

static Closure_t Pattern$by_match(Text_t text, Text_t pattern) {
    return (Closure_t){
        .fn = (void *)next_match,
        .userdata = new (match_iter_state_t, .state = NEW_TEXT_ITER_STATE(text), .i = I_small(1), .pattern = pattern),
    };
}

static Text_t apply_backrefs(Text_t text, List_t recursive_replacements, Text_t replacement, Text_t backref_pat,
                             capture_t *captures) {
    if (backref_pat.length == 0) return replacement;

    int32_t first_grapheme = Text$get_grapheme(backref_pat, 0);
    bool find_first = (first_grapheme != '{' && !uc_is_property((ucs4_t)first_grapheme, UC_PROPERTY_QUOTATION_MARK)
                       && !uc_is_property((ucs4_t)first_grapheme, UC_PROPERTY_PAIRED_PUNCTUATION));

    Text_t ret = Text("");
    TextIter_t replacement_state = NEW_TEXT_ITER_STATE(replacement);
    int64_t nonmatching_pos = 0;
    for (int64_t pos = 0; pos < replacement.length;) {
        // Optimization: quickly skip ahead to first char in the backref pattern:
        if (find_first) {
            while (pos < replacement.length && Text$get_grapheme_fast(&replacement_state, pos) != first_grapheme)
                ++pos;
        }

        int64_t backref_len = match(replacement, pos, backref_pat, 0, NULL, 0);
        if (backref_len < 0) {
            pos += 1;
            continue;
        }

        int64_t after_backref = pos + backref_len;
        int64_t backref = parse_int(&replacement_state, &after_backref);
        if (after_backref == pos + backref_len) { // Not actually a backref if there's no number
            pos += 1;
            continue;
        }
        if (backref < 0 || backref > 9)
            fail("Invalid backref index: ", backref, " (only 0-", MAX_BACKREFS - 1, " are allowed)");
        backref_len = (after_backref - pos);

        if (Text$get_grapheme_fast(&replacement_state, pos + backref_len) == ';')
            backref_len += 1; // skip optional semicolon

        if (!captures[backref].occupied) fail("There is no capture number ", backref, "!");

        Text_t backref_text =
            Text$slice(text, I(captures[backref].index + 1), I(captures[backref].index + captures[backref].length));

        if (captures[backref].recursive && recursive_replacements.length > 0)
            backref_text = replace_list(backref_text, recursive_replacements, backref_pat, true);

        if (pos > nonmatching_pos) {
            Text_t before_slice = Text$slice(replacement, I(nonmatching_pos + 1), I(pos));
            ret = Text$concat(ret, before_slice, backref_text);
        } else {
            ret = Text$concat(ret, backref_text);
        }

        pos += backref_len;
        nonmatching_pos = pos;
    }
    if (nonmatching_pos < replacement.length) {
        Text_t last_slice = Text$slice(replacement, I(nonmatching_pos + 1), I(replacement.length));
        ret = Text$concat(ret, last_slice);
    }
    return ret;
}

static Text_t Pattern$replace(Text_t text, Text_t pattern, Text_t replacement, Text_t backref_pat, bool recursive) {
    Text_t ret = EMPTY_TEXT;

    int32_t first_grapheme = Text$get_grapheme(pattern, 0);
    bool find_first = (first_grapheme != '{' && !uc_is_property((ucs4_t)first_grapheme, UC_PROPERTY_QUOTATION_MARK)
                       && !uc_is_property((ucs4_t)first_grapheme, UC_PROPERTY_PAIRED_PUNCTUATION));

    Text_t entries[2] = {pattern, replacement};
    List_t replacements = {
        .data = entries,
        .length = 1,
        .stride = sizeof(entries),
    };

    TextIter_t text_state = NEW_TEXT_ITER_STATE(text);
    int64_t nonmatching_pos = 0;
    for (int64_t pos = 0; pos < text.length;) {
        // Optimization: quickly skip ahead to first char in pattern:
        if (find_first) {
            while (pos < text.length && Text$get_grapheme_fast(&text_state, pos) != first_grapheme)
                ++pos;
        }

        capture_t captures[MAX_BACKREFS] = {};
        int64_t match_len = match(text, pos, pattern, 0, captures, 1);
        if (match_len < 0) {
            pos += 1;
            continue;
        }
        captures[0] = (capture_t){
            .index = pos,
            .length = match_len,
            .occupied = true,
            .recursive = false,
        };

        Text_t replacement_text =
            apply_backrefs(text, recursive ? replacements : (List_t){}, replacement, backref_pat, captures);
        if (pos > nonmatching_pos) {
            Text_t before_slice = Text$slice(text, I(nonmatching_pos + 1), I(pos));
            ret = Text$concat(ret, before_slice, replacement_text);
        } else {
            ret = Text$concat(ret, replacement_text);
        }
        nonmatching_pos = pos + match_len;
        pos += MAX(match_len, 1);
    }
    if (nonmatching_pos < text.length) {
        Text_t last_slice = Text$slice(text, I(nonmatching_pos + 1), I(text.length));
        ret = Text$concat(ret, last_slice);
    }
    return ret;
}

static Text_t Pattern$trim(Text_t text, Text_t pattern, bool trim_left, bool trim_right) {
    int64_t first = 0, last = text.length - 1;
    if (trim_left) {
        int64_t match_len = match(text, 0, pattern, 0, NULL, 0);
        if (match_len > 0) first = match_len;
    }

    if (trim_right) {
        for (int64_t i = text.length - 1; i >= first; i--) {
            int64_t match_len = match(text, i, pattern, 0, NULL, 0);
            if (match_len > 0 && i + match_len == text.length) last = i - 1;
        }
    }
    return Text$slice(text, I(first + 1), I(last + 1));
}

static Text_t Pattern$map(Text_t text, Text_t pattern, Closure_t fn, bool recursive) {
    Text_t ret = EMPTY_TEXT;

    int32_t first_grapheme = Text$get_grapheme(pattern, 0);
    bool find_first = (first_grapheme != '{' && !uc_is_property((ucs4_t)first_grapheme, UC_PROPERTY_QUOTATION_MARK)
                       && !uc_is_property((ucs4_t)first_grapheme, UC_PROPERTY_PAIRED_PUNCTUATION));

    TextIter_t text_state = NEW_TEXT_ITER_STATE(text);
    int64_t nonmatching_pos = 0;

    Text_t (*text_mapper)(PatternMatch, void *) = fn.fn;
    for (int64_t pos = 0; pos < text.length; pos++) {
        // Optimization: quickly skip ahead to first char in pattern:
        if (find_first) {
            while (pos < text.length && Text$get_grapheme_fast(&text_state, pos) != first_grapheme)
                ++pos;
        }

        capture_t captures[MAX_BACKREFS] = {};
        int64_t match_len = match(text, pos, pattern, 0, captures, 0);
        if (match_len < 0) continue;

        PatternMatch m = {
            .text = Text$slice(text, I(pos + 1), I(pos + match_len)),
            .index = I(pos + 1),
            .captures = {},
        };
        for (int i = 0; captures[i].occupied; i++) {
            Text_t capture = Text$slice(text, I(captures[i].index + 1), I(captures[i].index + captures[i].length));
            if (recursive) capture = Pattern$map(capture, pattern, fn, recursive);
            List$insert(&m.captures, &capture, I(0), sizeof(Text_t));
        }

        Text_t replacement = text_mapper(m, fn.userdata);
        if (pos > nonmatching_pos) {
            Text_t before_slice = Text$slice(text, I(nonmatching_pos + 1), I(pos));
            ret = Text$concat(ret, before_slice, replacement);
        } else {
            ret = Text$concat(ret, replacement);
        }
        nonmatching_pos = pos + match_len;
        pos += (match_len - 1);
    }
    if (nonmatching_pos < text.length) {
        Text_t last_slice = Text$slice(text, I(nonmatching_pos + 1), I(text.length));
        ret = Text$concat(ret, last_slice);
    }
    return ret;
}

static void Pattern$each(Text_t text, Text_t pattern, Closure_t fn, bool recursive) {
    int32_t first_grapheme = Text$get_grapheme(pattern, 0);
    bool find_first = (first_grapheme != '{' && !uc_is_property((ucs4_t)first_grapheme, UC_PROPERTY_QUOTATION_MARK)
                       && !uc_is_property((ucs4_t)first_grapheme, UC_PROPERTY_PAIRED_PUNCTUATION));

    TextIter_t text_state = NEW_TEXT_ITER_STATE(text);
    void (*action)(PatternMatch, void *) = fn.fn;
    for (int64_t pos = 0; pos < text.length; pos++) {
        // Optimization: quickly skip ahead to first char in pattern:
        if (find_first) {
            while (pos < text.length && Text$get_grapheme_fast(&text_state, pos) != first_grapheme)
                ++pos;
        }

        capture_t captures[MAX_BACKREFS] = {};
        int64_t match_len = match(text, pos, pattern, 0, captures, 0);
        if (match_len < 0) continue;

        PatternMatch m = {
            .text = Text$slice(text, I(pos + 1), I(pos + match_len)),
            .index = I(pos + 1),
            .captures = {},
        };
        for (int i = 0; captures[i].occupied; i++) {
            Text_t capture = Text$slice(text, I(captures[i].index + 1), I(captures[i].index + captures[i].length));
            if (recursive) Pattern$each(capture, pattern, fn, recursive);
            List$insert(&m.captures, &capture, I(0), sizeof(Text_t));
        }

        action(m, fn.userdata);
        pos += (match_len - 1);
    }
}

Text_t replace_list(Text_t text, List_t replacements, Text_t backref_pat, bool recursive) {
    if (replacements.length == 0) return text;

    Text_t ret = EMPTY_TEXT;

    int64_t nonmatch_pos = 0;
    for (int64_t pos = 0; pos < text.length;) {
        // Find the first matching pattern at this position:
        for (int64_t i = 0; i < replacements.length; i++) {
            Text_t pattern = *(Text_t *)(replacements.data + i * replacements.stride);
            capture_t captures[MAX_BACKREFS] = {};
            int64_t len = match(text, pos, pattern, 0, captures, 1);
            if (len < 0) continue;
            captures[0].index = pos;
            captures[0].length = len;

            // If we skipped over some non-matching text before finding a match, insert it here:
            if (pos > nonmatch_pos) {
                Text_t before_slice = Text$slice(text, I(nonmatch_pos + 1), I(pos));
                ret = Text$concat(ret, before_slice);
            }

            // Concatenate the replacement:
            Text_t replacement = *(Text_t *)(replacements.data + i * replacements.stride + sizeof(Text_t));
            Text_t replacement_text =
                apply_backrefs(text, recursive ? replacements : (List_t){}, replacement, backref_pat, captures);
            ret = Text$concat(ret, replacement_text);
            pos += MAX(len, 1);
            nonmatch_pos = pos;
            goto next_pos;
        }

        pos += 1;
    next_pos:
        continue;
    }

    if (nonmatch_pos <= text.length) {
        Text_t last_slice = Text$slice(text, I(nonmatch_pos + 1), I(text.length));
        ret = Text$concat(ret, last_slice);
    }
    return ret;
}

static Text_t Pattern$replace_all(Text_t text, Table_t replacements, Text_t backref_pat, bool recursive) {
    return replace_list(text, replacements.entries, backref_pat, recursive);
}

static List_t Pattern$split(Text_t text, Text_t pattern) {
    if (text.length == 0) // special case
        return (List_t){.length = 0};

    if (pattern.length == 0) // special case
        return Text$clusters(text);

    List_t chunks = {};

    int64_t i = 0;
    for (;;) {
        int64_t len = 0;
        int64_t found = _find(text, pattern, i, text.length - 1, &len, NULL);
        if (found == i && len == 0) found = _find(text, pattern, i + 1, text.length - 1, &len, NULL);
        if (found < 0) break;
        Text_t chunk = Text$slice(text, I(i + 1), I(found));
        List$insert(&chunks, &chunk, I_small(0), sizeof(Text_t));
        i = MAX(found + len, i + 1);
    }

    Text_t last_chunk = Text$slice(text, I(i + 1), I(text.length));
    List$insert(&chunks, &last_chunk, I_small(0), sizeof(Text_t));

    return chunks;
}

typedef struct {
    TextIter_t state;
    int64_t i;
    Text_t pattern;
} split_iter_state_t;

static OptionalText_t next_split(split_iter_state_t *state) {
    Text_t text = state->state.stack[0].text;
    if (state->i >= text.length) {
        if (state->pattern.length > 0 && state->i == text.length) { // special case
            state->i = text.length + 1;
            return EMPTY_TEXT;
        }
        return NONE_TEXT;
    }

    if (state->pattern.length == 0) { // special case
        Text_t ret = Text$cluster(text, I(state->i + 1));
        state->i += 1;
        return ret;
    }

    int64_t start = state->i;
    int64_t len = 0;
    int64_t found = _find(text, state->pattern, start, text.length - 1, &len, NULL);

    if (found == start && len == 0) found = _find(text, state->pattern, start + 1, text.length - 1, &len, NULL);

    if (found >= 0) {
        state->i = MAX(found + len, state->i + 1);
        return Text$slice(text, I(start + 1), I(found));
    } else {
        state->i = state->state.stack[0].text.length + 1;
        return Text$slice(text, I(start + 1), I(text.length));
    }
}

static Closure_t Pattern$by_split(Text_t text, Text_t pattern) {
    return (Closure_t){
        .fn = (void *)next_split,
        .userdata = new (split_iter_state_t, .state = NEW_TEXT_ITER_STATE(text), .i = 0, .pattern = pattern),
    };
}

static Text_t Pattern$escape_text(Text_t text) {
    // TODO: optimize for spans of non-escaped text
    Text_t ret = EMPTY_TEXT;
    TextIter_t state = NEW_TEXT_ITER_STATE(text);
    for (int64_t i = 0; i < text.length; i++) {
        uint32_t g = Text$get_main_grapheme_fast(&state, i);
        if (g == '{') {
            ret = Text$concat(ret, Text("{1{}"));
        } else if (g == '?' || uc_is_property_quotation_mark(g)
                   || (uc_is_property_paired_punctuation(g) && uc_is_property_left_of_pair(g))) {
            ret = Text$concat(ret, Text("{1"), Text$slice(text, I(i + 1), I(i + 1)), Text("}"));
        } else {
            ret = Text$concat(ret, Text$slice(text, I(i + 1), I(i + 1)));
        }
    }
    return ret;
}

static Text_t Pattern$as_text(const void *obj, bool colorize, const TypeInfo_t *info) {
    (void)info;
    if (!obj) return Text("Pattern");

    Text_t pat = *(Text_t *)obj;
    Text_t quote = Pattern$has(pat, Text("/")) && !Pattern$has(pat, Text("|")) ? Text("|") : Text("/");
    return Text$concat(colorize ? Text("\x1b[1m$\033[m") : Text("$"), Text$quoted(pat, colorize, quote));
}
