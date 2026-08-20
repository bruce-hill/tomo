# State the real thread-safety contract in number.h

number-design.md documents the library as "single-threaded-per-number (sharing a
number across threads without external synchronization is undefined)", but
the mutable state is broader than per-number, so the real contract is
single-threaded *per process*:

- the allocator globals set by `number_set_allocator`
- the cached pi / sqrt(2) singletons (shared across all callers, with
  mutable memoized approximations)
- every CR node's memoized (precision, value) approximation cache — shared
  DAG nodes mean two threads working on *different* numbers can still race
  on a shared subexpression's cache
- refcounts themselves (non-atomic increments/decrements)
- the NUMBER_STATS counters

Action:

- Document the per-process contract prominently in number.h (it currently
  isn't stated there at all — only number-design.md's weaker phrasing).
- Optionally, later, an opt-in build (`-DNUMBER_THREADS`?) with atomic
  refcounts and a per-node lock or CAS on the approximation cache — number-design.md
  already sketches this ("the cache update is a small critical section").
  Only worth doing if there's demand; the documentation fix is the real todo.
