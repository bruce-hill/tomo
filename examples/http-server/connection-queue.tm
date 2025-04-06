use pthreads

func _assert_success(name:Text, val:Int32; inline):
    fail("$name() failed!") if val < 0

struct ConnectionQueue(_connections:@[Int32]=@[], _mutex=pthread_mutex_t.new(), _cond=pthread_cond_t.new()):
    func enqueue(queue:ConnectionQueue, connection:Int32):
        queue._mutex.lock()
        queue._connections.insert(connection)
        queue._mutex.unlock()
        queue._cond.signal()


    func dequeue(queue:ConnectionQueue -> Int32):
        conn : Int32? = none

        queue._mutex.lock()

        while queue._connections.length == 0:
            queue._cond.wait(queue._mutex)

        conn = queue._connections.pop(1)
        queue._mutex.unlock()
        queue._cond.signal()
        return conn!
