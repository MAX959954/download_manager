#!/usr/bin/env python3
"""Builds benchmark.png from benchmarks/results.csv (see bench_downloader.cpp
and throttled_server.py for how the raw data was produced).

Run: python3 benchmarks/plot_results.py
"""
import csv
import statistics
from collections import defaultdict
from pathlib import Path

import matplotlib.pyplot as plt

HERE = Path(__file__).parent
CSV_PATH = HERE / "results.csv"
OUT_PATH = HERE / "benchmark.png"

# Categorical palette (first 3 slots of the validated palette — verified
# for CVD/contrast across all pairs, see skill dataviz/references/palette.md).
COLORS = {
    1: "#2a78d6",  # blue
    4: "#eb6834",  # orange
    8: "#1baf7a",  # aqua
}

TEXT_PRIMARY = "#0b0b0b"
TEXT_SECONDARY = "#52514e"
SURFACE = "#fcfcfb"


def main():
    rows = list(csv.DictReader(CSV_PATH.open()))

    # median throughput by (chunk_size, threads)
    grouped = defaultdict(list)
    for row in rows:
        key = (int(row["chunk_size_bytes"]), int(row["threads"]))
        grouped[key].append(float(row["throughput_mb_s"]))

    chunk_sizes = sorted({k[0] for k in grouped})
    thread_counts = sorted({k[1] for k in grouped})

    def chunk_label(n):
        return f"{n // 1024} KB" if n < 1024 * 1024 else f"{n // (1024 * 1024)} MB"

    fig, ax = plt.subplots(figsize=(8, 5), dpi=150)
    fig.patch.set_facecolor(SURFACE)
    ax.set_facecolor(SURFACE)

    n_groups = len(chunk_sizes)
    n_bars = len(thread_counts)
    bar_width = 0.8 / n_bars
    x = range(n_groups)

    for i, threads in enumerate(thread_counts):
        medians = [statistics.median(grouped[(cs, threads)]) for cs in chunk_sizes]
        offsets = [xi + (i - (n_bars - 1) / 2) * bar_width for xi in x]
        bars = ax.bar(
            offsets,
            medians,
            width=bar_width * 0.9,
            color=COLORS.get(threads, "#888888"),
            label=f"{threads} thread{'' if threads == 1 else 's'}",
            zorder=3,
        )
        for rect, value in zip(bars, medians):
            ax.text(
                rect.get_x() + rect.get_width() / 2,
                rect.get_height() + 0.15,
                f"{value:.1f}",
                ha="center",
                va="bottom",
                fontsize=9,
                color=TEXT_SECONDARY,
            )

    ax.set_xticks(list(x))
    ax.set_xticklabels([chunk_label(cs) for cs in chunk_sizes], color=TEXT_PRIMARY)
    ax.set_xlabel("Chunk size", color=TEXT_PRIMARY)
    ax.set_ylabel("Throughput, MB/s (median of 3 runs)", color=TEXT_PRIMARY)
    ax.set_title(
        "Parallel downloads vs thread count (after adaptive chunking)\n"
        "(20 MB file, server caps each connection at 1 MB/s)",
        color=TEXT_PRIMARY,
        fontsize=12,
    )
    ax.tick_params(colors=TEXT_SECONDARY)
    ax.grid(axis="y", color="#dddbd3", linewidth=0.8, zorder=0)
    for spine in ("top", "right"):
        ax.spines[spine].set_visible(False)
    for spine in ("left", "bottom"):
        ax.spines[spine].set_color("#c3c2b7")

    legend = ax.legend(frameon=False, loc="upper left")
    for text in legend.get_texts():
        text.set_color(TEXT_PRIMARY)

    fig.tight_layout()
    fig.savefig(OUT_PATH, facecolor=SURFACE)
    print(f"written {OUT_PATH}")


if __name__ == "__main__":
    main()
