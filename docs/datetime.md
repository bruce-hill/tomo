# DateTime

Tomo has a builtin datatype for representing a specific single point in time:
`DateTime`. A DateTime object is internally represented using a UNIX timestamp
in seconds and a number of nanoseconds to represent sub-second times (in C, the
equivalent of `struct timeval`). DateTime values do not represent calendar
dates or clock times, they represent an exact moment in time, such as the
moment when a file was last modified on the filesystem or the current moment
(`DateTime.now()`).

⚠️⚠️⚠️ **WARNING** ⚠️⚠️⚠️ Dates and times are deeply counterintuitive and you should
be extremely cautious when writing code that deals with dates and times. Effort
has been made to ensure that Tomo's `DateTime` code uses standard libraries and
is as correct as possible, but counterintuitive behaviors around time zones,
daylight savings time, leap seconds, and other anomalous time situations can
still cause bugs if you're not extremely careful.

## Time Zones

Because humans are not able to easily understand UNIX timestamps, the default
textual representation of `DateTime` objects uses the current locale's
preferred representation of the DateTime in the current time zone:

```tomo
>> DateTime.now()
= Sun Sep 29 18:20:12 2024 EDT
```

For various methods, it is assumed by default that users wish to perform
calculations and specify datetimes using the local time zone and daylight
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

## DateTime Methods

### `after`

**Description:**  
Returns a DateTime that occurs after the specified time differences. Time
differences may be either positive or negative.

**Note:** time offsets for days, months, weeks, and years do not refer to fixed
time intervals, but are relative to which date they are applied to. For
example, one year from January 1, 2024 is January 1, 2025, which is 366 days
later because 2024 is a leap year. Similarly, adding one month may add anywhere
from 28 to 31 days, depending on the starting month. Days and weeks are
affected by leap seconds. For this reason, `after()` takes an argument,
`timezone` which is used to determine in which timezone the offsets should be
calculated.

**Usage:**  
```markdown
datetime:after(seconds : Num = 0.0, minutes : Num = 0.0, hours : Num = 0.0, days : Int = 0, weeks : Int = 0, months : Int = 0, years : Int = 0, timezone : Text? = !Text) -> DateTime
```

**Parameters:**

- `seconds` (optional): An amount of seconds to offset the datetime (default: 0).
- `minutes` (optional): An amount of minutes to offset the datetime (default: 0).
- `hours` (optional): An amount of hours to offset the datetime (default: 0).
- `days` (optional): An amount of days to offset the datetime (default: 0).
- `weeks` (optional): An amount of weeks to offset the datetime (default: 0).
- `months` (optional): An amount of months to offset the datetime (default: 0).
- `years` (optional): An amount of years to offset the datetime (default: 0).
- `timezone` (optional): If specified, perform perform the calculations in the
  given timezone. If unspecified, the current local timezone will be used.

**Returns:**  
A new `DateTime` offset by the given amount.

**Example:**  
```markdown
>> DateTime(2024, 9, 29, hour=19):after(days=1, minutes=30)
= Mon Sep 30 19:30:00 2024 EDT
```

---

### `date`

**Description:**  
Return a text representation of the datetime using the `"%F"` format
specifier, which gives the date in `YYYY-MM-DD` form.

**Usage:**  
```markdown
datetime:date(timezone : Text? = !Text) -> Text
```

**Parameters:**

- `timezone` (optional): If specified, give the date in the given timezone (otherwise, use the current local timezone).

**Returns:**  
The date in `YYYY-MM-DD` format.

**Example:**  
```markdown
>> DateTime(2024, 9, 29):date()
= "2024-09-29"
```

---

### `format`

