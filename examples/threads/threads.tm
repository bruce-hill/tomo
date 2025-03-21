use <pthread.h>

struct Mutex(_mutex:@Memory):
    func new(->Mutex):
        return Mutex(
            inline C : @Memory {
                pthread_mutex_t *mutex = GC_MALLOC(sizeof(pthread_mutex_t));
                pthread_mutex_init(mutex, NULL);
                GC_register_finalizer(mutex, (void*)pthread_mutex_destroy, NULL, NULL, NULL);
                mutex
            }
        )

    func do_locked(m:Mutex, fn:func(); inline):
        inline C {
            pthread_mutex_lock((pthread_mutex_t*)_$m._mutex);
        }
        fn()
        inline C {
            pthread_mutex_unlock((pthread_mutex_t*)_$m._mutex);
        }

struct ThreadCondition(_cond:@Memory):
    func new(->ThreadCondition):
        return ThreadCondition(
            inline C : @Memory {
                pthread_cond_t *cond = GC_MALLOC(sizeof(pthread_cond_t));
                pthread_cond_init(cond, NULL);
                GC_register_finalizer(cond, (void*)pthread_cond_destroy, NULL, NULL, NULL);
                cond
            }
        )

    func wait(c:ThreadCondition, m:Mutex; inline):
        inline C {
            pthread_cond_wait((pthread_cond_t*)_$c._cond, (pthread_mutex_t*)_$m._mutex);
        }

    func signal(c:ThreadCondition; inline):
        inline C {
            pthread_cond_signal((pthread_cond_t*)_$c._cond);
        }

    func broadcast(c:ThreadCondition; inline):
        inline C {
            pthread_cond_broadcast((pthread_cond_t*)_$c._cond);
        }

struct Guard(mutex=Mutex.new(), cond=ThreadCondition.new()):
    func guarded(g:Guard, fn:func(); inline):
        g.mutex:do_locked(fn)
        g.cond:signal()

    func wait(g:Guard):
        g.cond:wait(g.mutex)

struct PThread(_thread:@Memory):
    func new(fn:func() -> PThread):
        return PThread(
            inline C : @Memory {
                pthread_t *thread = GC_MALLOC(sizeof(pthread_t));
                pthread_create(thread, NULL, _$fn.fn, _$fn.userdata);
                thread
            }
        )

    func join(t:PThread):
        inline C {
            pthread_join(*(pthread_t*)_$t._thread, NULL);
        }

    func cancel(t:PThread):
        inline C {
            pthread_cancel(*(pthread_t*)_$t._thread);
        }

    func detatch(t:PThread):
        inline C {
            pthread_detach(*(pthread_t*)_$t._thread);
        }

func main():
    g := Guard()
    queue := @[10, 20]

    t := PThread.new(func():
        say("In another thread!")
        item := @none:Int
        while item[] != 30:
            g:guarded(func():
                while queue.length == 0:
                    g:wait()

                item[] = queue[1]
                queue:remove_at(1)
            )
            say("Processing: $item")
            sleep(0.01)
        say("All done!")
    )
    >> t
    >> sleep(1)
    >> g:guarded(func():
        queue:insert(30)
    )
    >> t:join()
