#!/usr/bin/env python3
"""Render benchmark results (results.json) as comparison graphs.

One horizontal-bar panel per benchmark, languages sorted fastest-first.
Tomo is drawn in an accent color; every other language is neutral gray, so
the graph reads as "Tomo vs. the field" and is colorblind-safe by
construction (one accent vs. gray). Each bar is directly labeled with its
wall-clock time and slowdown relative to the fastest language.

Where a few entries are far slower than the rest of the field (Python is 300x
the leader on spectral-norm), the panel's x-axis is broken: the pack keeps a
linear scale and the runaways are compressed into a band at the right, with a
zigzag on the axis and on each crossing bar. See break_at() for where.

Writes a combined overview (<out>.svg/.png, all benchmarks stacked) plus one
standalone graph per benchmark (<out>-<benchmark>.svg/.png).

Usage: python3 plot.py [results.json] [-o out_basename]
"""
import json
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np
from matplotlib.patches import Patch

HERE = os.path.dirname(os.path.abspath(__file__))

# "Tomo vs the field": one accent + one neutral. Ink tokens for all text.
ACCENT = "#e4572e"   # Tomo
NEUTRAL = "#b8bcc2"  # every other language
INVALID = "#d64545"  # output-mismatch marker
INK = "#202124"
MUTED = "#5f6368"
GRID = "#e6e8eb"


# A single very slow entry squashes everyone else: on spectral-norm Python
# takes 182s against a 0.6s leader, so every other bar is a sliver. When that
# happens the axis is broken: everything up to the break keeps a normal linear
# scale, and the runaway entries past it are drawn on a compressed scale in a
# band at the right, marked with a zigzag where the scale changes. Where to
# break is derived from the data rather than fixed at some number of seconds:
# sort the times and break at the *lowest* neighbor-to-neighbor jump that is
# big enough to matter and leaves only a few entries above it -- the lowest
# such jump, not the biggest, because it is the one that wins back the most
# axis for the pack (mandelbrot's biggest jump is Lua->Python at the very top,
# but breaking below PyPy is what stops the field from being a sliver).
BREAK_JUMP = 2.5        # a jump this large can host the break...
BREAK_MAX_SHARE = 0.25  # ...if at most this fraction of the field is above it
BREAK_MIN_SQUASH = 0.4  # ...and the rest is squashed into this fraction of the
                        #    axis, i.e. there is real room to be won back
# How much of the axis the compressed band gets. It has to be wide enough to
# read as a region of its own -- the point of breaking is to stop the far end
# from eating the axis, not to shave the outliers down to a stub.
BREAK_TAIL_SHARE = 0.22
# The share of that band spent on the (empty) gap right after the break, so a
# compressed bar starts clear of the break rather than ending on top of it.
BREAK_TAIL_LEAD = 0.25


def load(path):
    with open(path) as f:
        return json.load(f)


def break_at(times):
    """Break the axis at this value, or None to draw every bar to scale.

    `times` must be sorted ascending.
    """
    slowest = times[-1] if times else 0.0
    if len(times) < 4 or slowest <= 0:
        return None
    for i in range(max(1, len(times) - int(len(times) * BREAK_MAX_SHARE)), len(times)):
        jump = times[i] / times[i - 1] if times[i - 1] > 0 else 0.0
        if jump < BREAK_JUMP:
            continue
        # ...but only if the field really is being squashed; if it already
        # fills a decent share of the axis there is nothing to win back.
        if times[i - 1] / slowest <= BREAK_MIN_SQUASH:
            return times[i - 1]
    return None


