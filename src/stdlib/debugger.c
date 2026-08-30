// Debugger support: the `breakpoint()` builtin.

#include "debugger.h"
#include "util.h"

// libtomo is built once, without TOMO_DEBUG_BUILD, so the call-site macro from
// the header is in the way of defining the function it names. Drop it here;
// this file is the one place that wants the symbol rather than the macro.
#undef tomo_breakpoint

// Deliberately empty: this exists only so that a debugger has a symbol to set
// a breakpoint on, and so that a `breakpoint()` in Tomo source turns into a
// distinct, findable place in the compiled program.
//
// The empty asm is what keeps it that way. Without it the compiler is free to
// fold this function together with any other empty function it emits
// (identical-code folding), which would make a breakpoint set here fire in
// unrelated places; the "memory" clobber also stops the call from being
// hoisted across the surrounding statements, so the program really is stopped
// where the source says it is.
public
__attribute__((noinline)) void tomo_debug_breakpoint(void) {
    __asm__ __volatile__("" ::: "memory");
}

// `breakpoint` used as a value instead of being called. There is no call site
// for the macro to act on, so this is what such a reference resolves to. It
// always goes to the real thing: libtomo is built once, without
// TOMO_DEBUG_BUILD, so it cannot compile the call away, and it does not need
// to, since stopping on tomo_debug_breakpoint() is a no-op unless a debugger
// is there to notice.
public
void tomo_breakpoint(void) { tomo_debug_breakpoint(); }