**Description:**  
Using the C-style [`strftime`](https://linux.die.net/man/3/strftime) format
options, return a text representation of the given date in the given format. If
`timezone` is specified, use that timezone instead of the current local
timezone.

**Usage:**  
```markdown
datetime:format(format: Text = "%Y-%m-%dT%H:%M:%S%z", timezone : Text? = !Text) -> Text
```

**Parameters:**

- `format`: The `strftime` format to use (default: `"%Y-%m-%dT%H:%M:%S%z"`).
- `timezone` (optional): If specified, use the given timezone (otherwise, use the current local timezone).

**Returns:**  
Nothing.

**Example:**  
```markdown
>> DateTime(2024, 9, 29):format("%A")
= "Sunday"
```

---

### `from_unix_timestamp`

**Description:**  
Return a datetime object that represents the same moment in time as
the given UNIX epoch timestamp (seconds since January 1, 1970 UTC).

**Usage:**  
```markdown
DateTime.from_unix_timestamp(timestamp: Int64) -> DateTime
```

**Parameters:**

- `timestamp`: The UNIX timestamp.

**Returns:**  
A `DateTime` object representing the same moment as the given UNIX timestamp.

**Example:**  
```markdown
# In the New York timezone:
>> DateTime.from_unix_timestamp(0)
= Wed Dec 31 19:00:00 1969
```

---

### `get`

**Description:**  
Get various components of the given datetime object and store them in the
provided optional fields.

**Usage:**  
```markdown
datetime:get(year : &Int? = !&Int, month : &Int? = !&Int, day : &Int? = !&Int, hour : &Int? = !&Int, minute : &Int? = !&Int, second : &Int? = !&Int, nanosecond : &Int? = !&Int, weekday : &Int? = !&Int, timezone : Text? = !Text) -> Void
```

**Parameters:**

- `year`: If non-null, store the year here.
- `month`: If non-null, store the month here (1-12).
- `day`: If non-null, store the day of the month here (1-31).
- `hour`: If non-null, store the hour of the day here (0-23).
- `minute`: If non-null, store the minute of the hour here (0-59).
- `second`: If non-null, store the second of the minute here (0-59).
- `nanosecond`: If non-null, store the nanosecond of the second here (0-1,000,000,000).
- `weekday`: If non-null, store the day of the week here (sunday=1, saturday=7)
- `timezone` (optional): If specified, give values in the given timezone (otherwise, use the current local timezone).

**Returns:**
Nothing.

**Example:**  
```markdown
dt := DateTime(2024, 9, 29)
month := 0
dt:get(month=&month)
>> month
= 9
```

---

### `get_local_timezone`

**Description:**
Get the local timezone's name (e.g. `America/New_York` or `UTC`. By default,
this value is read from `/etc/localtime`, however, this can be overridden by
calling `DateTime.set_local_timezone(...)`.

**Usage:**  
```markdown
DateTime.get_local_timezone() -> Text
```

**Parameters:**

None.

**Returns:**
The name of the current local timezone.

**Example:**  
```markdown
>> DateTime.get_local_timezone()
= "America/New_York"
```

---

### `hours_till`

**Description:**  
Return the number of hours until a given datetime.

**Usage:**  
```markdown
datetime:hours_till(then:DateTime) -> Num
```

**Parameters:**

- `then`: Another datetime that we want to calculate the time offset from (in hours).

**Returns:**
The number of hours (possibly fractional, possibly negative) until the given time.

**Example:**  
```markdown
the_future := now():after(hours=1, minutes=30)
>> now():hours_till(the_future)
= 1.5
```

---

### `minutes_till`

**Description:**  
Return the number of minutes until a given datetime.

**Usage:**  
```markdown
datetime:minutes_till(then:DateTime) -> Num
```

**Parameters:**

- `then`: Another datetime that we want to calculate the time offset from (in minutes).

**Returns:**
The number of minutes (possibly fractional, possibly negative) until the given time.

**Example:**  
```markdown
the_future := now():after(minutes=1, seconds=30)
>> now():minutes_till(the_future)
= 1.5
```

---

### `new`

**Description:**  
Return a new `DateTime` object representing the given time parameters expressed
in local time. This function is the same as calling `DateTime` directly as a
constructor.

**Usage:**  
```markdown
DateTime.new(year : Int, month : Int, day : Int, hour : Int = 0, minute : Int = 0, second : Num = 0.0) -> DateTime
```

**Parameters:**

- `year`: The year.
- `month`: The month of the year (1-12).
- `day`: The day of the month (1-31).
- `hour`: The hour of the day (0-23) (default: 0).
- `minute`: The minute of the hour (0-59) (default: 0).
- `second`: The second of the minute (0-59) (default: 0.0).

**Returns:**
A `DateTime` representing the given information in local time. If the given
parameters exceed reasonable bounds, the time values will wrap around. For
example, `DateTime.new(..., hour=3, minute=65)` is the same as
`DateTime.new(..., hour=4, minute=5)`. If any arguments cannot fit in a 32-bit
integer, an error will be raised.

**Example:**  
```markdown
>> DateTime.new(2024, 9, 29)
= Mon Sep 30 00:00:00 2024 EDT

# March 1642, 2020:
>> DateTime(2020, 4, 1643)
= Sat Sep 28 00:00:00 2024 EDT
```

---

### `now`

**Description:**
Get a `DateTime` object representing the current date and time. This function
is the same as the global function `now()`.

**Usage:**  
```markdown
DateTime.now() -> DateTime
```

**Parameters:**

None.

**Returns:**
Returns a `DateTime` object representing the current date and time.

**Example:**  
```markdown
>> DateTime.now()
= Sun Sep 29 20:22:48 2024 EDT
```

---

### `parse`

**Description:**  
Return a new `DateTime` object parsed from the given string in the given format,
or a null value if the value could not be successfully parsed.

**Usage:**  
```markdown
DateTime.parse(text: Text, format: Text = "%Y-%m-%dT%H:%M:%S%z") -> DateTime?
```

**Parameters:**

- `text`: The text to parse.
- `format`: The date format of the text being parsed (see:
  [strptime](https://linux.die.net/man/3/strptime) for more info on this
  format) (default: `"%Y-%m-%dT%H:%M:%S%z"`).

**Returns:**
If the text was successfully parsed according to the given format, return a
`DateTime` representing that information. Otherwise, return a null value.

**Example:**  
```markdown
>> DateTime.parse("2024-09-29", "%Y-%m-%d")!
= Sun Sep 29 00:00:00 2024 EDT

>> DateTime.parse("???", "%Y-%m-%d")
= !DateTime
```

---

### `relative`

**Description:**  
Return a plain English textual representation of the approximate time difference
between two `DateTime`s. For example: `5 minutes ago` or `1 day later`

**Usage:**  
```markdown
datetime:relative(relative_to : DateTime = DateTime.now(), timezone : Text? = !Text) -> Text
```

**Parameters:**

- `relative_to` (optional): The time against which the relative time is calculated (default: `DateTime.now()`).
- `timezone` (optional): If specified, perform calculations in the given
  timezone (otherwise, use the current local timezone).

**Returns:**
Return a plain English textual representation of the approximate time
difference between two `DateTime`s. For example: `5 minutes ago` or `1 day
later`. Return values are approximate and use only one significant unit of
measure with one significant digit, so a difference of 1.6 days will be
represented as `2 days later`. Datetimes in the past will have the suffix `"
ago"`, while datetimes in the future will have the suffix `" later"`.

**Example:**  
```markdown
>> now():after(days=2):relative()
= "2 days later"

>> now():after(minutes=-65):relative()
= "1 hour ago"
```

---

### `seconds_till`

**Description:**  
Return the number of seconds until a given datetime.

**Usage:**  
```markdown
datetime:seconds_till(then:DateTime) -> Num
```

**Parameters:**

- `then`: Another datetime that we want to calculate the time offset from (in seconds).

**Returns:**
The number of seconds (possibly fractional, possibly negative) until the given time.

**Example:**  
```markdown
the_future := now():after(seconds=1)
>> now():seconds_till(the_future)
= 1
```

---

### `set_local_timezone`

**Description:**
Set the current local timezone to a given value by name (e.g.
`America/New_York` or `UTC`). The local timezone is used as the default
timezone for performing calculations and constructing `DateTime` objects from
component parts. It's also used as the default way that `DateTime` objects are
converted to text.

**Usage:**  
```markdown
DateTime.set_local_timezone(timezone : Text? = !Text) -> Void
```

**Parameters:**

- `timezone` (optional): if specified, set the current local timezone to the
  timezone with the given name. If null, reset the current local timezone to
  the system default (the value referenced in `/etc/localtime`).

**Returns:**
Nothing.

**Example:**  
```markdown
DateTime.set_local_timezone("America/Los_Angeles")
```

---

### `time`

**Description:**  
Return a text representation of the time component of the given datetime.

**Usage:**  
```markdown
datetime:time(seconds : Bool = no, am_pm : Bool = yes, timezone : Text? = !Text) -> Text
```

**Parameters:**

- `seconds`: Whether to include seconds in the time (default: `no`).
- `am_pm`: Whether to use am/pm in the representation or use a 24-hour clock (default: `yes`).
- `timezone` (optional): If specified, give the time in the given timezone (otherwise, use the current local timezone).

**Returns:**  
A text representation of the time component of the datetime.

**Example:**  
```markdown
dt := DateTime(2024, 9, 29, hours=13, minutes=59, seconds=30)

>> dt:time()
= "1:59pm"

>> dt:time(am_pm=no)
= "13:59"

>> dt:time(seconds=yes)
= "1:59:30pm"
```

---

### `unix_timestamp`

**Description:**
Get the UNIX timestamp of the given datetime (seconds since the UNIX epoch:
January 1, 1970 UTC).

**Usage:**  
```markdown
datetime:unix_timestamp() -> Int64
```

**Parameters:**

None.

**Returns:**  
A 64-bit integer representation of the UNIX timestamp.

**Example:**  
```markdown
>> now():unix_timestamp()
= 1727654730[64]
```
