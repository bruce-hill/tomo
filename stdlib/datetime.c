// DateTime methods/type info
#include <ctype.h>
#include <gc.h>
#include <err.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "datatypes.h"
#include "datetime.h"
#include "optionals.h"
#include "patterns.h"
#include "stdlib.h"
#include "text.h"
#include "util.h"

static OptionalText_t _local_timezone = NULL_TEXT;

public Text_t DateTime$as_text(const DateTime_t *dt, bool colorize, const TypeInfo *type)
{
    (void)type;
    if (!dt)
        return Text("DateTime");

    struct tm info;
    struct tm *final_info = localtime_r(&dt->tv_sec, &info);
    static char buf[256];
    size_t len = strftime(buf, sizeof(buf), "%c %Z", final_info);
    Text_t text = Text$format("%.*s", (int)len, buf);
    if (colorize)
        text = Text$concat(Text("\x1b[36m"), text, Text("\x1b[m"));
    return text;
}

PUREFUNC public int32_t DateTime$compare(const DateTime_t *a, const DateTime_t *b, const TypeInfo *type)
{
    (void)type;
    if (a->tv_sec != b->tv_sec)
        return (a->tv_sec > b->tv_sec) - (a->tv_sec < b->tv_sec);
    return (a->tv_usec > b->tv_usec) - (a->tv_usec < b->tv_usec);
}

public DateTime_t DateTime$now(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        fail("Couldn't get the time!");
    return (DateTime_t){.tv_sec=ts.tv_sec, .tv_usec=ts.tv_nsec};
}

public DateTime_t DateTime$new(Int_t year, Int_t month, Int_t day, Int_t hour, Int_t minute, double second, OptionalText_t timezone)
{
    struct tm info = {
        .tm_min=Int_to_Int32(minute, false),
        .tm_hour=Int_to_Int32(hour, false),
        .tm_mday=Int_to_Int32(day, false),
        .tm_mon=Int_to_Int32(month, false) - 1,
        .tm_year=Int_to_Int32(year, false) - 1900,
        .tm_isdst=-1,
    };

    time_t t;
    if (timezone.length >= 0) {
        OptionalText_t old_timezone = _local_timezone;
        DateTime$set_local_timezone(timezone);
        t = mktime(&info);
        DateTime$set_local_timezone(old_timezone);
    } else {
        t = mktime(&info);
    }
    return (DateTime_t){.tv_sec=t + (time_t)second, .tv_usec=(suseconds_t)(fmod(second, 1.0) * 1e9)};
}

public DateTime_t DateTime$after(DateTime_t dt, double seconds, double minutes, double hours, Int_t days, Int_t weeks, Int_t months, Int_t years, OptionalText_t timezone)
{
    double offset = seconds + 60.*minutes + 3600.*hours;
    dt.tv_sec += (time_t)offset;

    struct tm info = {};
    if (timezone.length >= 0) {
        OptionalText_t old_timezone = _local_timezone;
        DateTime$set_local_timezone(timezone);
        localtime_r(&dt.tv_sec, &info);
        DateTime$set_local_timezone(old_timezone);
    } else {
        localtime_r(&dt.tv_sec, &info);
    }

    info.tm_mday += Int_to_Int32(days, false) + 7*Int_to_Int32(weeks, false);
    info.tm_mon += Int_to_Int32(months, false);
    info.tm_year += Int_to_Int32(years, false);

    time_t t = mktime(&info);
    return (DateTime_t){
        .tv_sec=t,
        .tv_usec=dt.tv_usec + (suseconds_t)(fmod(offset, 1.0) * 1e9),
    };
}

CONSTFUNC public double DateTime$seconds_till(DateTime_t now, DateTime_t then)
{
    return (double)(then.tv_sec - now.tv_sec) + 1e-9*(double)(then.tv_usec - now.tv_usec);
}

CONSTFUNC public double DateTime$minutes_till(DateTime_t now, DateTime_t then)
{
    return DateTime$seconds_till(now, then)/60.;
}

CONSTFUNC public double DateTime$hours_till(DateTime_t now, DateTime_t then)
{
    return DateTime$seconds_till(now, then)/3600.;
}

public void DateTime$get(
    DateTime_t dt, Int_t *year, Int_t *month, Int_t *day, Int_t *hour, Int_t *minute, Int_t *second,
    Int_t *nanosecond, Int_t *weekday, OptionalText_t timezone)
{
    struct tm info = {};

    if (timezone.length >= 0) {
        OptionalText_t old_timezone = _local_timezone;
        DateTime$set_local_timezone(timezone);
        localtime_r(&dt.tv_sec, &info);
        DateTime$set_local_timezone(old_timezone);
    } else {
        localtime_r(&dt.tv_sec, &info);
    }

    if (year) *year = I(info.tm_year + 1900);
    if (month) *month = I(info.tm_mon + 1);
    if (day) *day = I(info.tm_mday);
    if (hour) *hour = I(info.tm_hour);
    if (minute) *minute = I(info.tm_min);
    if (second) *second = I(info.tm_sec);
    if (nanosecond) *nanosecond = I(dt.tv_usec);
    if (weekday) *weekday = I(info.tm_wday + 1);
}

