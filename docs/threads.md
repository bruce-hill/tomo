# Threads

Tomo supports POSIX threads (pthreads) through the `Thread` type. The
recommended practice is to have each thread interact with other threads only
through [mutex-guarded datastructures](mutexed.md).

## Thread Methods

- [`func cancel(thread: Thread)`](#cancel)
- [`func detach(thread: Thread)`](#detach)
- [`func join(thread: Thread)`](#join)
- [`func new(fn: func(->Void) -> Thread)`](#new)

### `cancel`
Requests the cancellation of a specified thread.

**Signature:**  
```tomo
func cancel(thread: Thread)
```

- `thread`: The thread to cancel.

**Returns:**  
Nothing.

**Example:**  
```tomo
>> thread:cancel()
```

---

### `detach`
Detaches a specified thread, allowing it to run independently.

**Signature:**  
```tomo
func detach(thread: Thread)
```

- `thread`: The thread to detach.

**Returns:**  
Nothing.

**Example:**  
```tomo
>> thread:detach()
```
### `join`
Waits for a specified thread to terminate.

**Signature:**  
```tomo
func join(thread: Thread)
```

- `thread`: The thread to join.

**Returns:**  
Nothing.

**Example:**  
```tomo
>> thread:join()
```

---

### `new`
Creates a new thread to execute a specified function.

**Signature:**  
```tomo
func new(fn: func(->Void) -> Thread)
```

- `fn`: The function to be executed by the new thread.

**Returns:**  
A new `Thread` object representing the created thread.

**Example:**  
```tomo
>> jobs := |Int|
>> results := |Int|
>> thread := Thread.new(func():
    repeat:
        input := jobs:get()
        results:give(input + 10
)
= Thread<0x12345678>
>> jobs:give(10)
>> results:get()
= 11
```
