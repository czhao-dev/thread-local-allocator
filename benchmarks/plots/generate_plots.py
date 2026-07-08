#!/usr/bin/env python3
"""Renders benchmarks/plots/results.json into the PNG figures embedded in the
README's Benchmarks section (light + dark variant of each, swapped via a
GitHub <picture> tag). results.json holds the median of 15 repeated runs of
`benchmarks/run_all.sh`, plus the observed min/max, since these are
short-duration (0.1-0.4s) benchmarks sensitive to OS scheduling noise.
Regenerate after a new benchmark run:

    cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
    cmake --build build-release -j
    for i in $(seq 1 15); do benchmarks/run_all.sh build-release; done > /tmp/raw.txt
    # parse /tmp/raw.txt into benchmarks/plots/results.json (median + min/max
    # per workload/allocator), then:
    python3 benchmarks/plots/generate_plots.py
"""

import json
from pathlib import Path

import matplotlib
import matplotlib.pyplot as plt
import matplotlib.ticker as mticker

matplotlib.use("Agg")

HERE = Path(__file__).resolve().parent
RESULTS = HERE / "results.json"

ALLOCATORS = [("system", "system allocator"), ("memalloc", "memalloc")]

THEMES = {
    "light": dict(
        surface="#fcfcfb",
        ink="#0b0b0b",
        ink_secondary="#52514e",
        muted="#898781",
        grid="#e1e0d9",
        baseline="#c3c2b7",
        colors=["#898781", "#2a78d6"],
    ),
    "dark": dict(
        surface="#1a1a19",
        ink="#ffffff",
        ink_secondary="#c3c2b7",
        muted="#898781",
        grid="#2c2c2a",
        baseline="#383835",
        colors=["#898781", "#3987e5"],
    ),
}

BAR_HEIGHT = 0.5


def load_results():
    return json.load(open(RESULTS))


def plot_throughput(data: dict, theme_name: str, out_path: Path) -> None:
    t = THEMES[theme_name]
    fig, axes = plt.subplots(1, 2, figsize=(10.5, 4.2), facecolor=t["surface"], constrained_layout=True)

    panels = [
        ("fixed_size_bench", "Workload 1 - fixed-size churn\n8 threads x 2M ops of 64B"),
        ("mixed_size_bench", "Workload 2 - mixed-size allocation\n10M ops, sizes in [8, 4096]B"),
    ]

    for ax, (key, title) in zip(axes, panels):
        ax.set_facecolor(t["surface"])
        y_pos = range(len(ALLOCATORS))
        values = [data[key][a]["throughput"] for a, _ in ALLOCATORS]
        err_low = [data[key][a]["throughput"] - data[key][a]["throughput_min"] for a, _ in ALLOCATORS]
        err_high = [data[key][a]["throughput_max"] - data[key][a]["throughput"] for a, _ in ALLOCATORS]
        colors = t["colors"]

        bars = ax.barh(
            list(y_pos)[::-1],
            values,
            height=BAR_HEIGHT,
            color=colors,
            zorder=3,
            xerr=[err_low, err_high],
            ecolor=t["muted"],
            capsize=4,
        )

        ax.set_yticks(list(y_pos)[::-1])
        ax.set_yticklabels([label for _, label in ALLOCATORS], color=t["ink_secondary"], fontsize=9.5)
        ax.set_title(title, color=t["ink"], fontsize=10, pad=10, loc="left")
        ax.set_xlabel("Mops/sec (median of 15 runs, bar = min-max)", color=t["muted"], fontsize=8)

        ax.xaxis.set_major_locator(mticker.MaxNLocator(nbins=4))
        ax.tick_params(axis="x", colors=t["muted"], labelsize=8.5)
        ax.tick_params(axis="y", length=0)
        for spine in ax.spines.values():
            spine.set_visible(False)
        ax.spines["bottom"].set_visible(True)
        ax.spines["bottom"].set_color(t["baseline"])
        ax.grid(axis="x", color=t["grid"], linewidth=1, zorder=0)
        ax.set_axisbelow(True)

        max_val = max(data[key][a]["throughput_max"] for a, _ in ALLOCATORS)
        ax.set_xlim(0, max_val * 1.18)
        for bar, value, eh in zip(bars, values, err_high):
            ax.text(
                value + eh + max_val * 0.025,
                bar.get_y() + bar.get_height() / 2,
                f"{value:.1f}",
                va="center",
                ha="left",
                fontsize=8.5,
                color=t["ink"],
            )

    fig.suptitle(
        "Allocation throughput, memalloc vs. system allocator (higher is better)",
        color=t["ink"],
        fontsize=11,
        x=0.01,
        ha="left",
    )
    fig.savefig(out_path, dpi=200, facecolor=t["surface"])
    plt.close(fig)


def plot_latency(data: dict, theme_name: str, out_path: Path) -> None:
    t = THEMES[theme_name]
    fig, ax = plt.subplots(figsize=(7.2, 4.4), facecolor=t["surface"], constrained_layout=True)
    ax.set_facecolor(t["surface"])

    percentiles = ["p50", "p99", "p999"]
    x = range(len(percentiles))
    width = 0.35

    for i, (alloc_key, label) in enumerate(ALLOCATORS):
        values = [max(data["latency_bench"][alloc_key][p], 0.5) for p in percentiles]
        offset = (i - 0.5) * width
        bars = ax.bar(
            [xi + offset for xi in x], values, width=width, color=t["colors"][i], label=label, zorder=3
        )
        for bar, p in zip(bars, percentiles):
            v = data["latency_bench"][alloc_key][p]
            ax.text(
                bar.get_x() + bar.get_width() / 2,
                bar.get_height() * 1.05,
                f"{v} ns",
                ha="center",
                va="bottom",
                fontsize=8.5,
                color=t["ink"],
            )

    ax.set_yscale("log")
    ax.set_xticks(list(x))
    ax.set_xticklabels(["p50", "p99", "p999"], color=t["ink_secondary"], fontsize=9.5)
    ax.tick_params(axis="x", length=0)
    ax.tick_params(axis="y", colors=t["muted"], labelsize=8.5)
    ax.set_ylabel("allocation latency, ns (log scale)", color=t["ink_secondary"], fontsize=9.5)
    ax.legend(frameon=False, labelcolor=t["ink_secondary"], fontsize=9, loc="upper left")

    for spine in ax.spines.values():
        spine.set_visible(False)
    ax.spines["left"].set_visible(True)
    ax.spines["left"].set_color(t["baseline"])
    ax.grid(axis="y", color=t["grid"], linewidth=1, zorder=0, which="major")
    ax.set_axisbelow(True)

    ax.set_title(
        "Single-threaded allocation latency, 64B objects\n(500,000 allocations, median of 15 runs)",
        color=t["ink"],
        fontsize=10.5,
        loc="left",
    )
    fig.savefig(out_path, dpi=200, facecolor=t["surface"])
    plt.close(fig)


def main():
    data = load_results()
    for theme_name in THEMES:
        plot_throughput(data, theme_name, HERE / f"throughput_by_workload_{theme_name}.png")
        plot_latency(data, theme_name, HERE / f"latency_percentiles_{theme_name}.png")
    print("Wrote plots to", HERE)


if __name__ == "__main__":
    main()
