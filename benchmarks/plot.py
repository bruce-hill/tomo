#!/usr/bin/env python3
"""Render benchmark results (results.json) as comparison graphs.

One horizontal-bar panel per benchmark, languages sorted fastest-first.
Tomo is drawn in an accent color; every other language is neutral gray, so
the graph reads as "Tomo vs. the field" and is colorblind-safe by
construction (one accent vs. gray). Each bar is directly labeled with its
wall-clock time and slowdown relative to the fastest language.

Where a few entries are far slower than the rest of the field (Python is 300x
the leader on spectral-norm), the panel's x-axis is truncated to the rest of
the field: an ordinary axis, just a shorter one. The entries that don't fit run
off the end of it, marked with an arrowhead and labeled with their real time.
See truncate_at() for where.

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
from matplotlib.patches import Patch

# Hatching marks the strippable part of a size bar (see size_panel). The 1.0
# default is faint enough to read as texture rather than as a distinct region,
# which is the whole point of drawing it.
plt.rcParams["hatch.linewidth"] = 2.0

HERE = os.path.dirname(os.path.abspath(__file__))

# "Tomo vs the field": one accent + one neutral. Ink tokens for all text.
ACCENT = "#e4572e"   # Tomo
NEUTRAL = "#b8bcc2"  # every other language
INVALID = "#d64545"  # output-mismatch marker
INK = "#202124"
MUTED = "#5f6368"
GRID = "#e6e8eb"


# A few very slow entries squash everyone else: on spectral-norm Python takes
# 182s against a 0.61s leader, so every other bar is a sliver. The panel then
# truncates its x-axis to the rest of the field, an ordinary axis with an
# ordinary linear scale and ordinary ticks, just a shorter one, and the entries
# that do not fit run off the end of it, marked with an arrowhead and labeled
# with their real time inside the bar. Where to truncate is derived from the
# data rather than fixed at some number of seconds: sort the times and truncate
# above the *lowest* neighbor-to-neighbor jump that is big enough to matter and
# leaves only a few entries above it, taking the lowest such jump rather than
# the biggest, because it is the one that wins back the most axis for the pack
# (mandelbrot's biggest jump is Lua->Python at the very top, where truncating
# there would leave the field just as squashed).
BREAK_JUMP = 2.5        # a jump this large can host the truncation...
BREAK_MAX_SHARE = 0.25  # ...if at most this fraction of the field is above it
BREAK_MIN_SQUASH = 0.4  # ...and the rest is squashed into this fraction of the
                        #    axis, i.e. there is real room to be won back

# Only a handful of languages produce a standalone static binary at all, and it
# thins out further per benchmark. Below this many entries a panel is a list,
# not a comparison, so the size chart leaves it out (and says which).
MIN_SIZE_LANGS = 5


def load(path):
    with open(path) as f:
        return json.load(f)


def truncate_at(times):
    """The slowest time the panel still shows, or None to show them all.

    `times` must be sorted ascending. Everything above the returned value runs
    off the end of the axis.
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


