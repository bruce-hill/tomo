# A Posix Threads (pthreads) wrapper
use <pthread.h>

extern pthread_mutex_lock:func(mutex:&pthread_mutex_t -> Int32)
extern pthread_mutex_unlock:func(mutex:&pthread_mutex_t -> Int32)

struct pthread_mutex_t(; extern, opaque):
    func new(->@pthread_mutex_t):
        return inline C : @pthread_mutex_t {
            pthread_mutex_t *mutex = new(pthread_mutex_t);
            pthread_mutex_init(mutex, NULL);
            GC_register_finalizer(mutex, (void*)pthread_mutex_destroy, NULL, NULL, NULL);
            mutex
        }

    func lock(m:&pthread_mutex_t):
        fail("Failed to lock mutex") unless pthread_mutex_lock(m) == 0

    func unlock(m:&pthread_mutex_t):
        fail("Failed to unlock mutex") unless pthread_mutex_unlock(m) == 0

extern pthread_cond_wait:func(cond:&pthread_cond_t, mutex:&pthread_mutex_t -> Int32)
extern pthread_cond_signal:func(cond:&pthread_cond_t -> Int32)
extern pthread_cond_broadcast:func(cond:&pthread_cond_t -> Int32)

struct pthread_cond_t(; extern, opaque):
    func new(->@pthread_cond_t):
        return inline C : @pthread_cond_t {
            pthread_cond_t *cond = new(pthread_cond_t);
            pthread_cond_init(cond, NULL);
            GC_register_finalizer(cond, (void*)pthread_cond_destroy, NULL, NULL, NULL);
            cond
        }

    func wait(cond:&pthread_cond_t, mutex:&pthread_mutex_t):
        fail("Failed to wait on condition") unless pthread_cond_wait(cond, mutex) == 0

    func signal(cond:&pthread_cond_t):
        fail("Failed to signal pthread_cond_t") unless pthread_cond_signal(cond) == 0

    func broadcast(cond:&pthread_cond_t):
        fail("Failed to broadcast pthread_cond_t") unless pthread_cond_broadcast(cond) == 0

struct pthread_t(; extern, opaque):
    func new(fn:func() -> @pthread_t):
        return inline C : @pthread_t {
            pthread_t *thread = new(pthread_t);
            pthread_create(thread, NULL, _$fn.fn, _$fn.userdata);
            thread
        }

    func join(p:@pthread_t): inline C { pthread_join(*_$p, NULL); }
    func cancel(p:@pthread_t): inline C { pthread_cancel(*_$p); }
    func detatch(p:@pthread_t): inline C { pthread_detach(*_$p); }

struct IntQueue(_queue:@[Int], _mutex:@pthread_mutex_t, _cond:@pthread_cond_t):
    func new(initial=[:Int] -> IntQueue):
        return IntQueue(@initial, pthread_mutex_t.new(), pthread_cond_t.new())

    func give(q:IntQueue, n:Int):
        begin: q._mutex:lock()
        end: q._mutex:unlock()
        do: q._queue:insert(n)
        q._cond:signal()

    func take(q:IntQueue -> Int):
        begin: q._mutex:lock()
        end: q._mutex:unlock()
        do:
            repeat:
                if n := q._queue:pop(1):
                    return n
                q._cond:wait(q._mutex)
        fail("Unreachable")

func main():
    jobs := IntQueue.new()
    results := IntQueue.new()

    say_mutex := pthread_mutex_t.new()
    announce := func(speaker:Text, text:Text):
        begin: say_mutex:lock()
        end: say_mutex:unlock()
        do: say("$\033[2m[$speaker]$\033[m $text")

    worker := pthread_t.new(func():
        say("I'm in the thread!")
        repeat:
            announce("worker", "waiting for job")
            job := jobs:take()
            result := job * 10
            announce("worker", "Jobbing $job into $result")
            results:give(result)
            announce("worker", "Signaled $result")
    )

    for i in 10:
        announce("boss", "Pushing job $i")
        jobs:give(i)
        announce("boss", "Gave job $i")

    for i in 10:
        announce("boss", "Getting result...")
        result := results:take()
        announce("boss", "Got result $result")

    >> worker:cancel()
