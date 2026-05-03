// Configuration of values that will be baked into the executable:

#ifndef GIT_VERSION
#define GIT_VERSION "???"
#endif

#ifndef TOMO_BUILD_PREFIX
#define TOMO_BUILD_PREFIX "/usr/local"
#endif

#ifndef TOMO_BUILD_VERSION
#define TOMO_BUILD_VERSION "v0"
#endif

extern const char *TOMO_PATH;
extern const char *TOMO_VERSION;

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