def apply_break(ax, brk, times):
    """Give `ax` a broken x-axis at `brk`; return its (forward, inverse) pair.

    Everything up to `brk` keeps its linear scale; past it, the axis is
    compressed so the slowest entry lands at the far edge of a band
    `BREAK_TAIL_SHARE` wide. This is matplotlib's built-in "function" scale, so
    bars, ticks, gridlines and text all stay in real data coordinates -- only
    the mapping to the page changes. With no break, the pair is the identity.
    """
    slowest = times[-1] if times else 0.0
    if brk is None or slowest <= brk:
        return (lambda x: x), (lambda x: x)

    band = brk * BREAK_TAIL_SHARE
    # Knots of the piecewise-linear map: identity up to the break, then the
    # empty gap just past it collapses into a short lead-in (so the first
    # compressed bar clears the break instead of ending on it), and the
    # entries themselves spread over the rest of the band. The last segment is
    # extended past the slowest entry so offsets and limits computed beyond it
    # still map back sensibly.
    first = min(x for x in times if x > brk)
    xs = [0.0, brk, first, slowest, slowest + (slowest - first) + 1e-9]
    ys = [0.0, brk, brk + band * BREAK_TAIL_LEAD, brk + band,
          brk + band + band * (1 - BREAK_TAIL_LEAD)]

    def forward(x):
        return np.interp(np.asarray(x, dtype=float), xs, ys)

    def inverse(x):
        return np.interp(np.asarray(x, dtype=float), ys, xs)

    ax.set_xscale("function", functions=(forward, inverse))
    # The default locator would spread ticks across the whole (mostly
    # compressed) data range; ticks only mean anything below the break.
    ax.set_xticks([t for t in ticker.MaxNLocator(nbins=6, steps=[1, 2, 2.5, 5, 10])
                   .tick_values(0, brk) if 0 <= t <= brk])
    return forward, inverse


def break_marks(ax, x, fwd, inv, dx):
    """The x positions of a two-slash break mark centered on `x`.

    The offsets are measured on the page (i.e. after `fwd`), so the mark stays
    an even zigzag even though the two sides of the break are at different
    scales.
    """
    return [(float(inv(fwd(x) + a)), float(inv(fwd(x) + b)))
            for a, b in ((-2 * dx, 0), (0, 2 * dx))]


def draw_bar_break(ax, x, yi, height, fwd, inv, dx):
    """Zigzag a bar where it crosses the break, so the scale change is visible."""
    h = height / 2
    (a0, a1), (b0, b1) = break_marks(ax, x, fwd, inv, dx)
    # A white wedge cuts the bar in two, with a slash fencing off each side.
    ax.fill([a0, a1, b1, b0], [yi + h, yi - h, yi - h, yi + h],
            color="white", zorder=4, linewidth=0)
    for x0, x1 in ((a0, a1), (b0, b1)):
        ax.plot([x0, x1], [yi + h, yi - h], color=MUTED, linewidth=0.9,
                zorder=5, solid_capstyle="butt")


def draw_axis_break(ax, x, fwd, inv, dx):
    """The same zigzag on the x-axis itself, where the scale changes."""
    for x0, x1 in break_marks(ax, x, fwd, inv, dx):
        ax.plot([x0, x1], [-0.012, 0.012], transform=ax.get_xaxis_transform(),
                color=MUTED, linewidth=0.9, clip_on=False, zorder=5)


def panel(ax, bname, block):
    rows = []
    for lang, r in block["results"].items():
        rows.append((lang, r["label"], r["seconds"], r.get("valid", True)))
    rows.sort(key=lambda t: t[2])  # fastest first
    fastest = min((s for *_, s, _ in [(*r,) for r in rows]), default=1.0)
    fastest = rows[0][2] if rows else 1.0

    labels = [lbl for _, lbl, _, _ in rows]
    times = [s for *_, s, _ in rows]
    y = range(len(rows))

    colors = []
    for lang, _, _, valid in rows:
        if not valid:
            colors.append(INVALID)
        elif lang == "tomo":
            colors.append(ACCENT)
        else:
            colors.append(NEUTRAL)

    # One runaway entry would squash the rest of the field into slivers, so
    # past `brk` the axis switches to a compressed scale (see break_at).
    brk = break_at(times)
    slowest = times[-1] if times else 1.0
    fwd, inv = apply_break(ax, brk, times)

    # Draw top (fastest) at the top of the panel.
    ax.barh(y, times, color=colors, height=0.68, zorder=3)
    ax.invert_yaxis()
    ax.set_yticks(list(y))
    ax.set_yticklabels(labels, fontsize=10,
                       color=INK)
    # Emphasize the Tomo tick label.
    for tick, (lang, *_ ) in zip(ax.get_yticklabels(), rows):
        if lang == "tomo":
            tick.set_color(ACCENT)
            tick.set_fontweight("bold")

    # Everything below is laid out on the page, i.e. in `fwd` space, and
    # converted back to data values -- with a break, a data-space offset would
    # mean two different distances on the two sides of it.
    page_max = float(fwd(slowest))
    ax.set_xlim(0, float(inv(page_max * 1.18)))
    for spine in ("top", "right", "left"):
        ax.spines[spine].set_visible(False)
    ax.spines["bottom"].set_color(GRID)
    ax.tick_params(axis="x", colors=MUTED, labelsize=9, length=0)
    ax.tick_params(axis="y", length=0)
    ax.xaxis.grid(True, color=GRID, linewidth=1, zorder=0)
    ax.set_axisbelow(True)
    if brk is not None:
        draw_axis_break(ax, brk, fwd, inv, page_max * 0.006)

    for yi, (lang, _, s, valid) in zip(y, rows):
        rel = s / fastest if fastest else 1.0
        txt = f"{s:.2f}s" + (f"  ({rel:.1f}×)" if rel >= 1.005 else "  (fastest)")
        if not valid:
            txt += "  ✗ output"
        if brk is not None and s > brk:
            draw_bar_break(ax, brk, yi, 0.68, fwd, inv, page_max * 0.006)
        ax.text(float(inv(fwd(s) + page_max * 0.012)), yi, txt, va="center",
                ha="left", fontsize=9, color=INK if lang == "tomo" else MUTED,
                fontweight="bold" if lang == "tomo" else "normal", zorder=4)

    args = " ".join(block.get("args", []))
    ax.set_title(f"{bname}   (n = {args})", loc="left", fontsize=13,
                 color=INK, fontweight="bold", pad=8)
    ax.set_xlabel("wall-clock seconds — lower is faster", fontsize=9, color=MUTED)


