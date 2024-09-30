// DateTime methods/type info
#include <ctype.h>
#include <gc.h>
#include <err.h>
#include <stdlib.h>
#include <time.h>

#include "datatypes.h"
#include "optionals.h"
#include "patterns.h"
#include "stdlib.h"
#include "text.h"
#include "util.h"

public Text_t DateTime$as_text(const DateTime_t *dt, bool colorize, const TypeInfo *type)
{
    (void)type;
    if (!dt)
        return Text("DateTime");

    struct tm info;
    struct tm *final_info = localtime_r(&dt->tv_sec, &info);
    static char buf[256];
    size_t len = strftime(buf, sizeof(buf), "%c", final_info);
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

public DateTime_t DateTime$new(Int_t year, Int_t month, Int_t day, Int_t hour, Int_t minute, double second)
{
    struct tm info = {
        .tm_min=Int_to_Int32(minute, false),
        .tm_hour=Int_to_Int32(hour, false),
        .tm_mday=Int_to_Int32(day, false),
        .tm_mon=Int_to_Int32(month, false) - 1,
        .tm_year=Int_to_Int32(year, false) - 1900,
        .tm_isdst=-1,
    };
    time_t t = mktime(&info);
    return (DateTime_t){.tv_sec=t + (time_t)second, .tv_usec=(suseconds_t)(fmod(second, 1.0) * 1e9)};
}

public DateTime_t DateTime$after(DateTime_t dt, double seconds, double minutes, double hours, Int_t days, Int_t weeks, Int_t months, Int_t years, bool local_time)
{
    double offset = seconds + 60.*minutes + 3600.*hours;
    dt.tv_sec += (time_t)offset;

    struct tm info = {};
    if (local_time)
        localtime_r(&dt.tv_sec, &info);
    else
        gmtime_r(&dt.tv_sec, &info);
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
    Int_t *nanosecond, Int_t *weekday, bool local_time)
{
    struct tm info = {};
    if (local_time)
        localtime_r(&dt.tv_sec, &info);
    else
        gmtime_r(&dt.tv_sec, &info);

    if (year) *year = I(info.tm_year + 1900);
    if (month) *month = I(info.tm_mon + 1);
    if (day) *day = I(info.tm_mday);
    if (hour) *hour = I(info.tm_hour);
    if (minute) *minute = I(info.tm_min);
    if (second) *second = I(info.tm_sec);
    if (nanosecond) *nanosecond = I(dt.tv_usec);
    if (weekday) *weekday = I(info.tm_wday + 1);
}

public Text_t DateTime$format(DateTime_t dt, Text_t fmt, bool local_time)
{
    struct tm info;
    struct tm *final_info = local_time ? localtime_r(&dt.tv_sec, &info) : gmtime_r(&dt.tv_sec, &info);
    static char buf[256];
    size_t len = strftime(buf, sizeof(buf), Text$as_c_string(fmt), final_info);
    return Text$format("%.*s", (int)len, buf);
}

public Text_t DateTime$date(DateTime_t dt, bool local_time)
{
    return DateTime$format(dt, Text("%F"), local_time);
}

public Text_t DateTime$time(DateTime_t dt, bool seconds, bool am_pm, bool local_time)
{
    Text_t text;
    if (seconds)
        text = DateTime$format(dt, am_pm ? Text("%l:%M:%S%P") : Text("%T"), local_time);
    else
        text = DateTime$format(dt, am_pm ? Text("%l:%M%P") : Text("%H:%M"), local_time);
    return Text$trim(text, Pattern(" "), true, true);
}

public OptionalDateTime_t DateTime$parse(Text_t text, Text_t format)
{
    struct tm info = {.tm_isdst=-1};
    char *invalid = strptime(Text$as_c_string(text), Text$as_c_string(format), &info);
    if (!invalid || invalid[0] != '\0')
        return NULL_DATETIME;

    time_t t = mktime(&info);
    return (DateTime_t){.tv_sec=t};
}

static inline Text_t num_format(long n, const char *unit)
{
    if (n == 0)
        return Text("now");
    return Text$format((n == 1 || n == -1) ? "%ld %s %s" : "%ld %ss %s", n < 0 ? -n : n, unit, n < 0 ? "ago" : "later");
}

public Text_t DateTime$relative(DateTime_t dt, DateTime_t relative_to, bool local_time)
{
    struct tm info = {};
    if (local_time)
        localtime_r(&dt.tv_sec, &info);
    else
        gmtime_r(&dt.tv_sec, &info);

    struct tm relative_info = {};
    if (local_time)
        localtime_r(&relative_to.tv_sec, &relative_info);
    else
        gmtime_r(&relative_to.tv_sec, &relative_info);

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
    return (Int64_t)(dt.tv_sec);
}

CONSTFUNC public DateTime_t DateTime$from_unix_timestamp(Int64_t timestamp)
{
    return (DateTime_t){.tv_sec=(time_t)timestamp};
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
