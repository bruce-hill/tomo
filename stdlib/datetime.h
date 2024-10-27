#pragma once

// DateTime objects

#include <stdint.h>

#include "datatypes.h"
#include "integers.h"
#include "optionals.h"
#include "types.h"
#include "util.h"

Text_t DateTime$as_text(const DateTime_t *dt, bool colorize, const TypeInfo_t *type);
PUREFUNC int32_t DateTime$compare(const DateTime_t *a, const DateTime_t *b, const TypeInfo_t *type);
DateTime_t DateTime$now(void);
DateTime_t DateTime$new(Int_t year, Int_t month, Int_t day, Int_t hour, Int_t minute, double second, OptionalText_t timezone);
DateTime_t DateTime$after(DateTime_t dt, double seconds, double minutes, double hours, Int_t days, Int_t weeks, Int_t months, Int_t years, OptionalText_t timezone);
CONSTFUNC double DateTime$seconds_till(DateTime_t now, DateTime_t then);
CONSTFUNC double DateTime$minutes_till(DateTime_t now, DateTime_t then);
CONSTFUNC double DateTime$hours_till(DateTime_t now, DateTime_t then);
Int_t DateTime$year(DateTime_t dt, OptionalText_t timezone);
Int_t DateTime$month(DateTime_t dt, OptionalText_t timezone);
Int_t DateTime$day_of_week(DateTime_t dt, OptionalText_t timezone);
Int_t DateTime$day_of_month(DateTime_t dt, OptionalText_t timezone);
Int_t DateTime$day_of_year(DateTime_t dt, OptionalText_t timezone);
Int_t DateTime$hour(DateTime_t dt, OptionalText_t timezone);
Int_t DateTime$minute(DateTime_t dt, OptionalText_t timezone);
Int_t DateTime$second(DateTime_t dt, OptionalText_t timezone);
Int_t DateTime$nanosecond(DateTime_t dt, OptionalText_t timezone);
Text_t DateTime$format(DateTime_t dt, Text_t fmt, OptionalText_t timezone);
Text_t DateTime$date(DateTime_t dt, OptionalText_t timezone);
Text_t DateTime$time(DateTime_t dt, bool seconds, bool am_pm, OptionalText_t timezone);
OptionalDateTime_t DateTime$parse(Text_t text, Text_t format);
Text_t DateTime$relative(DateTime_t dt, DateTime_t relative_to, OptionalText_t timezone);
CONSTFUNC Int64_t DateTime$unix_timestamp(DateTime_t dt);
CONSTFUNC DateTime_t DateTime$from_unix_timestamp(Int64_t timestamp);
void DateTime$set_local_timezone(OptionalText_t timezone);
Text_t DateTime$get_local_timezone(void);

extern const TypeInfo_t DateTime$info;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0