def human_bytes(n):
    """Compact size label: KB below 1 MB, else MB (binary units)."""
    if n < 1024 * 1024:
        return f"{n / 1024:.0f} KB"
    return f"{n / (1024 * 1024):.2f} MB"


def size_panel(ax, bname, block):
    rows = []
    for lang, r in block["results"].items():
        rows.append((lang, r["label"], r["bytes"]))
    rows.sort(key=lambda t: t[2])  # smallest first
    smallest = rows[0][2] if rows else 1

    labels = [lbl for _, lbl, _ in rows]
    vals = [b for *_, b in rows]
    y = range(len(rows))

    colors = [ACCENT if lang == "tomo" else NEUTRAL for lang, _, _ in rows]
    ax.barh(y, vals, color=colors, height=0.68, zorder=3)
    ax.invert_yaxis()
    ax.set_yticks(list(y))
    ax.set_yticklabels(labels, fontsize=10, color=INK)
    for tick, (lang, *_) in zip(ax.get_yticklabels(), rows):
        if lang == "tomo":
            tick.set_color(ACCENT)
            tick.set_fontweight("bold")

    xmax = max(vals) if vals else 1
    ax.set_xlim(0, xmax * 1.20)
    for spine in ("top", "right", "left"):
        ax.spines[spine].set_visible(False)
    ax.spines["bottom"].set_color(GRID)
    ax.tick_params(axis="x", colors=MUTED, labelsize=9, length=0)
    ax.tick_params(axis="y", length=0)
    ax.xaxis.set_major_formatter(
        plt.FuncFormatter(lambda v, _p: f"{v / (1024 * 1024):.1f}"))
    ax.xaxis.grid(True, color=GRID, linewidth=1, zorder=0)
    ax.set_axisbelow(True)

    for yi, (lang, _, b) in zip(y, rows):
        rel = b / smallest if smallest else 1.0
        txt = human_bytes(b) + (f"  ({rel:.1f}×)" if rel >= 1.05 else "  (smallest)")
        ax.text(b + xmax * 0.012, yi, txt, va="center", ha="left",
                fontsize=9, color=INK if lang == "tomo" else MUTED,
                fontweight="bold" if lang == "tomo" else "normal", zorder=4)

    ax.set_title(f"{bname}", loc="left", fontsize=13, color=INK,
                 fontweight="bold", pad=8)
    ax.set_xlabel("static binary size (MB, stripped) — smaller is leaner",
                  fontsize=9, color=MUTED)


