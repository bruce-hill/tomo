// Configuration of values that will be baked into the executable:

#ifndef TOMO_VERSION
#define TOMO_VERSION "v???"
#endif

#ifndef GIT_VERSION
#define GIT_VERSION "???"
#endif

#ifndef TOMO_PREFIX
#define TOMO_PREFIX "/usr/local"
#endif

#ifndef DEFAULT_C_COMPILER
#define DEFAULT_C_COMPILER "cc"
#endif

#ifndef SUDO
#if defined(__OpenBSD__)
#define SUDO "doas"
#else
#define SUDO "sudo"
#endif
#endif

// vim: ts=4 sw=0 et cino=L2,l1,(0,W4,m1,\:0
