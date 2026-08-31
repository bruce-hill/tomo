#!/usr/bin/env python3
"""Cross-language benchmark driver for comparing Tomo against other languages.

Subcommands:
    fetch [benchmark...]   Download reference implementations from the
                           Computer Language Benchmarks Game into fetched/.
    run   [benchmark...]   Build + time + validate every runnable language,
                           writing results to results.json. Set BENCH_LANGS=
                           tomo,go to re-time only those languages, preserving
                           the others' existing rows (the reference language is
                           always run to re-validate output). Aborts if the
                           machine is on battery (turbo throttling skews
                           timings); set BENCH_ALLOW_BATTERY=1 to override.
    sizes [benchmark...]   Statically build every compiled language that can
                           produce a standalone binary and record the binary
                           size as built, writing sizes.json.
    list                   Show benchmarks, languages, and toolchain status.

Only Tomo sources (tomo/*.tm) are tracked in git; everything under fetched/
and .build/ is downloaded/generated and gitignored.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import time
import html

HERE = os.path.dirname(os.path.abspath(__file__))
FETCHED = os.path.join(HERE, "fetched")
BUILD = os.path.join(HERE, ".build")
RESULTS = os.path.join(HERE, "results.json")
SIZES = os.path.join(HERE, "sizes.json")
# Build with the repo's own in-tree compiler, not whatever `tomo` happens to
# be on PATH, since an installed release build can lag behind language
# features used by the ports (e.g. `for x at i in xs`), and silently mis-parse
# them.
LOCAL_TOMO = os.path.join(HERE, "..", "local-tomo")

# C# is compiled with Native AOT (matching CLBG's `csharpaot` entries) so it
# produces a standalone native binary, a fair peer to the other compiled
# languages, with none of the ~25 ms JIT/runtime startup a `dotnet foo.dll`
# launch would add to every short benchmark. Needs `clang` for the final link.
# Zig is fetched from the community Programming-Language-Benchmarks repo (the
# CLBG has no Zig entries) and built with a specific interpreter: the fetched
# sources target Zig ~0.14 (before the 0.15 std.io / process.args rework), so
# prefer a `zig0.14` on PATH, overridable with $ZIG.
ZIG_BIN = os.environ.get("ZIG") or shutil.which("zig0.14") or "zig"
PLB_RAW = ("https://raw.githubusercontent.com/hanabi1224/"
           "Programming-Language-Benchmarks/main/bench/algorithm")
# Our benchmark name -> the repo's algorithm directory (default: same name).
PLB_ALGO = {"fannkuchredux": "fannkuch-redux"}

CSPROJ_AOT = """<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <OutputType>Exe</OutputType>
    <TargetFramework>net10.0</TargetFramework>
    <ImplicitUsings>enable</ImplicitUsings>
    <Nullable>disable</Nullable>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <Optimize>true</Optimize>
    <PublishAot>true</PublishAot>
    <InvariantGlobalization>true</InvariantGlobalization>
  </PropertyGroup>
</Project>
"""


def load_config():
    with open(os.path.join(HERE, "config.json")) as f:
        return json.load(f)


# ---------------------------------------------------------------------------
# fetch
# ---------------------------------------------------------------------------
def extract_source(page_html):
    """Pull the program source out of a CLBG program page's <pre> block."""
    m = re.search(r"<pre[^>]*>(.*?)</pre>", page_html, re.S)
    if not m:
        raise ValueError("no <pre> block found")
    text = re.sub(r"<[^>]+>", "", m.group(1))
    return html.unescape(text)


def fetch(cfg, benchmarks):
    site = cfg["site"]
    langs = cfg["languages"]
    for bname in benchmarks:
        bench = cfg["benchmarks"][bname]
        outdir = os.path.join(FETCHED, bname)
        os.makedirs(outdir, exist_ok=True)
        for lang, slug in bench["programs"].items():
            spec = langs[lang]
            if "source_from" in spec:
                continue  # reuses another language's source at run time
            ext = spec["ext"]
            dest = os.path.join(outdir, f"{lang}.{ext}")
            # A language can source from the community Programming-Language-
            # Benchmarks repo (raw file, no HTML extraction) instead of CLBG.
            if spec.get("source") == "plb":
                algo = PLB_ALGO.get(bname, bname)
                url = f"{PLB_RAW}/{algo}/{slug}.{ext}"
                try:
                    with open(dest, "w") as f:
                        f.write(curl(url))
                    print(f"  ok   {bname}/{lang:<11} <- plb {algo}/{slug}.{ext}",
                          flush=True)
                except Exception as e:
                    print(f"  FAIL {bname}/{lang:<11} {url}\n       {e}", flush=True)
                continue
            # The CLBG's program URLs use a short slug for some benchmarks that
            # differs from our (more descriptive) name, e.g. reverse-complement
            # lives under `revcomp-*`. `site_name` overrides the URL stem.
            site_bname = bench.get("site_name", bname)
            url = f"{site}/{site_bname}-{slug}.html"
            try:
                page = curl(url)
                src = extract_source(page)
                with open(dest, "w") as f:
                    f.write(src)
                print(f"  ok   {bname}/{lang:<11} <- {bname}-{slug}", flush=True)
            except Exception as e:
                print(f"  FAIL {bname}/{lang:<11} {url}\n       {e}", flush=True)


