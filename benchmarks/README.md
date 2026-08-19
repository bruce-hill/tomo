# Tomo benchmarks

Compare Tomo's performance against other languages using programs from
[The Computer Language Benchmarks Game][clbg] (CLBG).

Only the **Tomo** ports (`tomo/*.tm`) live in this repo. Every other language's
source is **downloaded on demand** into `fetched/`, which is git-ignored — so
this directory never vendors other languages' code. Most come from the CLBG
website; **Zig**, **Nim**, and **Odin** aren't covered by the CLBG, so they're
fetched from the community [Programming-Language-Benchmarks][plb] repo instead.

The PNG graphs below are a checked-in snapshot from one x86-64 Linux box (best
of 3 runs, each pinned to a single core, every output validated against the
reference). Timings are machine-specific — `results.json` and the vector
graphs are git-ignored; run the three commands below to reproduce everything
locally and regenerate these PNGs.

![Tomo vs. other languages across five benchmarks](results.png)

With the field now up to ~15 languages, the useful summary is that **Tomo sits
inside the compiled-language cluster and beats every scripting language on every
benchmark**. On the tight compute loops it lands in the middle of that cluster:
**fannkuch-redux** puts it at 0.18s, within 1.8× of the fastest and jostling
with C, Java, and C++; **n-body** at 0.27s is ~1.9× the leaders (Zig/Rust), just
ahead of Go and Java. On the hash-table-heavy **k-nucleotide** it's a strong
0.49s — top-half of the field, ahead of Java, Rust, and all the scripting
languages, with only C, C++, Go, and a warmed-up LuaJIT faster. **binary-trees**
(an allocation/GC stress test) has it at 0.31s, mid-pack and ahead of Go and
every scripting language. **fasta** is Tomo's softest benchmark but no longer an
outlier — at 0.42s it's ~1.8× C (in line with n-body), edging out Rust and
ahead of LuaJIT, Fortran, and every scripting language, with only the hand-tuned
byte-level entries (Zig, C++, Nim, C, Go, Swift, Java) clearly faster.

The top of each compute chart is now crowded with fast natives — Zig, Nim, and
Fortran routinely lead — but no garbage-collected, memory-safe language in the
set is dramatically ahead of Tomo, and the scripting languages trail it
everywhere.

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
| Active | C (gcc), C++ (g++), Rust, Go, Zig, Nim, Odin, Java, C#, Swift, Fortran, JavaScript (node), Lua, LuaJIT, Python, PyPy, **Tomo** |

Not every language implements every benchmark. A language runs only the
benchmarks it has a validated program for; the rest are simply absent from that
chart. Coverage gaps (and why they exist) are noted below.

C# uses **Native AOT** (`dotnet publish` with `PublishAot`), matching the
CLBG `csharpaot` entries: the driver writes a minimal AOT `.csproj`, drops the
fetched source in as `Program.cs`, and runs the resulting standalone native
binary — a fair peer to the other compiled languages, with none of the JIT/
runtime startup a `dotnet foo.dll` launch adds to every short run. It needs the
.NET SDK plus `clang` (for the final native link); machines without both are
skipped. The first AOT build restores the ILCompiler package from NuGet, so it
needs network access once.

Swift is compiled ahead-of-time with `swiftc -O`. A few CLBG Swift entries are
too old for a current toolchain (Swift 6.x) or skip the benchmark's required
output format, so the slugs are chosen to avoid those: **n-body** uses
`swift-3` (`swift-1` prints unformatted doubles instead of `%.9f`) and
**k-nucleotide** uses `swift-2` (`swift-1` calls the long-removed
`String.characters`).

Fortran is compiled with `gfortran -O3 -march=native`. It runs in four of the
five benchmarks; CLBG has no Fortran k-nucleotide entry (the page is a stub
noting the lack of a standard Fortran hash table), so it's skipped there.

Zig, Nim, and Odin are the non-CLBG languages: their sources come from the
community [Programming-Language-Benchmarks][plb] repo (raw files, not
HTML-extracted — see `source: "plb"` in `config.json`), and a program's slug is
just the repo's filename stem (e.g. `"2"` → `2.nim`).

- **Zig** builds with `zig build-exe -OReleaseFast -lc`. Those sources target
  Zig ~0.14 (before the 0.15 `std.io`/`process.args` rework), so the driver
  prefers a `zig0.14` binary on PATH (override with `ZIG=...`); a bleeding-edge
  `zig` alone won't compile them. Runs four of five — its k-nucleotide entry
  reads an input *file path* rather than stdin, which doesn't fit the
  `stdin_fasta` harness.
- **Nim** builds with `nim c -d:danger`. The repo has Nim for n-body,
  binary-trees, and fasta only (three of five) — no fannkuch or k-nucleotide
  entry exists to fetch.
- **Odin** builds with `odin build -o:speed`. The repo has Odin for n-body,
  binary-trees, and k-nucleotide, but the k-nucleotide entry doesn't compile on
  a current Odin (`os.stream_from_handle` was removed), leaving two of five.

The PLB benchmark directory names differ slightly from ours
(`fannkuch-redux` vs `fannkuchredux`); `bench.py`'s `PLB_ALGO` maps between
them.

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

PyPy runs the same fetched sources as CPython (`source_from: "python"`), via
`pypy3` — so it needs a Python **3** PyPy (the `pypy` 2.7 binary won't run the
Python-3 entries). It covers all five.

LuaJIT runs the same fetched source as Lua. LuaJIT is Lua 5.1, and a few CLBG
Lua entries call 5.2+ names (e.g. `table.unpack` in the fasta entry), so the
`luajit` config carries a one-line `prelude` — `if not table.unpack then
table.unpack=unpack end` — run via `luajit -e` before the script. That shims
the 5.1/5.2 gap without editing the fetched source, so Lua and LuaJIT run
byte-identical programs. (k-nucleotide's Lua entry needs no shim; it just
hadn't been wired up for LuaJIT before.)

Rust is omitted from **binary-trees**: every CLBG Rust entry links an external
arena crate (`typed_arena`, `bumpalo`) and/or `rayon`, none vendored here. The
C++ entry (`gpp-2`) is the self-contained one; the faster `gpp-1`/`gpp-3` need
Boost.Pool. Tomo uses its own GC and heap pointers, no arena — a node is a
self-referential struct with two optional `@Tree?` children.

C# is omitted from **k-nucleotide**: its CLBG entries depend on the external
`Microsoft.Collections.DictionarySlim` NuGet package (much like the Java
entries there need `fastutil`), which isn't vendored here.

[clbg]: https://benchmarksgame-team.pages.debian.net/benchmarksgame/
[plb]: https://github.com/hanabi1224/Programming-Language-Benchmarks
