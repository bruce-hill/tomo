#include "nextlang.h"

static void foo(Int64 x);


void foo(Int64 x)
{
    say("Hello world!");
}

int main(int argc, const char *argv[])
{
    (void) argc;
    (void) argv;
    foo(((Int64_t) 5));

    return 0;
}