def curl(url):
    r = subprocess.run(["curl", "-fsSL", "--max-time", "30", url],
                       capture_output=True)
    if r.returncode != 0:
        raise RuntimeError(f"curl exit {r.returncode}: {r.stderr.decode()[:200]}")
    return r.stdout.decode("utf-8", "replace")


# ---------------------------------------------------------------------------
# toolchain / source resolution
# ---------------------------------------------------------------------------
def tool_available(spec):
    handler = spec.get("handler")
    if handler == "java":
        return shutil.which("javac") and shutil.which("java")
    if handler == "csharp":
        # Native AOT needs the .NET SDK plus clang to link the native binary.
        return bool(shutil.which("dotnet") and shutil.which("clang"))
    exe = (spec.get("build") or spec.get("run"))[0]
    if exe == "{tomo}":
        return os.access(LOCAL_TOMO, os.X_OK)
    if exe == "{zig}":
        return bool(shutil.which(ZIG_BIN))
    if exe.startswith("{"):
        exe = spec["run"][0]
    return bool(shutil.which(exe))


def source_path(cfg, bname, lang):
    spec = cfg["languages"][lang]
    if spec.get("tracked"):
        return os.path.join(HERE, "tomo", f"{bname}.{spec['ext']}")
    src_lang = spec.get("source_from", lang)
    src_spec = cfg["languages"][src_lang]
    return os.path.join(FETCHED, bname, f"{src_lang}.{src_spec['ext']}")


# ---------------------------------------------------------------------------
# run one process, measuring wall time and peak RSS
# ---------------------------------------------------------------------------
# Every timed run is pinned to a single core (taskset). Several CLBG programs
# are multithreaded (e.g. fannkuchredux go-1 hardcodes GOMAXPROCS(4); gpp-1
# uses std::async), so unpinned wall-clock times would compare 1-core naive
# programs against N-core parallel ones. Pinning makes the numbers a
# same-resources, language-vs-language comparison. Override the core with
# BENCH_CPU, or set BENCH_CPU=all to disable pinning.
def _pin(cmd):
    cpu = os.environ.get("BENCH_CPU", "0")
    if cpu == "all" or not shutil.which("taskset"):
        return cmd
    return ["taskset", "-c", cpu] + cmd


def fasta_input(n):
    """Produce FASTA output at scale `n` to feed as stdin to k-nucleotide-style
    benchmarks. Built once with the fetched C fasta generator (the canonical,
    deterministic producer) and cached per `n` under .build/."""
    src = os.path.join(FETCHED, "fasta", "c.c")
    if not os.path.exists(src):
        raise RuntimeError("fasta source missing; run: python3 bench.py fetch fasta")
    os.makedirs(BUILD, exist_ok=True)
    gen = os.path.join(BUILD, "fasta_gen")
    if not os.path.exists(gen) or os.path.getmtime(gen) < os.path.getmtime(src):
        _check(["gcc", "-O3", "-o", gen, src, "-lm"])
    out = os.path.join(BUILD, f"fasta_input_{n}.txt")
    if not os.path.exists(out) or os.path.getsize(out) == 0:
        with open(out, "wb") as f:
            if subprocess.run([gen, str(n)], stdout=f).returncode != 0:
                raise RuntimeError("fasta generator failed")
    return out


