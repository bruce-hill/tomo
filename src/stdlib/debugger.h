// Debugger support: the `breakpoint()` builtin.

#pragma once

// The symbol a debugger stops on. It does nothing itself, since its whole purpose
// is to be a place in the program that a debugger can name. `tomo run --debug`
// sets a breakpoint on it and, when it fires, steps up into the Tomo frame
// that called it (see lib/tomo@VERSION/tomo-gdb.py).
__attribute__((noinline)) void tomo_debug_breakpoint(void);

// The function `breakpoint` is bound to. Calls go through the macro below, but
// the symbol has to exist as well for the (unusual) case of passing
// `breakpoint` around as a value rather than calling it.
void tomo_breakpoint(void);

// What a call to `breakpoint()` compiles to. Only a build compiled for
// debugging (`--debug`, which defines TOMO_DEBUG_BUILD) stops; in every other
// build the call compiles away to nothing, so leaving a `breakpoint()` in the
// source costs a release build nothing at all.
#ifdef TOMO_DEBUG_BUILD
#define tomo_breakpoint() tomo_debug_breakpoint()
#else
#define tomo_breakpoint() ((void)0)
#endif
