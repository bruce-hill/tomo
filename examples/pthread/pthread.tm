use <pthread.h>

struct pthread_cond_t(; extern, opaque):
    func new(->@pthread_cond_t):
        return inline C : @pthread_cond_t {
            pthread_cond_t *cond = new(pthread_cond_t);
            pthread_cond_init(cond, NULL);
            GC_register_finalizer(cond, (void*)pthread_cond_destroy, NULL, NULL, NULL);
            cond
        }

struct pthread_mutex_t(; extern, opaque):
    func new(->@pthread_mutex_t):
        return inline C : @pthread_mutex_t {
            pthread_mutex_t *mutex = new(pthread_mutex_t);
            pthread_mutex_init(mutex, NULL);
            GC_register_finalizer(mutex, (void*)pthread_mutex_destroy, NULL, NULL, NULL);
            mutex
        }

extern pthread_cond_wait:func(cond:&pthread_cond_t, mutex:&pthread_mutex_t -> Int32)
extern pthread_cond_signal:func(cond:&pthread_cond_t -> Int32)
extern pthread_cond_broadcast:func(cond:&pthread_cond_t -> Int32)
extern pthread_mutex_lock:func(mutex:&pthread_mutex_t -> Int32)
extern pthread_mutex_unlock:func(mutex:&pthread_mutex_t -> Int32)

# extern pthread_join:func(thread:pthread_t, retval=none:@@Memory)
# extern pthread_cancel:func(thread:pthread_t)
# extern pthread_detach:func(thread:pthread_t)

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
