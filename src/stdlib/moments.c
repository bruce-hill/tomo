// Moment methods/type info
#include <ctype.h>
#include <gc.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "datatypes.h"
#include "math.h"
#include "moments.h"
#include "optionals.h"
#include "patterns.h"
#include "stdlib.h"
#include "text.h"
#include "util.h"

static OptionalText_t _local_timezone = NONE_TEXT;

#define WITH_TIMEZONE(tz, body) ({ if (tz.length >= 0) { \
        OptionalText_t old_timezone = _local_timezone; \
        Moment$set_local_timezone(tz); \
        body; \
        Moment$set_local_timezone(old_timezone); \
    } else { \
        body; \
    }})

public Text_t Moment$as_text(const void *moment, bool colorize, const TypeInfo_t*)
{
    if (!moment)
        return Text("Moment");

    struct tm info;
    struct tm *final_info = localtime_r(&((Moment_t*)moment)->tv_sec, &info);
    static char buf[256];
    size_t len = strftime(buf, sizeof(buf), "%c %Z", final_info);
    Text_t text = Text$format("%.*s", (int)len, buf);
    if (colorize)
        text = Text$concat(Text("\x1b[36m"), text, Text("\x1b[m"));
    return text;
}

PUREFUNC public int32_t Moment$compare(const void *va, const void *vb, const TypeInfo_t*)
{
    Moment_t *a = (Moment_t*)va, *b = (Moment_t*)vb;
    if (a->tv_sec != b->tv_sec)
        return (a->tv_sec > b->tv_sec) - (a->tv_sec < b->tv_sec);
    return (a->tv_usec > b->tv_usec) - (a->tv_usec < b->tv_usec);
}

CONSTFUNC public bool Moment$is_none(const void *m, const TypeInfo_t*)
{
    return ((Moment_t*)m)->tv_usec < 0;
}

public Moment_t Moment$now(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
        fail("Couldn't get the time!");
    return (Moment_t){.tv_sec=ts.tv_sec, .tv_usec=ts.tv_nsec/1000};
}

public Moment_t Moment$new(Int_t year, Int_t month, Int_t day, Int_t hour, Int_t minute, double second, OptionalText_t tz)
{
    struct tm info = {
        .tm_min=Int32$from_int(minute, false),
        .tm_hour=Int32$from_int(hour, false),
        .tm_mday=Int32$from_int(day, false),
        .tm_mon=Int32$from_int(month, false) - 1,
        .tm_year=Int32$from_int(year, false) - 1900,
        .tm_isdst=-1,
    };

    time_t t;
    WITH_TIMEZONE(tz, t = mktime(&info));
    return (Moment_t){.tv_sec=t + (time_t)second, .tv_usec=(suseconds_t)(fmod(second, 1.0) * 1e9)};
}

public Moment_t Moment$after(Moment_t moment, double seconds, double minutes, double hours, Int_t days, Int_t weeks, Int_t months, Int_t years, OptionalText_t tz)
{
    double offset = seconds + 60.*minutes + 3600.*hours;
    moment.tv_sec += (time_t)offset;

    struct tm info = {};
    WITH_TIMEZONE(tz, localtime_r(&moment.tv_sec, &info));

    info.tm_mday += Int32$from_int(days, false) + 7*Int32$from_int(weeks, false);
    info.tm_mon += Int32$from_int(months, false);
    info.tm_year += Int32$from_int(years, false);

    time_t t = mktime(&info);
    return (Moment_t){
        .tv_sec=t,
        .tv_usec=moment.tv_usec + (suseconds_t)(fmod(offset, 1.0) * 1e9),
    };
}

CONSTFUNC public double Moment$seconds_till(Moment_t now, Moment_t then)
{
    return (double)(then.tv_sec - now.tv_sec) + 1e-9*(double)(then.tv_usec - now.tv_usec);
}

CONSTFUNC public double Moment$minutes_till(Moment_t now, Moment_t then)
{
    return Moment$seconds_till(now, then)/60.;
}

CONSTFUNC public double Moment$hours_till(Moment_t now, Moment_t then)
{
    return Moment$seconds_till(now, then)/3600.;
}

public void Moment$get(
    Moment_t moment, Int_t *year, Int_t *month, Int_t *day, Int_t *hour, Int_t *minute, Int_t *second,
    Int_t *nanosecond, Int_t *weekday, OptionalText_t tz)
{
    struct tm info = {};
    WITH_TIMEZONE(tz, localtime_r(&moment.tv_sec, &info));

    if (year) *year = I(info.tm_year + 1900);
    if (month) *month = I(info.tm_mon + 1);
    if (day) *day = I(info.tm_mday);
    if (hour) *hour = I(info.tm_hour);
    if (minute) *minute = I(info.tm_min);
    if (second) *second = I(info.tm_sec);
    if (nanosecond) *nanosecond = I(moment.tv_usec);
    if (weekday) *weekday = I(info.tm_wday + 1);
}

public Int_t Moment$year(Moment_t moment, OptionalText_t tz)
{
    struct tm info = {};
    WITH_TIMEZONE(tz, localtime_r(&moment.tv_sec, &info));
    return I(info.tm_year + 1900);
}

public Int_t Moment$month(Moment_t moment, OptionalText_t tz)
{
    struct tm info = {};
    WITH_TIMEZONE(tz, localtime_r(&moment.tv_sec, &info));
    return I(info.tm_mon + 1);
}

