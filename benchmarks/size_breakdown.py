#!/usr/bin/env python3
"""Render size-breakdown.png: where a compiled Tomo binary's bytes come from.

Builds a small program that exercises bignum `Int` and exact-real `Num`
arithmetic (so GMP and the number engine are genuinely linked), re-links it
with `--print-map`, and attributes every byte of the file to the archive it
came from. The donut this draws is the answer to "why is a Tomo binary the
size it is": mostly GMP, Unicode tables, and the runtime, with the program
itself a sliver.

Usage: python3 size_breakdown.py   (writes size-breakdown.svg and .png)
"""
import glob
import math
import os
import re
import subprocess
import sys
import collections

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(HERE, ".build", "breakdown")
LOCAL_TOMO = os.path.join(HERE, "..", "local-tomo")

SURFACE, INK, MUTED = "#fcfcfb", "#202124", "#5f6368"
# Categorical hues in fixed order (same palette family as plot.py's accent).
HUES = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100",
        "#e87ba4", "#008300", "#4a3aa7", "#e34948"]

SAMPLE = """\
func main()
    big := Int(2)^128 + Int(1)
    exact := Num(1)/3 + Num.PI
    say("$big  $exact  $(exact.digits(12))")
"""


def build_with_map():
    """Build the sample, then re-run its link with --print-map captured."""
    os.makedirs(BUILD, exist_ok=True)
    src = os.path.join(BUILD, "breakdown.tm")
    binpath = os.path.join(BUILD, "breakdown")
    with open(src, "w") as f:
        f.write(SAMPLE)
    out = subprocess.run([LOCAL_TOMO, "build", "-O3", "-f", "--verbose",
                          "-o", binpath, src],
                         capture_output=True, text=True)
    # The verbose log prints the exact link command (in ANSI color, which has
    # to be stripped); zig cc has no -Wl,-Map, but LLD's --print-map writes
    # the map to stdout.
    log = re.sub(r"\x1b\[[0-9;]*m", "", out.stdout + out.stderr)
    link = None
    for line in log.splitlines():
        m = re.search(r"(\S*zig cc .*-o " + re.escape(binpath) + r")\b", line)
        if m:
            link = m.group(1)
    if link is None or not os.path.exists(binpath):
        sys.exit("could not build the sample or find its link command "
                 "(is ../local-tomo runnable?)")
    if link.startswith("zig cc"):
        # The log says just "zig cc"; resolve it to the bundled toolchain:
        candidates = sorted(
            p for p in
            [os.path.join(d, "zig", "zig") for d in
             glob.glob(os.path.join(HERE, "..", "build", "*", "tomo",
                                    "libexec", "tomo@*"))]
            if os.access(p, os.X_OK))
        if not candidates:
            sys.exit("no bundled zig found under ../build")
        link = candidates[-1] + link[len("zig"):]
    mapped = link.replace(f"-o {binpath}",
                          f"-Wl,--print-map -o {binpath}.mapped")
    r = subprocess.run(mapped, shell=True, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"re-link with --print-map failed:\n{r.stderr[:500]}")
    return binpath, r.stdout


def attribute(map_text, binpath):
    """Sum each input archive's contribution to the file, from the map."""
    row = re.compile(r"^\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+(\d+)"
                     r" (\s*)(\S.*)$")
    per = collections.Counter()
    cur_out = None
    for line in map_text.splitlines():
        m = row.match(line)
        if not m:
            continue
        size, indent, what = int(m.group(3), 16), len(m.group(5)), m.group(6)
        if indent == 0:  # output-section header
            cur_out = what.split()[0]
            continue
        if indent != 8:  # symbol row: would double-count its input section
            continue
        # .bss and friends occupy no file bytes, so they don't belong in a
        # chart of the *file*:
        if cur_out and cur_out.startswith((".bss", ".tbss", ".relro")):
            continue
        per[what.split(":(")[0].split("(")[0]] += size

    def bucket(path):
        b = os.path.basename(path)
        if b.startswith("<internal>"):
            # Mostly .symtab/.strtab (the symbol table `strip` would remove)
            # plus the linker's merged string/constant pools.
            return "symbol tables + linker data"
        if b == "libtomo.a": return "Tomo runtime"
        if b == "libgmp.a": return "GMP (bignum Int)"
        if b == "libunistring.a": return "libunistring (Unicode)"
        if b == "libgc.a": return "Boehm GC"
        if b == "libbacktrace.a": return "libbacktrace (stacktraces)"
        if b in ("libc.a", "libzigc.a", "crt1.o",
                 "libcompiler_rt.a", "libunwind.a"):
            return "musl libc + compiler-rt"
        # The program's own objects. Mostly not code: .tomo.source (a zip of
        # the vendored license texts plus the .tm source) and the program's
        # zstd-compressed DWARF, which is what lets a stacktrace name a .tm
        # line, dominate it.
        return "your program\n(code + embedded licenses)"

    grouped = collections.Counter()
    for path, size in per.items():
        grouped[bucket(path)] += size
    return grouped, os.path.getsize(binpath)


