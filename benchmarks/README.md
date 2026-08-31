# Tomo benchmarks

Tomo's performance against other languages, using programs from [The Computer
Language Benchmarks Game][clbg] (CLBG).

Only the **Tomo** ports (`tomo/*.tm`) live in this repo. Every other language's
source is downloaded on demand into the git-ignored `fetched/`, so this
directory never vendors other languages' code. Most come from the CLBG site;
Zig, Nim, and Odin aren't covered there, so they come from the community
[Programming-Language-Benchmarks][plb] repo instead.

## Results

The checked-in PNGs are a snapshot from one x86-64 Linux box (best of 3 runs,
each pinned to a single core, every output validated). Timings are
machine-specific; `make benchmarks` reproduces and regenerates them locally.

![Tomo vs. other languages across nine benchmarks](results.png)

Across ~17 languages and all nine benchmarks, **Tomo sits inside the
compiled-language cluster and beats every scripting language on every
benchmark**, and on three of the nine it is at or ahead of C. Highlights:
fannkuch-redux 0.16s (a shade faster than C++, Fortran, and C), binary-trees
0.20s (4th of 16, ahead of C, C++, Zig, Go), mandelbrot 0.70s (beats the
multithreaded C and Go entries outright), k-nucleotide 0.23s (3rd, behind only
C and C++). Its weakest showing is pidigits at ~4.2× C, a pure GMP bignum
stress test, still 4th of 9. No garbage-collected, memory-safe language in the
set is dramatically ahead of it.

Where a few entries are far slower than the rest (Python's 182s on
spectral-norm), a panel's x-axis is truncated to the rest of the field, still
an ordinary linear axis. Entries that don't fit run off the end with an
arrowhead and their real time, so they're plainly off the chart rather than
silently rescaled. The cut is derived from the data (`truncate_at` in
`plot.py`), not hardcoded per benchmark.

Per-benchmark graphs: [n-body](results-nbody.png) ·
[fannkuch-redux](results-fannkuchredux.png) · [fasta](results-fasta.png) ·
[k-nucleotide](results-knucleotide.png) ·
[binary-trees](results-binarytrees.png) · [pidigits](results-pidigits.png) ·
[reverse-complement](results-reversecomplement.png) ·
[mandelbrot](results-mandelbrot.png) ·
[spectral-norm](results-spectralnorm.png).

![Static binary size across languages](sizes.png)

Binary sizes are statically linked, so each number is the whole self-contained
footprint rather than a stub leaning on a system `libc`, and they are recorded
exactly as each toolchain produces them. Nothing is stripped: the question is
what a program weighs when you build it, not how far it could be squeezed
afterwards.

Tomo's is **~1.36 MB and near-constant** across all nine (1348–1381 KB), since
the runtime dominates and the program itself is noise. That puts it at
1.4–1.6× C, under Rust (1.4–1.6 MB), Go (2.2–2.6 MB), and C++ (up to 2.7 MB),
and above Nim (~900 KB), Zig (~1.0 MB), and C (837–973 KB).

Roughly 490 KB of Tomo's is DWARF, which is what lets a runtime error name the
`.tm` file, function, and line it came from. Zig and Go carry debug info in
their default builds too (970 KB and 756 KB respectively); C, Nim, and Rust
ship none in these configurations, so a stripped comparison would flatter Tomo
and Zig for a cost they do actually pay on disk.

Only a handful of languages produce a standalone static binary at all, so
benchmarks left with fewer than five of them (pidigits, reverse-complement,
spectral-norm) are dropped from the chart rather than shown as a two- or
three-way list.

## Usage

From the repository root:

```sh
make benchmarks                          # everything, end to end
make benchmarks BENCH_LANGS=tomo         # re-time Tomo only, keep other rows
make benchmarks BENCH_ARGS=1000 BENCH_REPEATS=1   # quick smoke run
make benchmark-run                       # just re-time (no sizes, no graphs)
make benchmark-sizes                     # just re-measure binary sizes
make benchmark-graphs                    # just re-render from existing JSON
make benchmark-list                      # benchmarks + toolchain status
make benchmark-refetch                   # re-download reference programs
```

`bench.py` (`fetch`/`list`/`run`/`sizes`) and `plot.py` (`--sizes`) can also be
run directly. Each `BENCH_*` variable works as an environment variable or a
`make` variable:

- `BENCH_ARGS="1000"`: override the benchmark input.
- `BENCH_REPEATS=1`: timed runs per language (best time is kept).
- `BENCH_BUILD_TIMEOUT=180`: per-language build timeout in seconds.
- `BENCH_CPU=2`: which core to pin to (default `0`); `all` disables pinning.

## How a run works

The reference language (C) runs first and its output becomes the expected
result. Every other language must reproduce it byte-for-byte, so a program
can't post a fast time by computing the wrong thing. Each is run `repeats`
times and the best wall-clock time is kept, along with peak RSS.

**Every timed run is pinned to one core** (`taskset -c 0`). Several CLBG
programs are multithreaded (fannkuch-redux's `go-1` hardcodes `GOMAXPROCS(4)`,
`gpp-1` fans out with `std::async`) while the C reference and the Tomo ports
are single-threaded. Pinning makes it a same-resources comparison instead of
1-core against N-core.

The Tomo ports implement the actual algorithm in pure Tomo, using the same
algorithm as the reference. The only permitted `C_code` is `printf` for the
final numeric output, since Tomo has no `%.9f`-style zero-padded float
formatting.