def on_battery(base="/sys/class/power_supply"):
    """True if running on battery, False if on AC, None if undeterminable.

    Battery power caps sustained CPU turbo, which reproducibly slows timings
    even when pinned to one core, enough to make results.json inconsistent
    with rows recorded while plugged in. Reads the Linux power-supply sysfs;
    returns None on other platforms or when there is no AC/battery to inspect
    (e.g. a desktop, or an empty sysfs), so the caller can proceed rather
    than block.
    """
    if not os.path.isdir(base):
        return None

    def _read(name, field):
        try:
            with open(os.path.join(base, name, field)) as f:
                return f.read().strip()
        except OSError:
            return None

    ac_verdict = None       # from a Mains adapter's `online`
    battery_verdict = None  # fallback from a Battery's `status`
    for name in os.listdir(base):
        kind = _read(name, "type")
        if kind == "Mains":
            online = _read(name, "online")
            if online == "1":
                return False  # a plugged-in AC adapter -> definitely on AC
            if online == "0":
                ac_verdict = True  # AC adapter present but offline -> battery
        elif kind == "Battery":
            status = _read(name, "status")
            if status == "Discharging":
                battery_verdict = True
            elif status in ("Charging", "Full", "Not charging"):
                battery_verdict = False
    return ac_verdict if ac_verdict is not None else battery_verdict


def require_ac_power():
    """Abort a timing run when on battery, unless BENCH_ALLOW_BATTERY is set."""
    if on_battery() and not os.environ.get("BENCH_ALLOW_BATTERY"):
        sys.exit(
            "error: running on battery power. CPU turbo is throttled on "
            "battery, which produces slower, inconsistent timings.\n"
            "       Plug in and re-run, or set BENCH_ALLOW_BATTERY=1 to "
            "override.")


def run_once(cmd, cwd=None, stdin_path=None):
    stdin = open(stdin_path, "rb") if stdin_path else subprocess.DEVNULL
    t0 = time.perf_counter()
    p = subprocess.Popen(_pin(cmd), cwd=cwd, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, stdin=stdin)
    out = p.stdout.read()
    _, status, ru = os.wait4(p.pid, 0)
    wall = time.perf_counter() - t0
    if stdin_path:
        stdin.close()
    rc = os.waitstatus_to_exitcode(status)
    return wall, ru.ru_maxrss, out, rc


def expand(template, **kw):
    # Substitute {key} placeholders anywhere in each token (not just tokens that
    # are entirely a placeholder), so forms like `-femit-bin={bin}` work.
    out = []
    for t in template:
        for k, v in kw.items():
            t = t.replace("{" + k + "}", v)
        out.append(t)
    return out


# ---------------------------------------------------------------------------
# build + prepare a runnable command for one (benchmark, language)
# ---------------------------------------------------------------------------
def prepare(cfg, bname, lang):
    """Return a run command (list) ready to have args appended, or raise."""
    spec = cfg["languages"][lang]
    src = source_path(cfg, bname, lang)
    if not os.path.exists(src):
        raise FileNotFoundError(f"source missing: {src}")
    bdir = os.path.join(BUILD, bname)
    os.makedirs(bdir, exist_ok=True)
    handler = spec.get("handler")

    if handler == "java":
        # Match the top-level class declaration (`public`/`final` optional --
        # some entries, e.g. fasta, declare a package-private `class fasta`).
        m = re.search(r"^(?:public\s+)?(?:final\s+)?class\s+(\w+)",
                      open(src).read(), re.MULTILINE)
        cls = m.group(1) if m else "Main"
        jsrc = os.path.join(bdir, f"{cls}.java")
        shutil.copyfile(src, jsrc)
        _check(["javac", "-d", bdir, jsrc])
        return ["java", "-cp", bdir, cls]

    if handler == "csharp":
        proj = os.path.join(bdir, "cs")
        os.makedirs(proj, exist_ok=True)
        with open(os.path.join(proj, "cs.csproj"), "w") as f:
            f.write(CSPROJ_AOT)
        shutil.copyfile(src, os.path.join(proj, "Program.cs"))
        outdir = os.path.join(proj, "out")
        _check(["dotnet", "publish", "-c", "Release", "-o", outdir, proj],
               quiet=True)
        return [os.path.join(outdir, "cs")]  # the AOT-linked native binary

    if "build" in spec:
        binpath = os.path.join(bdir, lang)
        # Per-benchmark, per-language extra build flags (e.g. an -I path for a
        # header-only dependency like klib/khash that k-nucleotide's C entry
        # needs). Absent for most benchmarks.
        cflags = cfg["benchmarks"][bname].get("cflags", {}).get(lang, [])
        _check(expand(spec["build"], src=src, bin=binpath, tomo=LOCAL_TOMO,
                      zig=ZIG_BIN) + cflags)
        return expand(spec["run"], src=src, bin=binpath, tomo=LOCAL_TOMO,
                      zig=ZIG_BIN)

    run = expand(spec["run"], src=src, bin="", tomo=LOCAL_TOMO, zig=ZIG_BIN)
    # A `prelude` is a one-liner run via the interpreter's `-e` before the
    # script, used to shim LuaJIT (Lua 5.1 semantics) up to the handful of
    # 5.2+ names a few CLBG Lua entries expect (e.g. `table.unpack`), so we can
    # run the *same* fetched source under both Lua and LuaJIT without editing
    # it. The script still sees its own argv.
    prelude = spec.get("prelude")
    if prelude:
        run = [run[0], "-e", prelude] + run[1:]
    return run