public Int_t Moment$day_of_week(Moment_t moment, OptionalText_t tz)
{
    struct tm info = {};
    WITH_TIMEZONE(tz, localtime_r(&moment.tv_sec, &info));
    return I(info.tm_wday + 1);
}

public Int_t Moment$day_of_month(Moment_t moment, OptionalText_t tz)
{
    struct tm info = {};
    WITH_TIMEZONE(tz, localtime_r(&moment.tv_sec, &info));
    return I(info.tm_mday);
}

public Int_t Moment$day_of_year(Moment_t moment, OptionalText_t tz)
{
    struct tm info = {};
    WITH_TIMEZONE(tz, localtime_r(&moment.tv_sec, &info));
    return I(info.tm_yday);
}

public Int_t Moment$hour(Moment_t moment, OptionalText_t tz)
{
    struct tm info = {};
    WITH_TIMEZONE(tz, localtime_r(&moment.tv_sec, &info));
    return I(info.tm_hour);
}

public Int_t Moment$minute(Moment_t moment, OptionalText_t tz)
{
    struct tm info = {};
    WITH_TIMEZONE(tz, localtime_r(&moment.tv_sec, &info));
    return I(info.tm_min);
}

public Int_t Moment$second(Moment_t moment, OptionalText_t tz)
{
    struct tm info = {};
    WITH_TIMEZONE(tz, localtime_r(&moment.tv_sec, &info));
    return I(info.tm_sec);
}

public Int_t Moment$microsecond(Moment_t moment, OptionalText_t tz)
{
    (void)tz;
    return I(moment.tv_usec);
}

public Text_t Moment$format(Moment_t moment, Text_t fmt, OptionalText_t tz)
{
    struct tm info;
    WITH_TIMEZONE(tz, localtime_r(&moment.tv_sec, &info));
    static char buf[256];
    size_t len = strftime(buf, sizeof(buf), Text$as_c_string(fmt), &info);
    return Text$format("%.*s", (int)len, buf);
}

public Text_t Moment$date(Moment_t moment, OptionalText_t tz)
{
    return Moment$format(moment, Text("%F"), tz);
}

public Text_t Moment$time(Moment_t moment, bool seconds, bool am_pm, OptionalText_t tz)
{
    Text_t text;
    if (seconds)
        text = Moment$format(moment, am_pm ? Text("%l:%M:%S%P") : Text("%T"), tz);
    else
        text = Moment$format(moment, am_pm ? Text("%l:%M%P") : Text("%H:%M"), tz);
    return Text$trim(text, Pattern(" "), true, true);
}

public OptionalMoment_t Moment$parse(Text_t text, Text_t format)
{
    struct tm info = {.tm_isdst=-1};
    const char *str = Text$as_c_string(text);
    const char *fmt = Text$as_c_string(format);
    if (strstr(fmt, "%Z"))
        fail("The %Z specifier is not supported for time parsing!");

    char *invalid = strptime(str, fmt, &info);
    if (!invalid || invalid[0] != '\0')
        return NONE_MOMENT;

    long offset = info.tm_gmtoff; // Need to cache this because mktime() mutates it to local tz >:(
    time_t t = mktime(&info);
    return (Moment_t){.tv_sec=t + offset - info.tm_gmtoff};
}

static INLINE Text_t num_format(long n, const char *unit)
{
    if (n == 0)
        return Text("now");
    return Text$format((n == 1 || n == -1) ? "%ld %s %s" : "%ld %ss %s", n < 0 ? -n : n, unit, n < 0 ? "ago" : "later");
}

public Text_t Moment$relative(Moment_t moment, Moment_t relative_to, OptionalText_t tz)
{
    struct tm info = {};
    struct tm relative_info = {};
    WITH_TIMEZONE(tz, {
        localtime_r(&moment.tv_sec, &info);
        localtime_r(&relative_to.tv_sec, &relative_info);
    });

    double second_diff = Moment$seconds_till(relative_to, moment);
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

CONSTFUNC public Int64_t Moment$unix_timestamp(Moment_t moment)
{
    return (Int64_t)moment.tv_sec;
}

CONSTFUNC public Moment_t Moment$from_unix_timestamp(Int64_t timestamp)
{
    return (Moment_t){.tv_sec=(time_t)timestamp};
}

public void Moment$set_local_timezone(OptionalText_t tz)
{
    if (tz.length >= 0) {
        setenv("TZ", Text$as_c_string(tz), 1);
    } else {
        unsetenv("TZ");
    }
    _local_timezone = tz;
    tzset();
}

public Text_t Moment$get_local_timezone(void)
{
    if (_local_timezone.length < 0) {
        static char buf[PATH_MAX];
        ssize_t len = readlink("/etc/localtime", buf, sizeof(buf));
        if (len < 0)
            fail("Could not get local tz!");

        char *zoneinfo = strstr(buf, "/zoneinfo/");
        if (zoneinfo)
            _local_timezone = Text$from_str(zoneinfo + strlen("/zoneinfo/"));
        else
            fail("Could not resolve local tz!");
    }
    return _local_timezone;
}

public const TypeInfo_t Moment$info = {
    .size=sizeof(Moment_t),
    .align=__alignof__(Moment_t),
    .metamethods={
        .as_text=Moment$as_text,
        .compare=Moment$compare,
        .is_none=Moment$is_none,
    },
};

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
