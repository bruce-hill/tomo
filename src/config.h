// Configuration of values that will be baked into the executable:

#ifndef TOMO_VERSION
#define TOMO_VERSION "v???"
#endif

#ifndef GIT_VERSION
#define GIT_VERSION "???"
#endif

#ifndef TOMO_INSTALL
#define TOMO_INSTALL "/usr/local"
#endif

extern const char *TOMO_PATH;

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
