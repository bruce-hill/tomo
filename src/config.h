// Configuration of values that will be baked into the executable:

#ifndef GIT_VERSION
#define GIT_VERSION "???"
#endif

extern const char *TOMO_PATH;
extern const char *TOMO_VERSION;

#ifndef DEFAULT_C_COMPILER
#define DEFAULT_C_COMPILER "cc"
#endif

// The `zig cc` target that user programs are compiled for (e.g.
// "x86_64-linux-musl"). When non-empty, Tomo compiles and links programs as
// fully static musl binaries with the same toolchain used to build the compiler.
#ifndef ZIG_TARGET
#define ZIG_TARGET ""
#endif

#ifndef SUDO
#if defined(__OpenBSD__)
#define SUDO "doas"
#else
#define SUDO "sudo"
#endif
#endif