public Text_t DateTime$format(DateTime_t dt, Text_t fmt, OptionalText_t timezone)
{
    struct tm info;
    if (timezone.length >= 0) {
        OptionalText_t old_timezone = _local_timezone;
        DateTime$set_local_timezone(timezone);
        localtime_r(&dt.tv_sec, &info);
        DateTime$set_local_timezone(old_timezone);
    } else {
        localtime_r(&dt.tv_sec, &info);
    }
    static char buf[256];
    size_t len = strftime(buf, sizeof(buf), Text$as_c_string(fmt), &info);
    return Text$format("%.*s", (int)len, buf);
}

public Text_t DateTime$date(DateTime_t dt, OptionalText_t timezone)
{
    return DateTime$format(dt, Text("%F"), timezone);
}

public Text_t DateTime$time(DateTime_t dt, bool seconds, bool am_pm, OptionalText_t timezone)
{
    Text_t text;
    if (seconds)
        text = DateTime$format(dt, am_pm ? Text("%l:%M:%S%P") : Text("%T"), timezone);
    else
        text = DateTime$format(dt, am_pm ? Text("%l:%M%P") : Text("%H:%M"), timezone);
    return Text$trim(text, Pattern(" "), true, true);
}

public OptionalDateTime_t DateTime$parse(Text_t text, Text_t format)
{
    struct tm info = {.tm_isdst=-1};
    const char *str = Text$as_c_string(text);
    const char *fmt = Text$as_c_string(format);
    if (strstr(fmt, "%Z"))
        fail("The %%Z specifier is not supported for time parsing!");

    char *invalid = strptime(str, fmt, &info);
    if (!invalid || invalid[0] != '\0')
        return NULL_DATETIME;

    long offset = info.tm_gmtoff; // Need to cache this because mktime() mutates it to local timezone >:(
    time_t t = mktime(&info);
    return (DateTime_t){.tv_sec=t + offset - info.tm_gmtoff};
}

static inline Text_t num_format(long n, const char *unit)
{
    if (n == 0)
        return Text("now");
    return Text$format((n == 1 || n == -1) ? "%ld %s %s" : "%ld %ss %s", n < 0 ? -n : n, unit, n < 0 ? "ago" : "later");
}

public Text_t DateTime$relative(DateTime_t dt, DateTime_t relative_to, OptionalText_t timezone)
{
    struct tm info = {};
    struct tm relative_info = {};

    if (timezone.length >= 0) {
        OptionalText_t old_timezone = _local_timezone;
        DateTime$set_local_timezone(timezone);
        localtime_r(&dt.tv_sec, &info);
        localtime_r(&relative_to.tv_sec, &relative_info);
        DateTime$set_local_timezone(old_timezone);
    } else {
        localtime_r(&dt.tv_sec, &info);
        localtime_r(&relative_to.tv_sec, &relative_info);
    }

    double second_diff = DateTime$seconds_till(relative_to, dt);
    if (info.tm_year != relative_info.tm_year && fabs(second_diff) > 365.*24.*60.*60.)
        return num_format((long)info.tm_year - (long)relative_info.tm_year, "year");
    else if (info.tm_mon != relative_info.tm_mon && fabs(second_diff) > 31.*24.*60.*60.)
        return num_format(12*((long)info.tm_year - (long)relative_info.tm_year) + (long)info.tm_mon - (long)relative_info.tm_mon, "month");
    else if (info.tm_yday != relative_info.tm_yday && fabs(second_diff) > 24.*60.*60.)
        return num_format(round(second_diff/(24.*60.*60.)), "day");
    else if (info.tm_hour != relative_info.tm_hour && fabs(second_diff) > 60.*60.)
        return num_format(round(second_diff/(60.*60.)), "hour");
    else if (info.tm_min != relative_info.tm_min && fabs(second_diff) > 60.)
        return num_format(round(second_diff/(60.)), "minute");
    else {
        if (fabs(second_diff) < 1e-6)
            return num_format((long)(second_diff*1e9), "nanosecond");
        else if (fabs(second_diff) < 1e-3)
            return num_format((long)(second_diff*1e6), "microsecond");
        else if (fabs(second_diff) < 1.0)
            return num_format((long)(second_diff*1e3), "millisecond");
        else
            return num_format((long)(second_diff), "second");
    }
}

CONSTFUNC public Int64_t DateTime$unix_timestamp(DateTime_t dt)
{
    return (Int64_t)dt.tv_sec;
}

CONSTFUNC public DateTime_t DateTime$from_unix_timestamp(Int64_t timestamp)
{
    return (DateTime_t){.tv_sec=(time_t)timestamp};
}

public void DateTime$set_local_timezone(OptionalText_t timezone)
{
    if (timezone.length >= 0) {
        setenv("TZ", Text$as_c_string(timezone), 1);
    } else {
        unsetenv("TZ");
    }
    _local_timezone = timezone;
    tzset();
}

public Text_t DateTime$get_local_timezone(void)
{
    if (_local_timezone.length < 0) {
        static char buf[PATH_MAX];
        ssize_t len = readlink("/etc/localtime", buf, sizeof(buf));
        if (len < 0)
            fail("Could not get local timezone!");

        char *zoneinfo = strstr(buf, "/zoneinfo/");
        if (zoneinfo)
            _local_timezone = Text$from_str(zoneinfo + strlen("/zoneinfo/"));
        else
            fail("Could not resolve local timezone!");
    }
    return _local_timezone;
}

public const TypeInfo DateTime$info = {
    .size=sizeof(DateTime_t),
    .align=__alignof__(DateTime_t),
    .tag=CustomInfo,
    .CustomInfo={
        .as_text=(void*)DateTime$as_text,
        .compare=(void*)DateTime$compare,
    },
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
