// Some common parsing utilities

#include <stdint.h>

#include "../unistr-fixed.h"
#include <unictype.h>
#include <uniname.h>

#include "../stdlib/util.h"
#include "errors.h"
#include "utils.h"

static const char *keywords[] = {
    "C_code", "_max_", "_min_",  "and",    "assert", "break", "continue", "defer", "deserialize", "do",
    "else",   "enum",  "extend", "extern", "for",    "func",  "if",       "in",    "lang",        "mod",
    "mod1",   "no",    "none",   "not",    "or",     "pass",  "return",   "skip",  "skip",        "stop",
    "struct", "then",  "unless", "use",    "when",   "while", "xor",      "yes",
};

CONSTFUNC bool is_keyword(const char *word) {
    int64_t lo = 0, hi = sizeof(keywords) / sizeof(keywords[0]) - 1;
    while (lo <= hi) {
        int64_t mid = (lo + hi) / 2;
        int32_t cmp = strcmp(word, keywords[mid]);
        if (cmp == 0) return true;
        else if (cmp > 0) lo = mid + 1;
        else if (cmp < 0) hi = mid - 1;
    }
    return false;
}

size_t some_of(const char **pos, const char *allow) {
    size_t len = strspn(*pos, allow);
    *pos += len;
    return len;
}

size_t some_not(const char **pos, const char *forbid) {
    size_t len = strcspn(*pos, forbid);
    *pos += len;
    return len;
}

size_t spaces(const char **pos) { return some_of(pos, " \t"); }

void whitespace(const char **pos) {
    while (some_of(pos, " \t\r\n") || comment(pos))
        continue;
}

size_t match(const char **pos, const char *target) {
    size_t len = strlen(target);
    if (strncmp(*pos, target, len) != 0) return 0;
    *pos += len;
    return len;
}

bool is_xid_continue_next(const char *pos) {
    ucs4_t point = 0;
    u8_next(&point, (const uint8_t *)pos);
    return uc_is_property_xid_continue(point);
}

size_t match_word(const char **out, const char *word) {
    const char *pos = *out;
    spaces(&pos);
    if (!match(&pos, word) || is_xid_continue_next(pos)) return 0;

    *out = pos;
    return strlen(word);
}

const char *get_word(const char **inout) {
    const char *word = *inout;
    spaces(&word);
    const uint8_t *pos = (const uint8_t *)word;
    ucs4_t point;
    pos = u8_next(&point, pos);
    if (!uc_is_property_xid_start(point) && point != '_') return NULL;

    for (const uint8_t *next; (next = u8_next(&point, pos)); pos = next) {
        if (!uc_is_property_xid_continue(point)) break;
    }
    *inout = (const char *)pos;
    return GC_strndup(word, (size_t)((const char *)pos - word));
}

const char *get_id(const char **inout) {
    const char *pos = *inout;
    const char *word = get_word(&pos);
    if (!word || is_keyword(word)) return NULL;
    *inout = pos;
    return word;
}

PUREFUNC const char *eol(const char *str) { return str + strcspn(str, "\r\n"); }

bool comment(const char **pos) {
    if ((*pos)[0] == '#') {
        *pos += strcspn(*pos, "\r\n");
        return true;
    } else {
        return false;
    }
}

PUREFUNC int64_t get_indent(parse_ctx_t *ctx, const char *pos) {
    int64_t line_num = get_line_number(ctx->file, pos);
    const char *line = get_line(ctx->file, line_num);
    if (line == NULL) {
        return 0;
    } else if (*line == ' ') {
        int64_t spaces = (int64_t)strspn(line, " ");
        if (line[spaces] == '\t')
            parser_err(ctx, line + spaces, line + spaces + 1,
                       "This is a tab following spaces, and you can't mix tabs and spaces");
        return spaces;
    } else if (*line == '\t') {
        int64_t indent = (int64_t)strspn(line, "\t");
        if (line[indent] == ' ')
            parser_err(ctx, line + indent, line + indent + 1,
                       "This is a space following tabs, and you can't mix tabs and spaces");
        return indent * SPACES_PER_INDENT;
    } else {
        return 0;
    }
}

bool indent(parse_ctx_t *ctx, const char **out) {
    const char *pos = *out;
    int64_t starting_indent = get_indent(ctx, pos);
    whitespace(&pos);
    const char *next_line = get_line(ctx->file, get_line_number(ctx->file, pos));
    if (next_line <= *out) return false;

    if (get_indent(ctx, next_line) != starting_indent + SPACES_PER_INDENT) return false;

    *out = next_line + strspn(next_line, " \t");
    return true;
}

