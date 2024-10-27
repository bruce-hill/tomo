
func main():
    >> 2024-1-1 12:00[America/New_York] == 2024-1-1T09:00[America/Los_Angeles]
    = yes
    >> 2024-1-1 12:00[America/New_York] == DateTime(2024, 1, 1, hour=9, timezone="America/Los_Angeles")
    = yes

    >> t := 2024-1-2 13:45[America/New_York]
    >> t:after(days=40) == 2024-2-11T13:45:00[America/New_York]
    = yes
    >> t:date(timezone="America/New_York")
    = "2024-01-02"

    >> t:time(timezone="America/New_York")
    = "1:45pm"

    >> t:time(am_pm=no, timezone="America/New_York")
    = "13:45"

    >> t:relative(relative_to=t:after(minutes=65))
    = "1 hour ago"

    >> t:seconds_till(t:after(minutes=2))
    = 120

    >> t:minutes_till(t:after(minutes=2))
    = 2

    >> t:hours_till(t:after(minutes=60))
    = 1

    >> t:day_of_week() # 1 = Sun, 2 = Mon, 3 = Tue
    = 3

    >> t:format("%A")
    = "Tuesday"

    >> t:unix_timestamp()
    = 1704221100[64]
    >> t == DateTime.from_unix_timestamp(1704221100[64])
    = yes

    >> t < t:after(minutes=1)
    = yes

    >> t < t:after(seconds=0.1)
    = yes

    >> now()