def main_sizes(path, out):
    data = load(path)
    benches = [(b, blk) for b, blk in data.items() if blk.get("results")]
    n = len(benches)
    fig, axes = plt.subplots(n, 1, figsize=(9, 1.0 + 3.2 * n), squeeze=False)
    fig.patch.set_facecolor("white")
    for ax, (bname, block) in zip(axes[:, 0], benches):
        ax.set_facecolor("white")
        size_panel(ax, bname, block)
    legend = [
        Patch(facecolor=ACCENT, label="Tomo"),
        Patch(facecolor=NEUTRAL, label="other languages"),
    ]
    axes[0, 0].legend(handles=legend, loc="upper right", frameon=False,
                      fontsize=9, borderaxespad=0.6)
    fig.suptitle("Static binary size — Tomo vs. other compiled languages",
                 x=0.02, ha="left", fontsize=14, fontweight="bold", color=INK)
    fig.text(0.02, 0.008,
             "statically linked · symbols stripped · only languages that can "
             "produce a standalone static binary",
             ha="left", fontsize=8, color=MUTED)
    fig.tight_layout(rect=[0, 0.03, 1, 0.93])
    svg = os.path.join(HERE, out + ".svg")
    png = os.path.join(HERE, out + ".png")
    fig.savefig(svg)
    fig.savefig(png, dpi=140)
    print(f"wrote {svg}\nwrote {png}")
    plt.close(fig)


def main():
    argv = sys.argv[1:]
    out = "results"
    if "--sizes" in argv:
        argv.remove("--sizes")
        out = "sizes"
        if "-o" in argv:
            i = argv.index("-o")
            out = argv[i + 1]
            del argv[i:i + 2]
        path = argv[0] if argv else os.path.join(HERE, "sizes.json")
        main_sizes(path, out)
        return
    if "-o" in argv:
        i = argv.index("-o")
        out = argv[i + 1]
        del argv[i:i + 2]
    path = argv[0] if argv else os.path.join(HERE, "results.json")
    data = load(path)

    benches = list(data.items())
    n = len(benches)
    fig_h = 1.0 + 3.2 * n
    fig, axes = plt.subplots(n, 1, figsize=(9, fig_h),
                             squeeze=False)
    fig.patch.set_facecolor("white")
    for ax, (bname, block) in zip(axes[:, 0], benches):
        ax.set_facecolor("white")
        panel(ax, bname, block)

    # Legend sits in the empty upper-right of the first panel (the fastest
    # language's row leaves that corner clear), so it never fights the title.
    legend = [
        Patch(facecolor=ACCENT, label="Tomo"),
        Patch(facecolor=NEUTRAL, label="other languages"),
    ]
    axes[0, 0].legend(handles=legend, loc="upper right", frameon=False,
                      fontsize=9, borderaxespad=0.6)
    # Reserve title/footer bands as *fixed* heights (in inches, converted to
    # figure fractions) rather than fixed fractions -- otherwise a taller
    # figure (more benchmarks) opens a huge gap under the title.
    top = 1.0 - 0.55 / fig_h
    bottom = 0.35 / fig_h
    fig.suptitle("Tomo vs. other languages — Computer Language Benchmarks Game",
                 x=0.02, y=1.0 - 0.28 / fig_h, ha="left", va="top",
                 fontsize=14, fontweight="bold", color=INK)
    fig.text(0.02, 0.10 / fig_h,
             "best of 3 runs · same input · pinned to one core · outputs validated against the C reference",
             ha="left", fontsize=8, color=MUTED)
    fig.tight_layout(rect=[0, bottom, 1, top])

    svg = os.path.join(HERE, out + ".svg")
    png = os.path.join(HERE, out + ".png")
    fig.savefig(svg)
    fig.savefig(png, dpi=140)
    print(f"wrote {svg}\nwrote {png}")
    plt.close(fig)

    # One standalone graph per benchmark, so each can be linked/shared on
    # its own instead of only as a panel in the combined overview.
    for bname, block in benches:
        bfig, bax = plt.subplots(1, 1, figsize=(9, 3.4))
        bfig.patch.set_facecolor("white")
        bax.set_facecolor("white")
        panel(bax, bname, block)
        legend = [
            Patch(facecolor=ACCENT, label="Tomo"),
            Patch(facecolor=NEUTRAL, label="other languages"),
        ]
        bax.legend(handles=legend, loc="upper right", frameon=False,
                  fontsize=9, borderaxespad=0.6)
        bfig.text(0.02, 0.02,
                 "best of 3 runs · same input · pinned to one core · outputs validated against the C reference",
                 ha="left", fontsize=8, color=MUTED)
        bfig.tight_layout(rect=[0, 0.08, 1, 1])

        bsvg = os.path.join(HERE, f"{out}-{bname}.svg")
        bpng = os.path.join(HERE, f"{out}-{bname}.png")
        bfig.savefig(bsvg)
        bfig.savefig(bpng, dpi=140)
        print(f"wrote {bsvg}\nwrote {bpng}")
        plt.close(bfig)


if __name__ == "__main__":
    main()