## Coverage

All nine of the CLBG's library-free core benchmarks are implemented.
(regex-redux is excluded: it benchmarks a regex library, not the language.)
Not every language has an entry for every one, and a language with no
installed toolchain is skipped rather than failing the run.

| Language | Benchmarks | Notable gaps |
|---|---|---|
| C, C++, Go, Java, JavaScript, Python, PyPy, **Tomo** | 9/9 | |
| C# | 8/9 | k-nucleotide needs the `DictionarySlim` NuGet package |
| Lua, LuaJIT | 8/9 | no pidigits entry |
| Swift | 7/9 | no pidigits; `swiftc` can't type-check spectral-norm's `eval_A` in reasonable time |
| Rust | 6/9 | the missing three all need `rug`/`ramp`, `rayon`, or an arena crate |
| Fortran, Zig | 4/9 | no CLBG Fortran hash table for k-nucleotide; Zig's k-nucleotide reads a file path, not stdin |
| Nim | 3/9 | no upstream entries to fetch |
| Odin | 2/9 | no upstream entries; its k-nucleotide no longer compiles |

Toolchain notes:

- **C#** uses Native AOT (`dotnet publish` with `PublishAot`), matching CLBG's
  `csharpaot` entries, so it's a fair peer to the other compiled languages with
  no JIT startup. Needs the .NET SDK plus `clang`, and network access once to
  restore ILCompiler from NuGet.
- **Zig, Nim, Odin** come from [PLB][plb] as raw files, with a program's slug
  being the filename stem (`"2"` → `2.nim`). Zig's sources target ~0.14, so
  the driver prefers a `zig0.14` on PATH (override with `ZIG=...`).
- **LuaJIT** is Lua 5.1, so its config carries a one-line `prelude` shimming
  `table.unpack`. Lua and LuaJIT then run byte-identical fetched sources.
- **PyPy** runs CPython's fetched sources, so it needs a Python 3 PyPy.

Per-benchmark quirks:

- **k-nucleotide** and **reverse-complement** read a FASTA file on stdin, which
  the driver generates with the C fasta program at the configured scale.
  k-nucleotide's C entry needs klib's `khash.h` at `/usr/include/klib`.
- **mandelbrot**'s C/C++ get `-ffp-contract=off`: under `-march=native` gcc
  otherwise fuses the complex-square update into FMAs, flipping one boundary
  pixel away from the output everyone else agrees on.
- **spectral-norm**'s C++ needs `-fopenmp`; Python uses the single-threaded
  `python3-6`, since the default entry's multiprocessing pool thrashes when
  pinned to one core.

## What this suite turned up

Chasing these numbers produced real runtime and compiler fixes. Each
before/after is the pair measured at the time, not where things stand now.

- **GMP limbs weren't GC-allocated.** Every `Int` bignum used GMP's default
  `malloc`, so the collector never reclaimed it and bignum-heavy loops leaked
  continuously. Routing GMP through the GC (`mp_set_memory_functions` in
  `tomo_init`) took pidigits from ~11.3 GB peak RSS to 12 MB, and 6.75s to
  1.75s.
- **`List$insert` growth copied element-by-element**, one `memcpy()` call per
  item even when the data was already packed. `perf` put a quarter of
  reverse-complement's runtime inside those one-byte calls. Bulk-copying
  contiguous lists took it 0.60s → 0.40s and sped up any list built by
  repeated `insert()`.
- **`List.find`/`has` now use `memchr()` for `[Byte]`/`[Int8]`** instead of a
  scalar loop, since byte equality already *is* what `memchr` computes. A 50 MB
  worst-case scan went 1.59s → 0.37s. Rewriting reverse-complement's
  boundary scans on top of it, the same trick Go's `bufio` and C#'s
  `Array.IndexOf` use, brought it to 0.25s.
- **List comprehensions compile to a pre-sized buffer with inlined appends**
  rather than insert-into-an-empty-list: the result of `[expr for x in SOURCE]`
  can't exceed `SOURCE.length`, so every append is a bounds-free store (~3×
  on a microbenchmark, 0.39s → 0.13s). The fast path lives in `List.insert`, so
  all append-in-a-loop code benefits. Unsigned-int lists including `[Byte]` are
  now GC-atomic too, so the collector skips their payload.
- **One that didn't pan out:** mimicking Go's branchless line-by-line transform
  was *slower* (0.26s → 0.37s). `perf annotate` showed the multi-cursor loop
  nest defeated GCC's strength-reduction of the indexing math, trading cheap
  pointer increments for per-access multiplies that cost more than the branch
  it removed.

## Layout

```
benchmarks/
  config.json   # languages (build/run recipes) + per-benchmark program map
  bench.py      # driver: fetch / list / run / sizes
  fetch.sh      # thin wrapper: ./fetch.sh  ==  bench.py fetch
  plot.py       # results.json -> results.svg + results.png (or --sizes)
  tomo/         # TRACKED: the Tomo ports, the only source we own
  fetched/      # git-ignored: reference implementations, downloaded
  .build/       # git-ignored: compiled binaries / build scratch
  results.json  # git-ignored: measured timings
  sizes.json    # git-ignored: measured static binary sizes
```

[clbg]: https://benchmarksgame-team.pages.debian.net/benchmarksgame/
[plb]: https://github.com/hanabi1224/Programming-Language-Benchmarks
