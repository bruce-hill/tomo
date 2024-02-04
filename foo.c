#include <stdio.h>

int main(void) {
    int x = 23;
    const char *s = "Hi";
#define say(x) _Generic(x, int: printf("%d\n", x), char *: puts(s), default: puts("???"))
    say(x);
    say(s);
#define all(...) { __VA_ARGS__; }
    all(say("one"); say(2))
    return 0;
}
