#pragma once

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__APPLE__)
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
