# A Posix Threads (pthreads) wrapper
use <pthread.h>

struct pthread_mutex_t(; extern, opaque)
    func new(->@pthread_mutex_t)
        return inline C : @pthread_mutex_t {
            pthread_mutex_t *mutex = GC_MALLOC(sizeof(pthread_mutex_t));
            pthread_mutex_init(mutex, NULL);
            GC_register_finalizer(mutex, (void*)pthread_mutex_destroy, NULL, NULL, NULL);
            mutex
        }

    func lock(m:&pthread_mutex_t)
        fail("Failed to lock mutex") unless inline C : Int32 { pthread_mutex_lock(_$m); } == 0

    func unlock(m:&pthread_mutex_t)
        fail("Failed to unlock mutex") unless inline C : Int32 { pthread_mutex_unlock(_$m); } == 0

struct pthread_cond_t(; extern, opaque)
    func new(->@pthread_cond_t)
        return inline C : @pthread_cond_t {
            pthread_cond_t *cond = GC_MALLOC(sizeof(pthread_cond_t));
            pthread_cond_init(cond, NULL);
            GC_register_finalizer(cond, (void*)pthread_cond_destroy, NULL, NULL, NULL);
            cond
        }

    func wait(cond:&pthread_cond_t, mutex:&pthread_mutex_t)
        fail("Failed to wait on condition") unless inline C : Int32 { pthread_cond_wait(_$cond, _$mutex); } == 0

    func signal(cond:&pthread_cond_t)
        fail("Failed to signal pthread_cond_t") unless inline C : Int32 { pthread_cond_signal(_$cond); } == 0

    func broadcast(cond:&pthread_cond_t)
        fail("Failed to broadcast pthread_cond_t") unless inline C : Int32 { pthread_cond_broadcast(_$cond); } == 0

struct pthread_rwlock_t(; extern, opaque)
    func new(->@pthread_rwlock_t)
        return inline C : @pthread_rwlock_t {
            pthread_rwlock_t *lock = GC_MALLOC(sizeof(pthread_rwlock_t));
            pthread_rwlock_init(lock, NULL);
            GC_register_finalizer(lock, (void*)pthread_rwlock_destroy, NULL, NULL, NULL);
            lock
        }

    func read_lock(lock:&pthread_rwlock_t)
        inline C { pthread_rwlock_rdlock(_$lock); }

    func write_lock(lock:&pthread_rwlock_t)
        inline C { pthread_rwlock_wrlock(_$lock); }

    func unlock(lock:&pthread_rwlock_t)
        inline C { pthread_rwlock_unlock(_$lock); }

struct pthread_t(; extern, opaque)
    func new(fn:func() -> @pthread_t)
        return inline C : @pthread_t {
            pthread_t *thread = GC_MALLOC(sizeof(pthread_t));
            pthread_create(thread, NULL, _$fn.fn, _$fn.userdata);
            thread
        }

    func join(p:pthread_t) inline C { pthread_join(_$p, NULL); }
    func cancel(p:pthread_t) inline C { pthread_cancel(_$p); }
    func detatch(p:pthread_t) inline C { pthread_detach(_$p); }

struct IntQueue(_queue:@[Int], _mutex:@pthread_mutex_t, _cond:@pthread_cond_t)
    func new(initial:[Int]=[] -> IntQueue)
        return IntQueue(@initial, pthread_mutex_t.new(), pthread_cond_t.new())

    func give(q:IntQueue, n:Int)
        do q._mutex.lock()
            q._queue.insert(n)
            q._mutex.unlock()
        q._cond.signal()

    func take(q:IntQueue -> Int)
        do q._mutex.lock()
            n := q._queue.pop(1)
            while not n
                q._cond.wait(q._mutex)
                n = q._queue.pop(1)
            q._mutex.unlock()
            return n!
        fail("Unreachable")

func main()
    jobs := IntQueue.new()
    results := IntQueue.new()

    say_mutex := pthread_mutex_t.new()
    announce := func(speaker:Text, text:Text)
        do say_mutex.lock()
            say("$\033[2m[$speaker]$\033[m $text")
            say_mutex.unlock()

    worker := pthread_t.new(func()
        say("I'm in the thread!")
        repeat
            announce("worker", "waiting for job")
            job := jobs.take()
            result := job * 10
            announce("worker", "Jobbing $job into $result")
            results.give(result)
            announce("worker", "Signaled $result")
    )

    for i in 10
        announce("boss", "Pushing job $i")
        jobs.give(i)
        announce("boss", "Gave job $i")

    for i in 10
        announce("boss", "Getting result...")
        result := results.take()
        announce("boss", "Got result $result")

    >> worker.cancel()
