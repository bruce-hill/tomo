// Some common parsing utilities

#include <stdbool.h>

#include "../stdlib/util.h"
#include "context.h"

#define SPACES_PER_INDENT 4

CONSTFUNC bool is_keyword(const char *word);
size_t some_of(const char **pos, const char *allow);
size_t some_not(const char **pos, const char *forbid);
size_t spaces(const char **pos);
void whitespace(const char **pos);
size_t match(const char **pos, const char *target);
size_t match_word(const char **pos, const char *word);
const char *get_word(const char **pos);
const char *get_id(const char **pos);
bool comment(const char **pos);
bool indent(parse_ctx_t *ctx, const char **pos);
const char *eol(const char *str);
PUREFUNC int64_t get_indent(parse_ctx_t *ctx, const char *pos);
const char *unescape(parse_ctx_t *ctx, const char **out);
bool is_xid_continue_next(const char *pos);
bool newline_with_indentation(const char **out, int64_t target);
bool match_separator(const char **pos);