def draw(grouped, file_size):
    parts = sorted(grouped.items(), key=lambda t: -t[1])
    total = sum(v for _, v in parts)

    fig, ax = plt.subplots(figsize=(9.5, 6.4))
    fig.patch.set_facecolor(SURFACE)
    ax.set_facecolor(SURFACE)
    wedges, _ = ax.pie(
        [v for _, v in parts], startangle=90, counterclock=False,
        colors=HUES[:len(parts)],
        wedgeprops=dict(width=0.42, edgecolor=SURFACE, linewidth=2))

    # Every slice is direct-labeled, so identity never rides on color alone.
    # Thin slices land at nearly the same angle; spread each side's labels to
    # a minimum vertical gap instead of letting them collide.
    placed = []
    for w, (name, val) in zip(wedges, parts):
        ang = (w.theta2 + w.theta1) / 2
        x, y = math.cos(math.radians(ang)), math.sin(math.radians(ang))
        placed.append({"name": name, "val": val, "x": x, "y": y,
                       "right": x >= 0})
    GAP = 0.30
    for right in (True, False):
        side = sorted((p for p in placed if p["right"] == right),
                      key=lambda p: p["y"])
        for i in range(1, len(side)):  # push up from the bottom...
            side[i]["ly"] = max(side[i].get("ly", side[i]["y"]),
                                side[i - 1].get("ly", side[i - 1]["y"]) + GAP)
        for i in range(len(side) - 2, -1, -1):  # ...back down if we overran
            if side[i + 1].get("ly", side[i + 1]["y"]) > 1.35:
                side[i]["ly"] = min(side[i].get("ly", side[i]["y"]),
                                    side[i + 1]["ly"] - GAP)
    for p in placed:
        ax.annotate(
            f"{p['name']}\n{p['val']/1024:.0f} KB · {100*p['val']/total:.1f}%",
            xy=(0.98 * p["x"], 0.98 * p["y"]),
            xytext=(1.30 if p["right"] else -1.30, p.get("ly", p["y"])),
            ha="left" if p["right"] else "right", va="center",
            fontsize=9.5, color=INK, linespacing=1.35,
            arrowprops=dict(arrowstyle="-", color=MUTED, linewidth=1,
                            shrinkA=0, shrinkB=4))

    ax.text(0, 0.10, f"{total/1024:.0f} KB", ha="center", va="center",
            fontsize=30, fontweight="bold", color=INK)
    ax.text(0, -0.14, "static binary\nas built", ha="center", va="center",
            fontsize=10, color=MUTED, linespacing=1.4)
    ax.set(aspect="equal", xlim=(-2.05, 2.05), ylim=(-1.5, 1.5))
    ax.axis("off")
    fig.suptitle("Where a Tomo binary's bytes go", x=0.02, y=0.97, ha="left",
                 fontsize=15, fontweight="bold", color=INK)
    fig.text(0.02, 0.915,
             "a program doing bignum and exact-real arithmetic, built with "
             "-O3 · attributed from the linker map · musl throughout",
             ha="left", fontsize=9, color=MUTED)
    fig.tight_layout(rect=[0, 0, 1, 0.90])
    for ext, dpi in (("svg", None), ("png", 150)):
        out = os.path.join(HERE, f"size-breakdown.{ext}")
        fig.savefig(out, dpi=dpi, facecolor=SURFACE)
        print(f"wrote {out}")

    for name, val in parts:
        flat = name.replace("\n", " ")
        print(f"  {val/1024:7.1f} KB  {100*val/total:5.1f}%  {flat}")
    print(f"  {total/1024:7.1f} KB attributed of a {file_size/1024:.1f} KB file")


if __name__ == "__main__":
    binpath, map_text = build_with_map()
    grouped, file_size = attribute(map_text, binpath)
    draw(grouped, file_size)
