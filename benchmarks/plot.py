#!/usr/bin/env python3
"""Render benchmark results (results.json) as comparison graphs.

One horizontal-bar panel per benchmark, languages sorted fastest-first.
Tomo is drawn in an accent color; every other language is neutral gray, so
the graph reads as "Tomo vs. the field" and is colorblind-safe by
construction (one accent vs. gray). Each bar is directly labeled with its
wall-clock time and slowdown relative to the fastest language.

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

HERE = os.path.dirname(os.path.abspath(__file__))

# "Tomo vs the field": one accent + one neutral. Ink tokens for all text.
ACCENT = "#e4572e"   # Tomo
NEUTRAL = "#b8bcc2"  # every other language
INVALID = "#d64545"  # output-mismatch marker
INK = "#202124"
MUTED = "#5f6368"
GRID = "#e6e8eb"


def load(path):
    with open(path) as f:
        return json.load(f)


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

    xmax = max(times) if times else 1.0
    ax.set_xlim(0, xmax * 1.18)
    for spine in ("top", "right", "left"):
        ax.spines[spine].set_visible(False)
    ax.spines["bottom"].set_color(GRID)
    ax.tick_params(axis="x", colors=MUTED, labelsize=9, length=0)
    ax.tick_params(axis="y", length=0)
    ax.xaxis.grid(True, color=GRID, linewidth=1, zorder=0)
    ax.set_axisbelow(True)

    for yi, (lang, _, s, valid) in zip(y, rows):
        rel = s / fastest if fastest else 1.0
        txt = f"{s:.2f}s" + (f"  ({rel:.1f}×)" if rel >= 1.005 else "  (fastest)")
        if not valid:
            txt += "  ✗ output"
        ax.text(s + xmax * 0.012, yi, txt, va="center", ha="left",
                fontsize=9, color=INK if lang == "tomo" else MUTED,
                fontweight="bold" if lang == "tomo" else "normal", zorder=4)

    args = " ".join(block.get("args", []))
    ax.set_title(f"{bname}   (n = {args})", loc="left", fontsize=13,
                 color=INK, fontweight="bold", pad=8)
    ax.set_xlabel("wall-clock seconds — lower is faster", fontsize=9, color=MUTED)


def main():
    argv = sys.argv[1:]
    out = "results"
    if "-o" in argv:
        i = argv.index("-o")
        out = argv[i + 1]
        del argv[i:i + 2]
    path = argv[0] if argv else os.path.join(HERE, "results.json")
    data = load(path)

    benches = list(data.items())
    n = len(benches)
    fig, axes = plt.subplots(n, 1, figsize=(9, 1.0 + 3.2 * n),
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
    fig.suptitle("Tomo vs. other languages — Computer Language Benchmarks Game",
                 x=0.02, ha="left", fontsize=14, fontweight="bold", color=INK)
    fig.text(0.02, 0.008,
             "best of 3 runs · same input · outputs validated against the C reference",
             ha="left", fontsize=8, color=MUTED)
    fig.tight_layout(rect=[0, 0.03, 1, 0.93])

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
                 "best of 3 runs · same input · outputs validated against the C reference",
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
