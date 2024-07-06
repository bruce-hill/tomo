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
being compiled into a shared library, `libfoo.so`:

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
