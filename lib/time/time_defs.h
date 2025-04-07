#pragma once
#include <time.h>
#include <sys/time.h>

typedef struct timeval Time;

static OptionalText_t _local_timezone = NONE_TEXT;

static INLINE Text_t num_format(long n, const char *unit)
{
    if (n == 0)
        return Text("now");
    return Text$format((n == 1 || n == -1) ? "%ld %s %s" : "%ld %ss %s", n < 0 ? -n : n, unit, n < 0 ? "ago" : "later");
}

static void set_local_timezone(Text_t tz)
{
    setenv("TZ", Text$as_c_string(tz), 1);
    _local_timezone = tz;
    tzset();
}

#define WITH_TIMEZONE(tz, body) ({ if (tz.length >= 0) { \
        OptionalText_t old_timezone = _local_timezone; \
        set_local_timezone(tz); \
        body; \
        set_local_timezone(old_timezone); \
    } else { \
        body; \
    }})
