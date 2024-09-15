# This is a coroutine library that uses libaco. If you don't have it installed,
# you can get it from the website: https://libaco.org
#
# Lua programmers will recognize this as similar to Lua's stackful coroutines.
#
# Async/Await programmers will weep at its beauty and gnash their teeth and
# rend their garments in despair at what they could have had.

use libaco.so
use <aco.h>

func main():
    !! Example usage:
    co := new(func():
        say("I'm in the coroutine!")
        yield()
        say("I'm back in the coroutine!")
    )
    >> co
    say("I'm in the main func")
    >> co:resume()
    say("I'm back in the main func")
    >> co:resume()
    say("I'm back in the main func again")
    >> co:resume()

_main_co := !@Memory
_shared_stack := !@Memory

struct Coroutine(co:@Memory):
    func is_finished(co:Coroutine; inline)->Bool:
        return inline C:Bool {((aco_t*)$co.$co)->is_finished}

    func resume(co:Coroutine)->Bool:
        if co:is_finished():
            return no
        inline C { aco_resume($co.$co); }
        return yes

func _init():
    inline C {
        aco_set_allocator(GC_malloc, NULL);
        aco_thread_init(aco_exit_fn);
    }
    _main_co = inline C:@Memory { aco_create(NULL, NULL, 0, NULL, NULL) }

    _shared_stack = inline C:@Memory { aco_shared_stack_new(0) }

func new(co:func())->Coroutine:
    if not _main_co:
        _init()

    main_co := _main_co
    shared_stack := _shared_stack
    aco_ptr := inline C:@Memory {
        aco_create($main_co, $shared_stack, 0, (void*)$co.fn, $co.userdata)
    }
    return Coroutine(aco_ptr)
            
func yield(; inline):
    inline C {
        aco_yield();
    }

