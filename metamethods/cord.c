#include <err.h>
#include <gc/cord.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "../util.h"

typedef CORD (*custom_cord_func)(void *x, bool use_color);

typedef struct {
    void *data;
    int64_t length:42;
    uint8_t free:4;
    bool copy_on_write:1, atomic:1;
    int16_t stride:16;
} generic_array_t;

static CORD vas_cord(void *x, bool use_color, const char **fmt, va_list args)
{
#define CLR(color, str) (use_color ? "\x1b["color"m" str "\x1b[m" : str)
	char c = **fmt;
	++(*fmt);
	switch (c) {
		case '@': case '?': case '&': {
			if (!x)
				return CORD_asprintf(use_color ? "\x1b[34;1m!\x1b[m%r" : "!%r", vas_cord(NULL, use_color, fmt, args));
			void *ptr = *(void**)x;
			char sigil = ptr ? c : '!';
			return CORD_asprintf(use_color ? "\x1b[34;1m%c\x1b[m%r" : "%c%r", sigil, vas_cord(ptr, use_color, fmt, args));
		}
		case 'B': {
			if (!x) return "Bool";
			return *(bool*)x ? CLR("35", "yes") : CLR("35", "no");
		}
		case 'I': {
			size_t bits = va_arg(args, size_t);
			switch (bits) {
				case 64:
					return x ? CORD_asprintf(CLR("35", "%ld"), *(int64_t*)x) : "Int64";
				case 32:
					return x ? CORD_asprintf(CLR("35", "%d"), *(int32_t*)x) : "Int32";
				case 16:
					return x ? CORD_asprintf(CLR("35", "%d"), *(int16_t*)x) : "Int16";
				case 8:
					return x ? CORD_asprintf(CLR("35", "%d"), *(int8_t*)x) : "Int8";
				default: errx(1, "Unsupported Int precision: %ld", bits);
			}
		}
		case 'N': {
			size_t bits = va_arg(args, size_t);
			switch (bits) {
				case 64:
					return x ? CORD_asprintf(CLR("35", "%g"), *(double*)x) : "Num64";
				case 32:
					return x ? CORD_asprintf(CLR("35", "%g"), *(double*)x) : "Num32";
				default: errx(1, "Unsupported Num precision: %ld", bits);
			}
		}
		case 'S': {
			return x ? *(CORD*)x : "Str";
		}
		case '[': {
			if (!x) {
				CORD cord = CORD_asprintf("[%r]", vas_cord(NULL, use_color, fmt, args));
				if (**fmt == ']')
					++(*fmt);
				return cord;
			}
			CORD cord = "[";
			generic_array_t *arr = x;
			for (int64_t i = 0; i < arr->length; i++) {
				const char *item_fmt = *fmt;
				va_list args_copy;
				va_copy(args_copy, args);
				if (i > 0) cord = CORD_cat(cord, ", ");
				CORD item_cord = vas_cord(arr->data + i*arr->stride, use_color, &item_fmt, args_copy);
				cord = CORD_cat(cord, item_cord);
				va_end(args_copy);
			}
			(void)vas_cord(NULL, use_color, fmt, args);
			if (**fmt == ']') ++(*fmt);
			return CORD_cat(cord, "]");
		}
		case '_': {
			custom_cord_func fn = va_arg(args, custom_cord_func);
			return fn(x, use_color);
		}
		case ' ': return "?";
		default: errx(1, "Unsupported format specifier: '%c'", c);
	}
	errx(1, "Unreachable");
#undef CLR
}

public CORD as_cord(void *x, bool use_color, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	CORD ret = vas_cord(x, use_color, &fmt, args);
	va_end(args);
	return ret;
}
