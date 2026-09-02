# Layout choices the formatter makes for inline C. `tomo format --check`
# compares syntax trees, and reparsing dedents a verbatim block, so a one-line
# block that needlessly grows to three lines passes that check unnoticed. The
# snapshot beside this file pins the layout itself.

func main()
    x := Int64(1)
    # Short enough to stay on one line, as a doctest and inside an expression:
    >> C_code:Int64`(int64_t)@x`
    assert C_code:Int64`(int64_t)@x` == x
    # A statement block, also short enough to stay put:
    C_code`(void)(@x + 1);`
    # ...and one that genuinely spans lines, which has to keep spanning them:
    C_code`
        int a = 1;
        int b = a + 1;
        (void)b;
    `