bool newline_with_indentation(const char **out, int64_t target) {
    const char *pos = *out;
    if (*pos == '\r') ++pos;
    if (*pos != '\n') return false;
    ++pos;
    if (*pos == '\r' || *pos == '\n' || *pos == '\0') {
        // Empty line
        *out = pos;
        return true;
    }

    if (*pos == ' ') {
        if ((int64_t)strspn(pos, " ") >= target) {
            *out = pos + target;
            return true;
        }
    } else if ((int64_t)strspn(pos, "\t") * SPACES_PER_INDENT >= target) {
        *out = pos + target / SPACES_PER_INDENT;
        return true;
    }
    return false;
}

//
// Convert an escape sequence like \n to a string
//
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstack-protector"
#endif
const char *unescape(parse_ctx_t *ctx, const char **out) {
    const char **endpos = out;
    const char *escape = *out;
    static const char *unescapes[256] = {['a'] = "\a", ['b'] = "\b", ['e'] = "\x1b", ['f'] = "\f", ['n'] = "\n",
                                         ['r'] = "\r", ['t'] = "\t", ['v'] = "\v",   ['_'] = " "};
    assert(*escape == '\\');
    if (unescapes[(int)escape[1]]) {
        *endpos = escape + 2;
        return GC_strdup(unescapes[(int)escape[1]]);
    } else if (escape[1] == '[') {
        // ANSI Control Sequence Indicator: \033 [ ... m
        size_t len = strcspn(&escape[2], "\r\n]");
        if (escape[2 + len] != ']') parser_err(ctx, escape, escape + 2 + len, "Missing closing ']'");
        *endpos = escape + 3 + len;
        return String("\033[", string_slice(&escape[2], len), "m");
    } else if (escape[1] == '{') {
        // Unicode codepoints by name
        size_t len = strcspn(&escape[2], "\r\n}");
        if (escape[2 + len] != '}') parser_err(ctx, escape, escape + 2 + len, "Missing closing '}'");
        char name[len + 1];
        memcpy(name, &escape[2], len);
        name[len] = '\0';

        if (name[0] == 'U') {
            for (char *p = &name[1]; *p; p++) {
                if (!isxdigit(*p)) goto look_up_unicode_name;
            }
            // Unicode codepoints by hex
            char *endptr = NULL;
            long codepoint = strtol(name + 1, &endptr, 16);
            uint32_t ustr[2] = {codepoint, 0};
            size_t bufsize = 8;
            uint8_t buf[bufsize];
            (void)u32_to_u8(ustr, bufsize, buf, &bufsize);
            *endpos = escape + 3 + len;
            return GC_strndup((char *)buf, bufsize);
        }

    look_up_unicode_name:;

        uint32_t codepoint = unicode_name_character(name);
        if (codepoint == UNINAME_INVALID)
            parser_err(ctx, escape, escape + 3 + len, "Invalid unicode codepoint name: ", quoted(name));
        *endpos = escape + 3 + len;
        char *str = GC_MALLOC_ATOMIC(16);
        size_t u8_len = 16;
        (void)u32_to_u8(&codepoint, 1, (uint8_t *)str, &u8_len);
        str[u8_len] = '\0';
        return str;
    } else if (escape[1] == 'x' && escape[2] && escape[3]) {
        // ASCII 2-digit hex
        char buf[] = {escape[2], escape[3], 0};
        char c = (char)strtol(buf, NULL, 16);
        *endpos = escape + 4;
        return GC_strndup(&c, 1);
    } else if ('0' <= escape[1] && escape[1] <= '7' && '0' <= escape[2] && escape[2] <= '7' && '0' <= escape[3]
               && escape[3] <= '7') {
        char buf[] = {escape[1], escape[2], escape[3], 0};
        char c = (char)strtol(buf, NULL, 8);
        *endpos = escape + 4;
        return GC_strndup(&c, 1);
    } else {
        *endpos = escape + 2;
        return GC_strndup(escape + 1, 1);
    }
}
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

bool match_separator(const char **pos) { // Either comma or newline
    const char *p = *pos;
    int separators = 0;
    for (;;) {
        if (some_of(&p, "\r\n,")) ++separators;
        else if (!comment(&p) && !some_of(&p, " \t")) break;
    }
    if (separators > 0) {
        *pos = p;
        return true;
    } else {
        return false;
    }
}
