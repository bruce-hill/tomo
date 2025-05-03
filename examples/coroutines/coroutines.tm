# This is a coroutine library that uses libaco (https://libaco.org)
#
# Lua programmers will recognize this as similar to Lua's stackful coroutines.
#
# Async/Await programmers will weep at its beauty and gnash their teeth and
# rend their garments in despair at what they could have had.

use ./aco.h
use ./aco.c
use ./acoyield.S

func main()
    say("Example usage")
    co := Coroutine(func()
        say("I'm in the coroutine!")
        yield()
        say("I'm back in the coroutine!")
    )
    >> co
    say("I'm in the main func")
    >> co.resume()
    say("I'm back in the main func")
    >> co.resume()
    say("I'm back in the main func again")
    >> co.resume()

struct aco_t(; extern, opaque)
struct aco_shared_stack_t(; extern, opaque)

_main_co : @aco_t? = none
_shared_stack : @aco_shared_stack_t? = none

struct Coroutine(co:@aco_t)
    convert(fn:func() -> Coroutine)
        if not _main_co
            _init()

        main_co := _main_co
        shared_stack := _shared_stack
        aco_ptr := C_code:@aco_t(
            aco_create(@main_co, @shared_stack, 0, (void*)@fn.fn, @fn.userdata)
        )
        return Coroutine(aco_ptr)

    func is_finished(co:Coroutine->Bool; inline)
        return C_code:Bool(((aco_t*)@co.co)->is_finished)

    func resume(co:Coroutine->Bool)
        if co.is_finished()
            return no
        C_code { aco_resume(@co.co); }
        return yes

func _init()
    C_code {
        aco_set_allocator(GC_malloc, NULL);
        aco_thread_init(aco_exit_fn);
    }
    _main_co = C_code:@aco_t(aco_create(NULL, NULL, 0, NULL, NULL))

    _shared_stack = C_code:@aco_shared_stack_t(aco_shared_stack_new(0))

func yield(; inline)
    C_code {
        aco_yield();
    }