BUILD_TIMEOUT = int(os.environ.get("BENCH_BUILD_TIMEOUT", "180"))


def _check(cmd, quiet=False):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True,
                           timeout=BUILD_TIMEOUT)
    except subprocess.TimeoutExpired:
        raise RuntimeError(f"{cmd[0]} timed out after {BUILD_TIMEOUT}s")
    if r.returncode != 0:
        raise RuntimeError(f"{cmd[0]} failed:\n{r.stderr[-800:]}")


# ---------------------------------------------------------------------------
# run
# ---------------------------------------------------------------------------
def run(cfg, benchmarks):
    require_ac_power()
    repeats = int(os.environ.get("BENCH_REPEATS", cfg.get("repeats", 3)))
    # BENCH_LANGS=tomo,go re-times only those languages and preserves every
    # other language's existing row in results.json (handy when only the Tomo
    # compiler changed). The reference language is always run to regenerate the
    # expected output for validation, but its stored row is kept as-is unless it
    # too was named.
    only_env = os.environ.get("BENCH_LANGS")
    only = {l.strip() for l in only_env.split(",") if l.strip()} if only_env else None
    results = {}
    if os.path.exists(RESULTS):
        results = json.load(open(RESULTS))

    for bname in benchmarks:
        bench = cfg["benchmarks"][bname]
        args = [str(a) for a in bench.get("args", [])]
        if os.environ.get("BENCH_ARGS"):
            args = os.environ["BENCH_ARGS"].split()
        # `stdin_fasta` benchmarks (e.g. k-nucleotide) read a FASTA file on
        # stdin instead of taking CLI args: `args` sizes the generated input,
        # and the programs themselves are invoked with no arguments.
        stdin_path = None
        prog_args = args
        if bench.get("stdin_fasta"):
            stdin_path = fasta_input(args[0])
            prog_args = []
        ref_lang = bench.get("reference", "c")
        print(f"\n=== {bname}  args={args} ===")

        # Order: reference first (defines expected output), then Tomo (the
        # language under test), then the remaining reference languages.
        rest = [l for l in _langs_for(cfg, bench) if l not in (ref_lang, "tomo")]
        order = [ref_lang, "tomo"] + rest
        expected = None
        prev_rows = results.get(bname, {}).get("results", {})
        rows = {}
        for lang in order:
            spec = cfg["languages"][lang]
            is_ref = lang == ref_lang
            # With BENCH_LANGS set, skip languages that weren't named (carrying
            # their prior row forward). The reference is never skipped: we need
            # its fresh output to validate the languages we *are* re-running.
            requested = only is None or lang in only
            if not requested and not is_ref:
                if lang in prev_rows:
                    rows[lang] = prev_rows[lang]
                else:
                    print(f"  skip {lang:<11} (not in BENCH_LANGS, no prior result)")
                continue
            if spec.get("disabled"):
                print(f"  skip {lang:<11} (disabled)")
                continue
            if not tool_available(spec):
                print(f"  skip {lang:<11} (toolchain missing)")
                continue
            try:
                cmd = prepare(cfg, bname, lang) + prog_args
            except Exception as e:
                print(f"  skip {lang:<11} ({str(e).splitlines()[0]})")
                continue

            best = None
            rss = None
            out = None
            rc = 0
            for _ in range(repeats):
                w, r_rss, out, rc = run_once(cmd, stdin_path=stdin_path)
                if rc != 0:
                    break
                best = w if best is None else min(best, w)
                rss = r_rss if rss is None else max(rss, r_rss)
            if rc != 0:
                print(f"  FAIL {lang:<11} exit={rc}")
                continue

            norm = out.strip()
            if is_ref:
                expected = norm
            ok = expected is not None and norm == expected
            mark = "ok " if ok else "OUT"  # OUT = output mismatch
            fresh_row = {"seconds": round(best, 4),
                         "rss_kb": rss,
                         "valid": bool(ok),
                         "label": spec["label"],
                         "color": spec.get("color", "#888888")}
            if not requested and is_ref and lang in prev_rows:
                # Reference was run only to regenerate the expected output;
                # keep its stored timing rather than this validation run's.
                rows[lang] = prev_rows[lang]
                print(f"  ref  {lang:<11} {best:8.3f}s  (validated; stored row preserved)")
            else:
                rows[lang] = fresh_row
                print(f"  {mark}  {lang:<11} {best:8.3f}s  {rss/1024:7.1f} MB"
                      f"  {'valid' if ok else 'OUTPUT MISMATCH'}")

        results[bname] = {"args": args, "reference": ref_lang, "results": rows}

    with open(RESULTS, "w") as f:
        json.dump(results, f, indent=2)
    print(f"\nwrote {RESULTS}")