def draw_offchart(ax, xlim, yi, height, color):
    """Arrowhead at the axis edge: this bar keeps going past the chart."""
    h = height / 2
    tip = xlim * 1.022
    ax.fill([xlim, tip, xlim], [yi + h, yi, yi - h], color=color, zorder=4,
            linewidth=0, clip_on=False)


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

    # A few runaway entries would squash the rest of the field into slivers, so
    # the axis stops at the rest of the field (see truncate_at) and they run off
    # the end of it. The axis is an ordinary one either way, with the same
    # linear scale and the same ticks, so bar lengths mean the same thing
    # throughout.
    shown = truncate_at(times)
    xmax = shown if shown is not None else (max(times) if times else 1.0)
    # The usual headroom past the longest bar is where its label goes; an
    # off-chart bar fills it and carries its label inside itself instead.
    xlim = xmax * 1.18
    widths = [min(s, xlim) for s in times]

    # Draw top (fastest) at the top of the panel.
    ax.barh(y, widths, color=colors, height=0.68, zorder=3)
    ax.invert_yaxis()
    ax.set_yticks(list(y))
    ax.set_yticklabels(labels, fontsize=10,
                       color=INK)
    # Emphasize the Tomo tick label.
    for tick, (lang, *_ ) in zip(ax.get_yticklabels(), rows):
        if lang == "tomo":
            tick.set_color(ACCENT)
            tick.set_fontweight("bold")

    ax.set_xlim(0, xlim)
    for spine in ("top", "right", "left"):
        ax.spines[spine].set_visible(False)
    ax.spines["bottom"].set_color(GRID)
    ax.tick_params(axis="x", colors=MUTED, labelsize=9, length=0)
    ax.tick_params(axis="y", length=0)
    ax.xaxis.grid(True, color=GRID, linewidth=1, zorder=0)
    ax.set_axisbelow(True)
    for yi, (lang, _, s, valid), w, color in zip(y, rows, widths, colors):
        rel = s / fastest if fastest else 1.0
        txt = f"{s:.2f}s" + (f"  ({rel:.1f}×)" if rel >= 1.005 else "  (fastest)")
        if not valid:
            txt += "  ✗ output"
        if w >= xlim:  # runs off the chart
            draw_offchart(ax, xlim, yi, 0.68, color)
            # The arrowhead already says "keeps going"; the label just needs
            # to be inside the bar, since there is no room past it.
            ax.text(w - xmax * 0.016, yi, txt, va="center", ha="right",
                    fontsize=9, color="white" if lang == "tomo" else INK,
                    fontweight="bold" if lang == "tomo" else "normal", zorder=5)
        else:
            ax.text(w + xmax * 0.012, yi, txt, va="center", ha="left",
                    fontsize=9, color=INK if lang == "tomo" else MUTED,
                    fontweight="bold" if lang == "tomo" else "normal", zorder=4)

    args = " ".join(block.get("args", []))
    ax.set_title(f"{bname}   (n = {args})", loc="left", fontsize=13,
                 color=INK, fontweight="bold", pad=8)
    ax.set_xlabel("wall-clock seconds (lower is faster)", fontsize=9, color=MUTED)


def human_bytes(n):
    """Compact size label: KB below 1 MB, else MB (binary units)."""
    if n < 1024 * 1024:
        return f"{n / 1024:.0f} KB"
    return f"{n / (1024 * 1024):.2f} MB"


def size_panel(ax, bname, block):
    rows = []
    for lang, r in block["results"].items():
        # Older sizes.json files carry no stripped measurement; treat the whole
        # binary as content rather than inventing a band.
        rows.append((lang, r["label"], r["bytes"],
                     r.get("stripped_bytes", r["bytes"])))
    rows.sort(key=lambda t: t[2])  # smallest first
    smallest = rows[0][2] if rows else 1

    labels = [lbl for _, lbl, _, _ in rows]
    vals = [b for _, _, b, _ in rows]
    kept = [k for *_, k in rows]
    y = range(len(rows))

    colors = [ACCENT if lang == "tomo" else NEUTRAL for lang, _, _, _ in rows]
    # Two segments per bar: what survives `strip` drawn solid, and what it
    # removes (debug info, symbol tables) hatched onto the end. The bar's full
    # length is still the size as built, which is what actually ships.
    ax.barh(y, kept, color=colors, height=0.68, zorder=3)
    # linewidth=0 on purpose: a stroked edge is centred on the bar's boundary,
    # so it would make this segment render a pixel or two taller than the solid
    # one beside it. The hatch lines still take their color from edgecolor.
    ax.barh(y, [v - k for v, k in zip(vals, kept)], left=kept, height=0.68,
            color="white", edgecolor=colors, hatch="/////", linewidth=0,
            zorder=3)
    ax.invert_yaxis()
    ax.set_yticks(list(y))
    ax.set_yticklabels(labels, fontsize=10, color=INK)
    for tick, (lang, *_) in zip(ax.get_yticklabels(), rows):
        if lang == "tomo":
            tick.set_color(ACCENT)
            tick.set_fontweight("bold")

    xmax = max(vals) if vals else 1
    ax.set_xlim(0, xmax * 1.34) # room for the longest "N KB (M×) · K KB code"
    for spine in ("top", "right", "left"):
        ax.spines[spine].set_visible(False)
    ax.spines["bottom"].set_color(GRID)
    ax.tick_params(axis="x", colors=MUTED, labelsize=9, length=0)
    ax.tick_params(axis="y", length=0)
    ax.xaxis.set_major_formatter(
        plt.FuncFormatter(lambda v, _p: f"{v / (1024 * 1024):.1f}"))
    ax.xaxis.grid(True, color=GRID, linewidth=1, zorder=0)
    ax.set_axisbelow(True)

    for yi, (lang, _, b, k) in zip(y, rows):
        # rows is sorted ascending, so only the first row is actually the
        # smallest; everything else carries its ratio. A second decimal when
        # the ratio would round to 1.0 keeps a genuinely larger binary from
        # reading as a tie.
        rel = b / smallest if smallest else 1.0
        ratio = f"{rel:.2f}×" if rel < 1.05 else f"{rel:.1f}×"
        txt = human_bytes(b) + ("  (smallest)" if yi == 0 else f"  ({ratio})")
        if b - k > 0.02 * b:
            txt += f"  ·  {human_bytes(k)} code"
        ax.text(b + xmax * 0.012, yi, txt, va="center", ha="left",
                fontsize=9, color=INK if lang == "tomo" else MUTED,
                fontweight="bold" if lang == "tomo" else "normal", zorder=4)

    ax.set_title(f"{bname}", loc="left", fontsize=13, color=INK,
                 fontweight="bold", pad=8)
    ax.set_xlabel("static binary size (MB, as built; smaller is leaner)",
                  fontsize=9, color=MUTED)


