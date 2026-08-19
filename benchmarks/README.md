# Tomo benchmarks

Compare Tomo's performance against other languages using programs from
[The Computer Language Benchmarks Game][clbg] (CLBG).

Only the **Tomo** ports (`tomo/*.tm`) live in this repo. Every other language's
source is **downloaded on demand** from the CLBG website into `fetched/`, which
is git-ignored — so this directory never vendors other languages' code.

The PNG graphs below are a checked-in snapshot from one x86-64 Linux box (best
of 3 runs, each pinned to a single core, every output validated against the
reference). Timings are machine-specific — `results.json` and the vector
graphs are git-ignored; run the three commands below to reproduce everything
locally and regenerate these PNGs.

![Tomo vs. other languages across four CLBG benchmarks](results.png)

Tomo runs shoulder-to-shoulder with the compiled languages on the tight
compute loops (**n-body**, **fannkuch-redux**), and on the hash-table-heavy
**k-nucleotide** it places 4th — ahead of Java, Rust, and every scripting
language. **binary-trees** is an allocation/GC stress test, and Tomo again
places 4th — behind only AOT Java, C, and C++, and ahead of Go, Node, and every
scripting language. **fasta** is the outlier, where hand-tuned byte-level
output gives the low-level entries a wider edge, but Tomo still beats Lua,
Node, and Python.

Per-benchmark graphs: [n-body](results-nbody.png) ·
[fannkuch-redux](results-fannkuchredux.png) · [fasta](results-fasta.png) ·
[k-nucleotide](results-knucleotide.png) ·
[binary-trees](results-binarytrees.png).

## Layout

```
benchmarks/
  config.json      # languages (build/run recipes) + per-benchmark program map
  bench.py         # driver: fetch / run / list
  fetch.sh         # thin wrapper: ./fetch.sh  ==  bench.py fetch
  plot.py          # results.json -> results.svg + results.png
  tomo/            # TRACKED — the Tomo ports (the only source we own)
    nbody.tm
    fannkuchredux.tm
    fasta.tm
    knucleotide.tm
    binarytrees.tm
  fetched/         # git-ignored — reference implementations, downloaded
  .build/          # git-ignored — compiled binaries / build scratch
  results.json     # git-ignored — measured timings
```

## Usage

```sh
./fetch.sh                 # download reference implementations for all benchmarks
python3 bench.py list      # show benchmarks + which toolchains are installed
python3 bench.py run       # build, run, validate, and time every language
python3 plot.py            # render results.svg and results.png
```

Environment overrides for quick iteration:

- `BENCH_ARGS="1000"` — override the benchmark input (e.g. a fast smoke test).
- `BENCH_REPEATS=1` — number of timed runs per language (best time is kept).
- `BENCH_BUILD_TIMEOUT=180` — per-language build timeout in seconds.
- `BENCH_CPU=2` — which core to pin runs to (default `0`); `BENCH_CPU=all`
  disables pinning.

## How a run works

For each benchmark, the **reference** language (C) runs first and its output
becomes the expected result. Every other language must reproduce that output
byte-for-byte or it is flagged as an output mismatch (a program can't post a
fast time by computing the wrong thing). Each language is run `repeats` times
and the best wall-clock time is kept, along with peak RSS.

**Every timed run is pinned to a single core** (`taskset -c 0`). Several CLBG
programs are multithreaded — fannkuch-redux's `go-1` hardcodes
`runtime.GOMAXPROCS(4)`, `gpp-1` fans out with `std::async`, and the Rust and
Java entries spawn threads — while the C reference and the Tomo ports are
single-threaded. Unpinned, the wall-clock chart would compare 1-core programs
against N-core ones; pinned, it's a same-resources, language-vs-language
comparison.

## Ground rules for the Tomo ports

- **No inline C for computation.** The Tomo ports implement the actual
  algorithm in pure Tomo. The *only* permitted `C_code` use is formatting the
  final numeric output with `printf` (Tomo has no `%.9f`-style zero-padded
  float formatting), matching the benchmark's required output exactly.
- Ports use the same algorithm as the reference programs, sized by the same
  input, and validated against the reference output.

## Languages

| Status | Languages |
|--------|-----------|
| Active | C (gcc), C++ (g++), Rust, Go, Java, JavaScript (node), Lua, LuaJIT, Python, **Tomo** |
| Disabled | C# — needs a project-style `dotnet build`; disabled in `config.json` for now |
| Skipped here | Swift — no `swiftc` toolchain installed on this machine |

A language with no installed toolchain is skipped with a message rather than
failing the run.

## Benchmarks

Currently implemented: **n-body**, **fannkuch-redux**, **fasta**,
**k-nucleotide**, **binary-trees**. The plan is to grow the library-free core
set (spectral-norm, mandelbrot, reverse-complement) one at a time, each with a
validated Tomo port.

**k-nucleotide** reads a FASTA file on stdin: the driver generates that input
with the C fasta program at the configured scale and pipes it to each
implementation (see `stdin_fasta` in `config.json`). The C entry depends on
klib's `khash.h`; the benchmark's `cflags` adds `-I/usr/include/klib`, so C is
skipped on machines where klib isn't installed there. Its Java entry uses
`graalvmaot-3` (the `-1`/`-2` entries need the external `fastutil` library).

LuaJIT is omitted from **fasta**: the CLBG Lua entries for it use
`table.unpack` (Lua 5.2+), which LuaJIT (5.1 semantics) does not provide, and
we don't patch fetched sources.

Rust is omitted from **binary-trees**: every CLBG Rust entry links an external
arena crate (`typed_arena`, `bumpalo`) and/or `rayon`, none vendored here. The
C++ entry (`gpp-2`) is the self-contained one; the faster `gpp-1`/`gpp-3` need
Boost.Pool. Tomo uses its own GC and heap pointers, no arena — a node is a
self-referential struct with two optional `@Tree?` children.

[clbg]: https://benchmarksgame-team.pages.debian.net/benchmarksgame/
