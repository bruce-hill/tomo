#!/usr/bin/env python3
"""Cross-language benchmark driver for comparing Tomo against other languages.

Subcommands:
    fetch [benchmark...]   Download reference implementations from the
                           Computer Language Benchmarks Game into fetched/.
    run   [benchmark...]   Build + time + validate every runnable language,
                           writing results to results.json.
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
# Build with the repo's own in-tree compiler, not whatever `tomo` happens to
# be on PATH — an installed release build can lag behind language features
# used by the ports (e.g. `for x at i in xs`), and silently mis-parse them.
LOCAL_TOMO = os.path.join(HERE, "..", "local-tomo")


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
            url = f"{site}/{bname}-{slug}.html"
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
        return bool(shutil.which("dotnet"))
    exe = (spec.get("build") or spec.get("run"))[0]
    if exe == "{tomo}":
        return os.access(LOCAL_TOMO, os.X_OK)
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
def run_once(cmd, cwd=None, stdin_path=None):
    stdin = open(stdin_path, "rb") if stdin_path else subprocess.DEVNULL
    t0 = time.perf_counter()
    p = subprocess.Popen(cmd, cwd=cwd, stdout=subprocess.PIPE,
                         stderr=subprocess.DEVNULL, stdin=stdin)
    out = p.stdout.read()
    _, status, ru = os.wait4(p.pid, 0)
    wall = time.perf_counter() - t0
    if stdin_path:
        stdin.close()
    rc = os.waitstatus_to_exitcode(status)
    return wall, ru.ru_maxrss, out, rc


def expand(template, **kw):
    return [kw.get(t[1:-1], t) if t.startswith("{") and t.endswith("}") else t
            for t in template]


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
        m = re.search(r"public\s+(?:final\s+)?class\s+(\w+)", open(src).read())
        cls = m.group(1) if m else "Main"
        jsrc = os.path.join(bdir, f"{cls}.java")
        shutil.copyfile(src, jsrc)
        _check(["javac", "-d", bdir, jsrc])
        return ["java", "-cp", bdir, cls]

    if handler == "csharp":
        proj = os.path.join(bdir, "cs")
        if not os.path.exists(proj):
            os.makedirs(proj)
            _check(["dotnet", "new", "console", "-o", proj], quiet=True)
        shutil.copyfile(src, os.path.join(proj, "Program.cs"))
        _check(["dotnet", "build", "-c", "Release", "-o",
                os.path.join(proj, "out"), proj], quiet=True)
        dll = next(f for f in os.listdir(os.path.join(proj, "out"))
                   if f.endswith(".dll") and "Microsoft" not in f)
        return ["dotnet", os.path.join(proj, "out", dll)]

    if "build" in spec:
        binpath = os.path.join(bdir, lang)
        _check(expand(spec["build"], src=src, bin=binpath, tomo=LOCAL_TOMO))
        return expand(spec["run"], src=src, bin=binpath, tomo=LOCAL_TOMO)

    return expand(spec["run"], src=src, bin="", tomo=LOCAL_TOMO)


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
    repeats = int(os.environ.get("BENCH_REPEATS", cfg.get("repeats", 3)))
    results = {}
    if os.path.exists(RESULTS):
        results = json.load(open(RESULTS))

    for bname in benchmarks:
        bench = cfg["benchmarks"][bname]
        args = [str(a) for a in bench.get("args", [])]
        if os.environ.get("BENCH_ARGS"):
            args = os.environ["BENCH_ARGS"].split()
        stdin_path = None  # (future: benchmarks that read stdin)
        ref_lang = bench.get("reference", "c")
        print(f"\n=== {bname}  args={args} ===")

        # Order: reference first (defines expected output), then Tomo (the
        # language under test), then the remaining reference languages.
        rest = [l for l in _langs_for(cfg, bench) if l not in (ref_lang, "tomo")]
        order = [ref_lang, "tomo"] + rest
        expected = None
        rows = {}
        for lang in order:
            spec = cfg["languages"][lang]
            if spec.get("disabled"):
                print(f"  skip {lang:<11} (disabled)")
                continue
            if not tool_available(spec):
                print(f"  skip {lang:<11} (toolchain missing)")
                continue
            try:
                cmd = prepare(cfg, bname, lang) + args
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
            if lang == ref_lang:
                expected = norm
            ok = expected is not None and norm == expected
            mark = "ok " if ok else "OUT"  # OUT = output mismatch
            rows[lang] = {"seconds": round(best, 4),
                          "rss_kb": rss,
                          "valid": bool(ok),
                          "label": spec["label"],
                          "color": spec.get("color", "#888888")}
            print(f"  {mark}  {lang:<11} {best:8.3f}s  {rss/1024:7.1f} MB"
                  f"  {'valid' if ok else 'OUTPUT MISMATCH'}")

        results[bname] = {"args": args, "reference": ref_lang, "results": rows}

    with open(RESULTS, "w") as f:
        json.dump(results, f, indent=2)
    print(f"\nwrote {RESULTS}")


def _langs_for(cfg, bench):
    return list(bench["programs"].keys()) + ["tomo"]


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
    elif cmd == "list":
        cmd_list(cfg)
    else:
        print(f"unknown command: {cmd}")
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main()
