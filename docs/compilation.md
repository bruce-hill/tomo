# Compilation Pipeline

For a simple single-file program, the compilation process has a dependency
graph that looks like this:
```
  +----------+  transpile   +------------+  generate arg parser   
  | foo.tm.h | <----------- |   foo.tm   | -------------
  +----------+              +------------+              |
    |    |                    | transpile               |
    |    |                    v                         |
    |    |                  +------------+              |
    |    |                  |  foo.tm.c  |         +--------------------+
    |    |                  +------------+         | main() entry point |
    |    |                    |                    +--------------------+
    |    |          compile   v                         |
    |    |                  +------------+              |
    |    +----------------->|  foo.tm.o  |              |
    |                       +------------+              |
    |                         | link                    |
    |                         v                         |
    |          compile      +------------+  compile     |
    +---------------------> |    foo     | <------------+
                            +------------+
```

For a more complicated example, imagine `foo.tm` imports `baz.tm` and both are
being compiled into a shared library, `libfoo.so` (or `libfoo.dylib` on Mac):

```
        +---------------------------------------+
        |                                       |
      +----------+  transpile   +------------+  |
   +- | baz.tm.h | <----------- |   baz.tm   | -+-------------------------+
   |  +----------+              +------------+  |                         |
   |    |                         |             |                         |
   |    |                         | transpile   |                         |
   |    |                         v             |                         |
   |    |                       +------------+  |                         |
   |    |                       |  baz.tm.c  |  |                         |
   |    |                       +------------+  |                         |
   |    |                         |             |                         |
   |    |                         | compile     |                         |
   |    |                         v             |                         |
   |    |          compile      +------------+  |                         |
   |    +---------------------> |  baz.tm.o  |  |                         |
   |                            +------------+  |                         |
   |                              |             |                         |
   |                              | link        | compile                 |
   |                              v             v                         |
   |               compile      +--------------------------------------+  |
   |    +---------------------> |              libfoo.so               |  |
   |    |                       +--------------------------------------+  |
   |    |                                       ^                         |
   |    |                                       | link                    |
   |    |                                       |                         |
   |  +----------+  transpile   +------------+  |                         |
   |  | foo.tm.h | <----------- |   foo.tm   |  |                         |
   |  +----------+              +------------+  |                         |
   |    |                         |             |                         |
   |    |                         | transpile   |                         |
   |    |                         v             |                         |
   |    |                       +------------+  |          type info      |
   |    |                       |  foo.tm.c  | <+-------------------------+
   |    |                       +------------+  |
   |    |                         |             |
   |    |                         | compile     |
   |    |                         v             |
   |    |          compile      +------------+  |
   +----+---------------------> |  foo.tm.o  | -+
        |                       +------------+
        |          compile        ^
        +-------------------------+ 
```

These dependency graphs are relatively complicated-looking, but here are some
rough takeaways:

 1) Header files are a dependency for many parts of the process, so it's
    good to transpile them as early as possible.
 2) Once all the header files are available, 
    compiled into their object files in parallel. This is by far the
    slowest part of compilation (invoking the C compiler), so it benefits
    the most from parallelization.
 3) After all object files are compiled, the last step is to link them
    all together (fast and simple).

To sastisfy these requirements as efficiently as possible, the approach taken
below is to first transpile all header files sequentially (this could be
parallelized, but is probably faster than the overhead of forking new
processes), then fork a new process for each dependency to transpile and
compile it to an object file. Then, wait for all child processes to finish and
link the resulting object files together.

## Phase 1 (sequential transpilation):

```
          +--------+       +--------+
          | foo.tm |       | baz.tm |
          +--------+       +--------+
              |              |   | 
              +--------------+   |
              |                  |
              v                  v
         +----------+      +----------+
         | foo.tm.h |      | baz.tm.h |
         +----------+      +----------+
```

## Phase 2 (parallel transpilation/compilation):

```
 ################################    ################################
 #           Process 1          #    #           Process 2          #
 #   +--------+   +----------+  #    #  +----------+   +--------+   #
 #   | foo.tm |   | foo.tm.h |  #    #  | baz.tm.h |   | baz.tm |   #
 #   +--------+   | baz.tm.h |  #    #  +----------+   +--------+   #
 #       |        +----+-----+  #    #         |           |        #
 #       v             |        #    #         |           v        #
 #  +----------+       |        #    #         |      +----------+  #
 #  | foo.tm.c |       |        #    #         |      | baz.tm.c |  #
 #  +----------+       |        #    #         |      +----------+  #
 #       |             |        #    #         |        |           #
 #       +------+------+        #    #         +--------+           #
 #              |               #    #                  |           #
 #              v               #    #                  v           #
 #        +----------+          #    #            +----------+      #
 #        | foo.tm.o |          #    #            | baz.tm.o |      #
 #        +----------+          #    #            +----------+      #
 ################################    ################################
```

## Phase 3 (linking a shared object file library):

```
   +----------+      +----------+
   | foo.tm.o |      | baz.tm.o |
   +----------+      +----------+
        |                 |
        +--------+--------+
                 |
                 v
           +-----------+
           | libfoo.so |
           +-----------+
```

## Phase 3 (linking an executable):

```
   +----------+      +----------+   +--------+  +--------+
   | foo.tm.o |      | baz.tm.o |   | foo.tm |  | baz.tm |
   +----------+      +----------+   +--------+  +--------+
        |                 |              |          |
        +--------+--------+              +----+-----+
                 | link                       | Figure out command line args
                 v                            v
              +-----+    compile   +-------------------------+
              | foo |<-------------| main() function for exe |
              +-----+              +----------+--------------+
                                   | foo.tm.h |
                                   +----------+
                                   | baz.tm.h |
                                   +----------+
```

# Build Metadata

Tomo attaches build information and version data to each compiled executable and library file.

Various different info lines will be added with tab-separated key/value pairs.
Build data can be extracted from a compiled executable or library file using `tomo -b` or `tomo --build-info`.
An example output looks like this:

```
Tomo compiler version:  2026-04-26
Tomo compiler git:      2026-04-27_135100d6
Binary compiled at:     2026-04-27 17:26:42 EDT
Package digest [random]:        sha256:049864fed351c781e337a38ed527d845e29df244606b1f8a59d3766754fa7339
Package source [random]:        https://github.com/bruce-hill/tomo-random/archive/refs/tags/v1.3.tar.gz
Package digest [patterns]:      sha256:2551b5ecc2617d884d856764a7f4f5ba394e97aaf1412110fe63f137b510f546
Package source [patterns]:      https://github.com/bruce-hill/tomo-patterns/archive/refs/tags/v2025-11-29.tar.gz
```

The exact format and contents of this information is not guaranteed and may be subject to change.
