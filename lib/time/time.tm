# Time - a module for dealing with dates and times
use <math.h>
use ./time_defs.h

enum Weekday(Sunday, Monday, Tuesday, Wednesday, Thursday, Friday, Saturday)

struct TimeInfo(year,month,day,hour,minute,second,nanosecond:Int, weekday:Weekday, day_of_year:Int, timezone:Text)

struct Time(tv_sec:Int64, tv_usec:Int64; extern)
    func now(->Time)
        return C_code : Time (
            struct timespec ts;
            if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
                fail("Couldn't get the time!");
            (Time){.tv_sec=ts.tv_sec, .tv_usec=ts.tv_nsec/1000}
        )

    func local_timezone(->Text)
        C_code {
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
        }
        return C_code:Text(_local_timezone)

    func set_local_timezone(timezone:Text)
        C_code {
            setenv("TZ", @(CString(timezone)), 1);
            _local_timezone = @timezone;
            tzset();
        }

    func format(t:Time, format="%c", timezone=Time.local_timezone() -> Text)
        return C_code : Text (
            struct tm result;
            time_t time = @t.tv_sec;
            struct tm *final_info;
            WITH_TIMEZONE(@timezone, final_info = localtime_r(&time, &result));
            static char buf[256];
            size_t len = strftime(buf, sizeof(buf), String(@format), final_info);
            Text$from_strn(buf, len)
        )

    func new(year,month,day:Int, hour=0, minute=0, second=0.0, timezone=Time.local_timezone() -> Time)
        return C_code : Time(
            struct tm info = {
                .tm_min=Int32$from_int(@minute, false),
                .tm_hour=Int32$from_int(@hour, false),
                .tm_mday=Int32$from_int(@day, false),
                .tm_mon=Int32$from_int(@month, false) - 1,
                .tm_year=Int32$from_int(@year, false) - 1900,
                .tm_isdst=-1,
            };

            time_t t;
            WITH_TIMEZONE(@timezone, t = mktime(&info));
            (Time){.tv_sec=t + (time_t)@second, .tv_usec=(suseconds_t)(fmod(@second, 1.0) * 1e9)}
        )

    func unix_timestamp(t:Time -> Int64)
        return C_code:Int64((int64_t)@t.tv_sec)

    func from_unix_timestamp(timestamp:Int64 -> Time)
        return C_code:Time((Time){.tv_sec=@timestamp};)

    func seconds_till(t:Time, target:Time -> Num)
        seconds := Num(target.tv_sec - t.tv_sec)
        seconds += 1e-9*Num(target.tv_usec - t.tv_usec)
        return seconds

    func minutes_till(t:Time, target:Time -> Num)
        return t.seconds_till(target)/60.

    func hours_till(t:Time, target:Time -> Num)
        return t.seconds_till(target)/3600.

    func relative(t:Time, relative_to=Time.now(), timezone=Time.local_timezone() -> Text)
        C_code {
            struct tm info = {};
            struct tm relative_info = {};
            WITH_TIMEZONE(@timezone, {
                localtime_r(&@t.tv_sec, &info);
                localtime_r(&@relative_to.tv_sec, &relative_info);
            });
            double second_diff = @(relative_to.seconds_till(t));
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
        fail("Unreachable")

    func time(t:Time, seconds=no, am_pm=yes, timezone=Time.local_timezone() -> Text)
        time := if seconds and am_pm
            t.format("%l:%M:%S%P")
        else if seconds and not am_pm
            t.format("%T")
        else if not seconds and am_pm
            t.format("%l:%M%P")
        else
            t.format("%H:%M")
        return time.trim()

    func date(t:Time, timezone=Time.local_timezone() -> Text)
        return t.format("%F")

    func info(t:Time, timezone=Time.local_timezone() -> TimeInfo)
        ret : TimeInfo
        C_code {
            struct tm info = {};
            WITH_TIMEZONE(@timezone, localtime_r(&@t.tv_sec, &info));
            @ret.year = I(info.tm_year + 1900);
            @ret.month = I(info.tm_mon + 1);
            @ret.day = I(info.tm_mday);
            @ret.hour = I(info.tm_hour);
            @ret.minute = I(info.tm_min);
            @ret.second = I(info.tm_sec);
            @ret.nanosecond = I(@t.tv_usec);
            @ret.weekday = info.tm_wday + 1;
            @ret.day_of_year = I(info.tm_yday);
            @ret.timezone = @timezone;
        }
        return ret

    func after(t:Time, seconds=0.0, minutes=0.0, hours=0.0, days=0, weeks=0, months=0, years=0, timezone=Time.local_timezone() -> Time)
        return C_code : Time (
            double offset = @seconds + 60.*@minutes + 3600.*@hours ;
            @t.tv_sec += (time_t)offset;

            struct tm info = {};
            WITH_TIMEZONE(@timezone, localtime_r(&@t.tv_sec, &info));

            info.tm_mday += Int32$from_int(@days, false) + 7*Int32$from_int(@weeks, false);
            info.tm_mon += Int32$from_int(@months, false);
            info.tm_year += Int32$from_int(@years, false);

            time_t t = mktime(&info);
            (Time){
                .tv_sec=t,
                .tv_usec=@t.tv_usec + (suseconds_t)(fmod(offset, 1.0) * 1e9),
            }
        )

    func parse(text:Text, format="%Y-%m-%dT%H:%M:%S%z", timezone=Time.local_timezone() -> Time?)
        ret : Time?
        C_code {
            struct tm info = {.tm_isdst=-1};
            const char *str = Text$as_c_string(@text);
            const char *fmt = Text$as_c_string(@format);
            if (strstr(fmt, "%Z"))
                fail("The %Z specifier is not supported for time parsing!");

            char *invalid;
            WITH_TIMEZONE(@timezone, invalid = strptime(str, fmt, &info));
            if (!invalid || invalid[0] != '\0') {
                @ret.is_none = true;
            } else {
                long offset = info.tm_gmtoff; // Need to cache this because mktime() mutates it to local tz >:(
                time_t t;
                WITH_TIMEZONE(@timezone, t = mktime(&info));
                @ret.value.tv_sec = t + offset - info.tm_gmtoff;
            }
        }
        return ret

func _run_tests()
    >> Time.now().format()
    >> Time.set_local_timezone("Europe/Paris")
    >> Time.now().format()
    >> Time.set_local_timezone("America/New_York")
    >> Time.now().format()
    # >> Time.now().format(timezone="Europe/Paris")
    # >> Time.now().format()
    # >> Time.now().format("%Y-%m-%d")
    # >> Time.new(2023, 11, 5).format()
    # >> Time.local_timezone()

    # >> Time.new(2023, 11, 5).seconds_till(Time.now())
    # >> Time.new(2023, 11, 5).relative()

    # >> Time.now().info()
    # >> Time.now().time()
    # >> Time.now().date()

    # >> Time.parse("2023-11-05 01:01", "%Y-%m-%d %H:%M")
    # >> Time.parse("2023-11-05 01:01", "%Y-%m-%d %H:%M", timezone="Europe/Paris")

func main()
    _run_tests()
