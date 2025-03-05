# Moments

Tomo has a builtin datatype for representing a specific single point in time:
`Moment`. This is similar to a "datetime" in other languages, but it does not
represent a locale-specific time with a timezone. A Moment object is internally
represented using a UNIX timestamp in seconds and a number of nanoseconds to
represent sub-second times (in C, the equivalent of `struct timeval`). Moment
values do not represent calendar dates or clock times, they represent an exact
nanosecond-long sliver of time, such as the moment when a file was last
modified on the filesystem or the current moment (`Moment.now()`).

⚠️⚠️⚠️ **WARNING** ⚠️⚠️⚠️ Dates and times are deeply counterintuitive and you should
be extremely cautious when writing code that deals with dates and times. Effort
has been made to ensure that Tomo's `Moment` code uses standard libraries and
is as correct as possible, but counterintuitive behaviors around time zones,
daylight savings time, leap seconds, and other anomalous time situations can
still cause bugs if you're not extremely careful.

## Syntax

Moment literals can be specified using [ISO
8601](https://en.wikipedia.org/wiki/ISO_8601) syntax with an optional
square-bracket delimited time zone name afterwards. A space may be used instead
of a `T` in the ISO 8601 format for readability, and spaces may come before the
timezone.

```tomo
2024-09-30
2024-09-30T13:57
2024-09-30 13:57
2024-09-30 13:57:01
2024-09-30 13:57:01 +04:00
2024-09-30 13:57:01 [America/New_York]
```

## Time Zones

Because humans are not able to easily understand UNIX timestamps, the default
textual representation of `Moment` objects uses the current locale's
preferred representation of the Moment in the current time zone:

```tomo
>> Moment.now()
= Sun Sep 29 18:20:12 2024 EDT
```

For various methods, it is assumed by default that users wish to perform
calculations and specify moments using the local time zone and daylight
savings time rules. For example, if a program is running in New York and it is
currently 11pm on February 28th, 2023 (the last day of the month) in local
time, it is assumed that "one month from now" refers to 11pm on March 28th,
2024 in local time, rather than referring to one month from the current UTC
time. In that example, the initial time would be 3am March 1, 2023 in UTC, so
one month later would be 3am April 1, 2023 in UTC, which is which is 11am March
31st in local time. Most users would be unpleasantly surprised to find out that
when it's February 28th in local time, one month later is March 28th until 8pm,
at which point it becomes March 31st!

For various functions where time zones matter, there is an optional `timezone`
argument that, if set, will override the timezone when performing calculations.
If unspecified, it is assumed that the current local timezone should be used.
Time zones are specified by name, such as `America/New_York` or `UTC`.

## Moment Methods

- [`func after(moment: Moment, seconds : Num = 0.0, minutes : Num = 0.0, hours : Num = 0.0, days : Int = 0, weeks : Int = 0, months : Int = 0, years : Int = 0, timezone : Text? = !Text -> Moment)`](#after)
- [`func date(moment: Moment, timezone : Text? = !Text -> Text)`](#date)
- [`func day_of_month(moment: Moment, timezone : Text? = !Text -> Int)`](#day_of_month)
- [`func day_of_week(moment: Moment, timezone : Text? = !Text -> Int)`](#day_of_week)
- [`func day_of_year(moment: Moment, timezone : Text? = !Text -> Int)`](#day_of_year)
- [`func format(moment: Moment, format: Text = "%Y-%m-%dT%H:%M:%S%z", timezone : Text? = !Text -> Text)`](#format)
- [`func from_unix_timestamp(timestamp: Int64 -> Moment)`](#from_unix_timestamp)
- [`func get_local_timezone(->Text)`](#get_local_timezone)
- [`func hour(moment: Moment, timezone : Text? = !Text -> Int)`](#hour)
- [`func hours_till(moment: Moment, then:Moment -> Num)`](#hours_till)
- [`func minute(moment: Moment, timezone : Text? = !Text -> Int)`](#minute)
- [`func minutes_till(moment: Moment, then:Moment -> Num)`](#minutes_till)
- [`func month(moment: Moment, timezone : Text? = !Text -> Int)`](#month)
- [`func nanosecond(moment: Moment, timezone : Text? = !Text -> Int)`](#nanosecond)
- [`func new(year : Int, month : Int, day : Int, hour : Int = 0, minute : Int = 0, second : Num = 0.0 -> Moment)`](#new)
- [`func now(->Moment)`](#now)
- [`func parse(text: Text, format: Text = "%Y-%m-%dT%H:%M:%S%z" -> Moment?)`](#parse)
- [`func relative(moment: Moment, relative_to : Moment = Moment.now(), timezone : Text? = !Text -> Text)`](#relative)
- [`func second(moment: Moment, timezone : Text? = !Text -> Int)`](#second)
- [`func seconds_till(moment: Moment, then:Moment -> Num)`](#seconds_till)
- [`func set_local_timezone(timezone : Text? = !Text -> Void)`](#set_local_timezone)
- [`func time(moment: Moment, seconds : Bool = no, am_pm : Bool = yes, timezone : Text? = !Text -> Text)`](#time)
- [`func unix_timestamp(moment:Moment->Int64)`](#unix_timestamp)

### `after`
Returns a Moment that occurs after the specified time differences. Time
differences may be either positive or negative.

**Note:** time offsets for days, months, weeks, and years do not refer to fixed
time intervals, but are relative to which date they are applied to. For
example, one year from January 1, 2024 is January 1, 2025, which is 366 days
later because 2024 is a leap year. Similarly, adding one month may add anywhere
from 28 to 31 days, depending on the starting month. Days and weeks are
affected by leap seconds. For this reason, `after()` takes an argument,
`timezone` which is used to determine in which timezone the offsets should be
calculated.

```tomo
func after(moment: Moment, seconds : Num = 0.0, minutes : Num = 0.0, hours : Num = 0.0, days : Int = 0, weeks : Int = 0, months : Int = 0, years : Int = 0, timezone : Text? = !Text -> Moment)
```

- `moment`: The moment used as a starting point.
- `seconds` (optional): An amount of seconds to offset the moment (default: 0).
- `minutes` (optional): An amount of minutes to offset the moment (default: 0).
- `hours` (optional): An amount of hours to offset the moment (default: 0).
- `days` (optional): An amount of days to offset the moment (default: 0).
- `weeks` (optional): An amount of weeks to offset the moment (default: 0).
- `months` (optional): An amount of months to offset the moment (default: 0).
- `years` (optional): An amount of years to offset the moment (default: 0).
- `timezone` (optional): If specified, perform perform the calculations in the
  given timezone. If unspecified, the current local timezone will be used.

**Returns:**  
A new `Moment` offset by the given amount.

**Example:**  
```tomo
>> Moment(2024, 9, 29, hour=19):after(days=1, minutes=30)
= Mon Sep 30 19:30:00 2024 EDT
```

---

### `date`
Return a text representation of the moment using the `"%F"` format
specifier, which gives the date in `YYYY-MM-DD` form.

```tomo
func date(moment: Moment, timezone : Text? = !Text -> Text)
```

- `moment`: The moment to get the date from.
- `timezone` (optional): If specified, give the date in the given timezone (otherwise, use the current local timezone).

**Returns:**  
The date in `YYYY-MM-DD` format.

**Example:**  
```tomo
>> Moment(2024, 9, 29):date()
= "2024-09-29"
```

---

### `day_of_month`
Return the integer day of the month (1-31).

```tomo
func day_of_month(moment: Moment, timezone : Text? = !Text -> Int)
```

- `moment`: The moment to get the day of the month from.
- `timezone` (optional): If specified, use the given timezone (otherwise, use the current local timezone).

**Returns:**  
The day of the month as an integer (1-31).

**Example:**  
```tomo
>> Moment(2024, 9, 29):day_of_month()
= 29
```

---

### `day_of_week`
Return the integer day of the week (1-7), where 1 = Sunday, 2 = Monday,
3 = Tuesday, 4 = Wednesday, 5 = Thursday, 6 = Friday, 7 = Saturday.

```tomo
func day_of_week(moment: Moment, timezone : Text? = !Text -> Int)
```

- `moment`: The moment to get the day of the week from.
- `timezone` (optional): If specified, use the given timezone (otherwise, use the current local timezone).

**Returns:**  
The day of the week as an integer (1-7).

**Example:**  
```tomo
>> Moment(2024, 9, 29):day_of_week()
= 1
```

---

### `day_of_year`
Return the integer day of the year (1-366, including leap years).

```tomo
func day_of_year(moment: Moment, timezone : Text? = !Text -> Int)
```

- `moment`: The moment to get the day of the year from.
- `timezone` (optional): If specified, use the given timezone (otherwise, use the current local timezone).

**Returns:**  
The day of the year as an integer (1-366).

**Example:**  
```tomo
>> Moment(2024, 9, 29):day_of_year()
= 272
```

---

### `format`
Using the C-style [`strftime`](https://linux.die.net/man/3/strftime) format
options, return a text representation of the given date in the given format. If
`timezone` is specified, use that timezone instead of the current local
timezone.

```tomo
func format(moment: Moment, format: Text = "%Y-%m-%dT%H:%M:%S%z", timezone : Text? = !Text -> Text)
```

- `moment`: The moment to format.
- `format`: The `strftime` format to use (default: `"%Y-%m-%dT%H:%M:%S%z"`).
- `timezone` (optional): If specified, use the given timezone (otherwise, use the current local timezone).

**Returns:**  
Nothing.

**Example:**  
```tomo
>> Moment(2024, 9, 29):format("%A")
= "Sunday"
```

---

### `from_unix_timestamp`
Return a moment object that represents the same moment in time as
the given UNIX epoch timestamp (seconds since January 1, 1970 UTC).

```tomo
func from_unix_timestamp(timestamp: Int64 -> Moment)
```

- `timestamp`: The UNIX timestamp.

**Returns:**  
A `Moment` object representing the same moment as the given UNIX timestamp.

**Example:**  
```tomo
# In the New York timezone:
>> Moment.from_unix_timestamp(0)
= Wed Dec 31 19:00:00 1969
```

### `get_local_timezone`
Get the local timezone's name (e.g. `America/New_York` or `UTC`. By default,
this value is read from `/etc/localtime`, however, this can be overridden by
calling `Moment.set_local_timezone(...)`.

```tomo
func get_local_timezone(->Text)
```

None.

**Returns:**
The name of the current local timezone.

**Example:**  
```tomo
>> Moment.get_local_timezone()
= "America/New_York"
```

---

### `hour`
Return the hour of the day as an integer (1-24).

```tomo
func hour(moment: Moment, timezone : Text? = !Text -> Int)
```

- `moment`: The moment to get the hour from.
- `timezone` (optional): If specified, use the given timezone (otherwise, use the current local timezone).

**Returns:**  
The hour of the day as an integer (1-24).

**Example:**  
```tomo
>> Moment(2024, 9, 29, 11, 59):hour()
= 11
```

---

### `hours_till`
Return the number of hours until a given moment.

```tomo
func hours_till(moment: Moment, then:Moment -> Num)
```

- `moment`: The starting point moment.
- `then`: Another moment that we want to calculate the time offset from (in hours).

**Returns:**
The number of hours (possibly fractional, possibly negative) until the given time.

**Example:**  
```tomo
the_future := now():after(hours=1, minutes=30)
>> now():hours_till(the_future)
= 1.5
```

---

### `minute`
Return the minute of the day as an integer (0-59).

```tomo
func minute(moment: Moment, timezone : Text? = !Text -> Int)
```

- `moment`: The moment to get the minute from.
- `timezone` (optional): If specified, use the given timezone (otherwise, use the current local timezone).

**Returns:**  
The minute of the hour as an integer (0-59).

**Example:**  
```tomo
>> Moment(2024, 9, 29, 11, 59):minute()
= 59
```

---

### `minutes_till`
Return the number of minutes until a given moment.

```tomo
func minutes_till(moment: Moment, then:Moment -> Num)
```

- `moment`: The starting point moment.
- `then`: Another moment that we want to calculate the time offset from (in minutes).

**Returns:**
The number of minutes (possibly fractional, possibly negative) until the given time.

**Example:**  
```tomo
the_future := now():after(minutes=1, seconds=30)
>> now():minutes_till(the_future)
= 1.5
```

---

### `month`
Return the month of the year as an integer (1-12).

```tomo
func month(moment: Moment, timezone : Text? = !Text -> Int)
```

- `moment`: The moment to get the month from.
- `timezone` (optional): If specified, use the given timezone (otherwise, use the current local timezone).

**Returns:**  
The month of the year as an integer (1-12).

**Example:**  
```tomo
>> Moment(2024, 9, 29, 11, 59):month()
= 9
```

---

### `nanosecond`
Return the nanosecond of the second as an integer (0-999,999,999).

```tomo
func nanosecond(moment: Moment, timezone : Text? = !Text -> Int)
```

- `moment`: The moment to get the nanosecond from.
- `timezone` (optional): If specified, use the given timezone (otherwise, use the current local timezone).

**Returns:**  
The nanosecond of the second as an integer (0-999,999,999).

**Example:**  
```tomo
>> Moment(2024, 9, 29, 11, 59):month()
= 9
```

---

### `new`
Return a new `Moment` object representing the given time parameters expressed
in local time. This function is the same as calling `Moment` directly as a
constructor.

```tomo
func new(year : Int, month : Int, day : Int, hour : Int = 0, minute : Int = 0, second : Num = 0.0 -> Moment)
```

- `year`: The year.
- `month`: The month of the year (1-12).
- `day`: The day of the month (1-31).
- `hour`: The hour of the day (0-23) (default: 0).
- `minute`: The minute of the hour (0-59) (default: 0).
- `second`: The second of the minute (0-59) (default: 0.0).

**Returns:**
A `Moment` representing the given information in local time. If the given
parameters exceed reasonable bounds, the time values will wrap around. For
example, `Moment.new(..., hour=3, minute=65)` is the same as
`Moment.new(..., hour=4, minute=5)`. If any arguments cannot fit in a 32-bit
integer, an error will be raised.

**Example:**  
```tomo
>> Moment.new(2024, 9, 29)
= Mon Sep 30 00:00:00 2024 EDT

# March 1642, 2020:
>> Moment(2020, 4, 1643)
= Sat Sep 28 00:00:00 2024 EDT
```

---

### `now`
Get a `Moment` object representing the current date and time. This function
is the same as the global function `now()`.

```tomo
func now(->Moment)
```

None.

**Returns:**
Returns a `Moment` object representing the current date and time.

**Example:**  
```tomo
>> Moment.now()
= Sun Sep 29 20:22:48 2024 EDT
```

---

### `parse`
Return a new `Moment` object parsed from the given string in the given format,
or `none` if the value could not be successfully parsed.

```tomo
func parse(text: Text, format: Text = "%Y-%m-%dT%H:%M:%S%z" -> Moment?)
```

- `text`: The text to parse.
- `format`: The date format of the text being parsed (see:
  [strptime](https://linux.die.net/man/3/strptime) for more info on this
  format) (default: `"%Y-%m-%dT%H:%M:%S%z"`).

**Returns:**
If the text was successfully parsed according to the given format, return a
`Moment` representing that information. Otherwise, return `none`.

**Example:**  
```tomo
>> Moment.parse("2024-09-29", "%Y-%m-%d")!
= Sun Sep 29 00:00:00 2024 EDT

>> Moment.parse("???", "%Y-%m-%d")
= !Moment
```

---

### `relative`
Return a plain English textual representation of the approximate time difference
between two `Moment`s. For example: `5 minutes ago` or `1 day later`

```tomo
func relative(moment: Moment, relative_to : Moment = Moment.now(), timezone : Text? = !Text -> Text)
```

- `moment`: The moment whose relative time you're getting.
- `relative_to` (optional): The time against which the relative time is calculated (default: `Moment.now()`).
- `timezone` (optional): If specified, perform calculations in the given
  timezone (otherwise, use the current local timezone).

**Returns:**
Return a plain English textual representation of the approximate time
difference between two `Moment`s. For example: `5 minutes ago` or `1 day
later`. Return values are approximate and use only one significant unit of
measure with one significant digit, so a difference of 1.6 days will be
represented as `2 days later`. moments in the past will have the suffix `"
ago"`, while moments in the future will have the suffix `" later"`.

**Example:**  
```tomo
>> now():after(days=2):relative()
= "2 days later"

>> now():after(minutes=-65):relative()
= "1 hour ago"
```

---

### `second`
Return the second of the minute as an integer (0-59).

```tomo
func second(moment: Moment, timezone : Text? = !Text -> Int)
```

- `moment`: The moment to get the second from.
- `timezone` (optional): If specified, use the given timezone (otherwise, use the current local timezone).

**Returns:**  
The second of the hour as an integer (0-59).

**Example:**  
```tomo
>> Moment(2024, 9, 29, 11, 30, 59):second()
= 59
```

---

### `seconds_till`
Return the number of seconds until a given moment.

```tomo
func seconds_till(moment: Moment, then:Moment -> Num)
```

- `moment`: The starting point moment.
- `then`: Another moment that we want to calculate the time offset from (in seconds).

**Returns:**
The number of seconds (possibly fractional, possibly negative) until the given time.

**Example:**  
```tomo
the_future := now():after(seconds=1)
>> now():seconds_till(the_future)
= 1
```

---

### `set_local_timezone`
Set the current local timezone to a given value by name (e.g.
`America/New_York` or `UTC`). The local timezone is used as the default
timezone for performing calculations and constructing `Moment` objects from
component parts. It's also used as the default way that `Moment` objects are
converted to text.

```tomo
func set_local_timezone(timezone : Text? = !Text -> Void)
```

- `timezone` (optional): if specified, set the current local timezone to the
  timezone with the given name. If null, reset the current local timezone to
  the system default (the value referenced in `/etc/localtime`).

**Returns:**
Nothing.

**Example:**  
```tomo
Moment.set_local_timezone("America/Los_Angeles")
```

---

### `time`
Return a text representation of the time component of the given moment.

```tomo
func time(moment: Moment, seconds : Bool = no, am_pm : Bool = yes, timezone : Text? = !Text -> Text)
```

- `moment`: The moment whose time value you want to get.
- `seconds`: Whether to include seconds in the time (default: `no`).
- `am_pm`: Whether to use am/pm in the representation or use a 24-hour clock (default: `yes`).
- `timezone` (optional): If specified, give the time in the given timezone (otherwise, use the current local timezone).

**Returns:**  
A text representation of the time component of the moment.

**Example:**  
```tomo
moment := Moment(2024, 9, 29, hours=13, minutes=59, seconds=30)

>> dt:time()
= "1:59pm"

>> dt:time(am_pm=no)
= "13:59"

>> dt:time(seconds=yes)
= "1:59:30pm"
```

---

### `unix_timestamp`
Get the UNIX timestamp of the given moment (seconds since the UNIX epoch:
January 1, 1970 UTC).

```tomo
func unix_timestamp(moment:Moment->Int64)
```

`moment`: The moment whose UNIX timestamp you want to get.

**Returns:**  
A 64-bit integer representation of the UNIX timestamp.

**Example:**  
```tomo
>> now():unix_timestamp()
= 1727654730[64]
```
