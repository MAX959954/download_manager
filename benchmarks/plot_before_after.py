#!/usr/bin/env python3
"""before_after.png — visually demonstrates what ChunkQueue fixes: the
"20 MB file, 10 MB chunk size" scenario (only 2 chunks initially). The
"Before" numbers are a real measurement of the old static algorithm (one
task per fixed chunk, no splitting; see benchmarks/old_static_bench.cpp,
not part of production code, used only for this comparison). The "After"
numbers come from benchmarks/bench_downloader.cpp (ChunkQueue) via
results.csv.
"""
import matplotlib.pyplot as plt

THREADS = [1, 4, 8]
BEFORE_MB_S = [1.00, 2.01, 2.01]   # old_static_bench.cpp, median of 3 runs
AFTER_MB_S = [1.00, 3.93, 7.16]    # benchmarks/results.csv (chunk=10MB), median of 3 runs

TEXT_PRIMARY = "#0b0b0b"
TEXT_SECONDARY = "#52514e"
SURFACE = "#fcfcfb"
COLOR_BEFORE = "#52514e"  # neutral gray — "before"
COLOR_AFTER = "#1baf7a"   # aqua — "after", accent

fig, ax = plt.subplots(figsize=(7, 5), dpi=150)
fig.patch.set_facecolor(SURFACE)
ax.set_facecolor(SURFACE)

x = range(len(THREADS))
width = 0.32

bars_before = ax.bar([xi - width / 2 for xi in x], BEFORE_MB_S, width=width,
                      color=COLOR_BEFORE, label="Before (static chunks)", zorder=3)
bars_after = ax.bar([xi + width / 2 for xi in x], AFTER_MB_S, width=width,
                     color=COLOR_AFTER, label="After (ChunkQueue, adaptive)", zorder=3)

for bars in (bars_before, bars_after):
    for rect in bars:
        ax.text(rect.get_x() + rect.get_width() / 2, rect.get_height() + 0.12,
                f"{rect.get_height():.2f}", ha="center", va="bottom", fontsize=9, color=TEXT_SECONDARY)

ax.set_xticks(list(x))
ax.set_xticklabels([f"{t} thread{'' if t == 1 else 's'}" for t in THREADS], color=TEXT_PRIMARY)
ax.set_ylabel("Throughput, MB/s", color=TEXT_PRIMARY)
ax.set_title(
    "Effect of ChunkQueue with a large initial chunk size\n"
    "(20 MB file, chunkSize 10 MB — starts with only 2 ranges)",
    color=TEXT_PRIMARY, fontsize=12,
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
fig.savefig("benchmarks/before_after.png", facecolor=SURFACE)
print("written benchmarks/before_after.png")
