// Configuration of values that will be baked into the executable:

#ifndef GIT_VERSION
#define GIT_VERSION "???"
#endif

extern const char *TOMO_PATH;
extern const char *TOMO_VERSION;

// The `zig cc` target that user programs are compiled for (e.g.
// "x86_64-linux-musl"). When non-empty, Tomo compiles and links programs as
// fully static musl binaries with the same toolchain used to build the compiler.
#ifndef ZIG_TARGET
#define ZIG_TARGET ""
#endif

// The platform key this Tomo build is for (e.g. "x86_64-linux") and the
// space-separated list of platforms Tomo distributions exist for. Used by the
// --target flag to validate and set up cross-compilation.
#ifndef TOMO_PLATFORM
#define TOMO_PLATFORM ""
#endif
#ifndef TOMO_DIST_PLATFORMS
#define TOMO_DIST_PLATFORMS ""
#endif

#ifndef SUDO
#if defined(__OpenBSD__)
#define SUDO "doas"
#else
#define SUDO "sudo"
#endif
#endif
