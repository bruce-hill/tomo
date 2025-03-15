#pragma once

// Moment objects

#include <stdint.h>

#include "datatypes.h"
#include "integers.h"
#include "optionals.h"
#include "types.h"
#include "util.h"

Text_t Moment$as_text(const void *moment, bool colorize, const TypeInfo_t *type);
PUREFUNC int32_t Moment$compare(const void *a, const void *b, const TypeInfo_t *type);
CONSTFUNC public bool Moment$is_none(const void *m, const TypeInfo_t*);
Moment_t Moment$now(void);
Moment_t Moment$new(Int_t year, Int_t month, Int_t day, Int_t hour, Int_t minute, double second, OptionalText_t timezone);
Moment_t Moment$after(Moment_t moment, double seconds, double minutes, double hours, Int_t days, Int_t weeks, Int_t months, Int_t years, OptionalText_t timezone);
CONSTFUNC double Moment$seconds_till(Moment_t now, Moment_t then);
CONSTFUNC double Moment$minutes_till(Moment_t now, Moment_t then);
CONSTFUNC double Moment$hours_till(Moment_t now, Moment_t then);
Int_t Moment$year(Moment_t moment, OptionalText_t timezone);
Int_t Moment$month(Moment_t moment, OptionalText_t timezone);
Int_t Moment$day_of_week(Moment_t moment, OptionalText_t timezone);
Int_t Moment$day_of_month(Moment_t moment, OptionalText_t timezone);
Int_t Moment$day_of_year(Moment_t moment, OptionalText_t timezone);
Int_t Moment$hour(Moment_t moment, OptionalText_t timezone);
Int_t Moment$minute(Moment_t moment, OptionalText_t timezone);
Int_t Moment$second(Moment_t moment, OptionalText_t timezone);
Int_t Moment$microsecond(Moment_t moment, OptionalText_t timezone);
Text_t Moment$format(Moment_t moment, Text_t fmt, OptionalText_t timezone);
Text_t Moment$date(Moment_t moment, OptionalText_t timezone);
Text_t Moment$time(Moment_t moment, bool seconds, bool am_pm, OptionalText_t timezone);
OptionalMoment_t Moment$parse(Text_t text, Text_t format);
Text_t Moment$relative(Moment_t moment, Moment_t relative_to, OptionalText_t timezone);
CONSTFUNC Int64_t Moment$unix_timestamp(Moment_t moment);
CONSTFUNC Moment_t Moment$from_unix_timestamp(Int64_t timestamp);
void Moment$set_local_timezone(OptionalText_t timezone);
Text_t Moment$get_local_timezone(void);

extern const TypeInfo_t Moment$info;

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0