def main_sizes(path, out):
    data = load(path)
    benches, thin = [], []
    for b, blk in data.items():
        if not blk.get("results"):
            continue
        (benches if len(blk["results"]) >= MIN_SIZE_LANGS else thin).append((b, blk))
    if thin:
        dropped = ", ".join(f"{b} ({len(blk['results'])})" for b, blk in thin)
        print(f"skipping (fewer than {MIN_SIZE_LANGS} languages): {dropped}")
    if not benches:
        print("no benchmark has enough languages to chart")
        return
    n = len(benches)
    fig_h = 1.0 + 3.2 * n
    fig, axes = plt.subplots(n, 1, figsize=(9, fig_h), squeeze=False)
    fig.patch.set_facecolor("white")
    for ax, (bname, block) in zip(axes[:, 0], benches):
        ax.set_facecolor("white")
        size_panel(ax, bname, block)
    legend = [
        Patch(facecolor=ACCENT, label="Tomo"),
        Patch(facecolor=NEUTRAL, label="other languages"),
        Patch(facecolor="white", edgecolor=MUTED, hatch="////",
              label="removed by `strip`"),
    ]
    axes[0, 0].legend(handles=legend, loc="upper right", frameon=False,
                      fontsize=9, borderaxespad=0.6)
    # Title/footer bands are fixed heights (inches converted to figure
    # fractions), the same as main(): a fixed *fraction* opens a bigger and
    # bigger gap under the title as the figure grows with the panel count.
    top = 1.0 - 0.55 / fig_h
    bottom = 0.35 / fig_h
    fig.suptitle("Static binary size: Tomo vs. other compiled languages",
                 x=0.02, y=1.0 - 0.28 / fig_h, ha="left", va="top",
                 fontsize=14, fontweight="bold", color=INK)
    fig.text(0.02, 0.10 / fig_h,
             "statically linked · bars are the size as built; the hatched part "
             "is what `strip` removes · only languages that can produce a "
             "standalone static binary",
             ha="left", fontsize=8, color=MUTED)
    fig.tight_layout(rect=[0, bottom, 1, top])
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
    # figure fractions) rather than fixed fractions, since otherwise a taller
    # figure (more benchmarks) opens a huge gap under the title.
    top = 1.0 - 0.55 / fig_h
    bottom = 0.35 / fig_h
    fig.suptitle("Tomo vs. other languages: Computer Language Benchmarks Game",
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
