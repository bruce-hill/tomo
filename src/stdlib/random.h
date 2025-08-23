#include <stdint.h>
#include <assert.h>

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
#include <stdlib.h>
static ssize_t getrandom(void *buf, size_t buflen, unsigned int flags) {
    (void)flags;
    arc4random_buf(buf, buflen);
    return buflen;
}
#elif defined(__linux__)
// Use getrandom()
#   include <sys/random.h>
#else
    #error "Unsupported platform for secure random number generation"
#endif

static int64_t random_range(int64_t low, int64_t high) {
    uint64_t range = (uint64_t)high - (uint64_t)low + 1;
    uint64_t min_r = -range % range;
    uint64_t r;
    do {
        assert(getrandom(&r, sizeof(r), 0) == sizeof(r));
    } while (r < min_r);
    return (int64_t)((uint64_t)low + (r % range));
}