def _langs_for(cfg, bench):
    return list(bench["programs"].keys()) + ["tomo"]


# ---------------------------------------------------------------------------
# sizes: compare compiled *static* binary sizes across languages
# ---------------------------------------------------------------------------
# Only languages that can produce a standalone statically-linked binary are
# included, so the comparison is like-for-like (a self-contained executable,
# no external libc/runtime hidden off to the side). A language qualifies if it
# is static by default (`static_default`, e.g. Tomo's musl build, Go) or has a
# `size_build` recipe that forces static linking (C/C++ `-static`, Rust
# crt-static, Nim `-static`, Zig musl target). Interpreted and bytecode
# languages (Python, Lua, JS, Java) have no such binary and are skipped, as are
# toolchains that can't statically link on this box (Swift, Odin, Fortran).
#
# Sizes are recorded exactly as each toolchain produces them. Stripping first
# would measure how small each language *can* be made with extra tooling; what
# a program actually weighs when you build it is the honest comparison, and it
# is what anyone shipping one has to carry.
def _size_build_cmd(spec):
    if spec.get("size_build"):
        return spec["size_build"]
    if spec.get("static_default"):
        return spec["build"]
    return None


def _is_static(path):
    """Best-effort: does `file` report this ELF as statically linked?"""
    try:
        out = subprocess.run(["file", "-L", path], capture_output=True,
                             text=True).stdout
    except Exception:
        return None
    return ("statically linked" in out) or ("static-pie linked" in out)


def sizes(cfg, benchmarks):
    results = {}
    if os.path.exists(SIZES):
        results = json.load(open(SIZES))
    for bname in benchmarks:
        bench = cfg["benchmarks"][bname]
        print(f"\n=== {bname}  (static binary size) ===")
        rows = {}
        for lang in _langs_for(cfg, bench):
            spec = cfg["languages"][lang]
            build = _size_build_cmd(spec)
            if build is None:
                continue  # not a statically-linkable compiled language
            if spec.get("disabled") or not tool_available(spec):
                print(f"  skip {lang:<11} (toolchain missing)")
                continue
            src = source_path(cfg, bname, lang)
            if not os.path.exists(src):
                print(f"  skip {lang:<11} (source missing, run fetch)")
                continue
            bdir = os.path.join(BUILD, bname, "size")
            os.makedirs(bdir, exist_ok=True)
            binpath = os.path.join(bdir, lang)
            cflags = bench.get("cflags", {}).get(lang, [])
            try:
                _check(expand(build, src=src, bin=binpath, tomo=LOCAL_TOMO,
                              zig=ZIG_BIN) + cflags)
            except Exception as e:
                print(f"  FAIL {lang:<11} ({str(e).splitlines()[0]})")
                continue
            if not os.path.exists(binpath):
                print(f"  FAIL {lang:<11} (no binary produced)")
                continue
            size = os.path.getsize(binpath) # as built; see above
            static = _is_static(binpath)
            rows[lang] = {"bytes": size, "static": static, "label": spec["label"],
                          "color": spec.get("color", "#888888")}
            tag = "static" if static else "DYNAMIC!"
            print(f"  ok   {lang:<11} {size:>12,} B  [{tag}]  {spec['label']}")
        results[bname] = {"results": rows}
    with open(SIZES, "w") as f:
        json.dump(results, f, indent=2)
    print(f"\nwrote {SIZES}")


def cmd_list(cfg):
    print("Benchmarks:", ", ".join(cfg["benchmarks"]))
    print("\nLanguages (toolchain status):")
    for lang, spec in cfg["languages"].items():
        ok = "available" if tool_available(spec) else "MISSING"
        print(f"  {lang:<12} {spec['label']:<20} {ok}")


def main():
    cfg = load_config()
    argv = sys.argv[1:]
    if not argv:
        print(__doc__)
        return
    cmd = argv[0]
    picks = argv[1:] or list(cfg["benchmarks"].keys())
    if cmd == "fetch":
        fetch(cfg, picks)
    elif cmd == "run":
        run(cfg, picks)
    elif cmd == "sizes":
        sizes(cfg, picks)
    elif cmd == "list":
        cmd_list(cfg)
    else:
        print(f"unknown command: {cmd}")
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main()
