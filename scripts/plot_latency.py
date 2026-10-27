#!/usr/bin/env python3
"""Plot the per-message latency distribution produced by the replay harness.

Reads the CSV written by `chronos_replay` (columns: sequence, latency_ns),
drops the first 10% of messages as warmup, and renders a two-panel figure:
  * left:  histogram (log count) with p50 / p90 / p95 / p99 / p99.9 markers
  * right: complementary CDF on log-log axes, the standard view for tail latency

The warmup cut removes the period where the instruction cache, branch
predictors, and data TLB are still cold and the price-level array is still
growing, so the reported percentiles describe the steady-state hot path.

Usage:
    ./build/src/chronos_replay latency.csv
    python3 scripts/plot_latency.py latency.csv -o images/m4_latency_histogram.png
    python3 scripts/plot_latency.py latency.csv --warmup-fraction 0   # keep every sample
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")  # headless: render straight to file, no display server needed

import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402

PERCENTILES = (50.0, 90.0, 95.0, 99.0, 99.9)
MARKER_COLORS = {
    50.0: "#2a9d8f",
    90.0: "#8ab17d",
    95.0: "#e9c46a",
    99.0: "#f4a261",
    99.9: "#e76f51",
}
MAX_BINS = 400
MIN_CLUSTER_SHARE = 0.005  # clusters below 0.5% of samples are treated as off-tick noise
DEFAULT_WARMUP_FRACTION = 0.10


def load_latencies(csv_path: Path) -> np.ndarray:
    """Latency samples in arrival order (sorted by `sequence` when present)."""
    frame = pd.read_csv(csv_path)
    if "latency_ns" not in frame.columns:
        raise ValueError(f"{csv_path} has no 'latency_ns' column (found: {list(frame.columns)})")
    if "sequence" in frame.columns:
        frame = frame.sort_values("sequence", kind="stable")
    samples = frame["latency_ns"].to_numpy(dtype=np.float64)
    if samples.size == 0:
        raise ValueError(f"{csv_path} contains no samples")
    return samples


def strip_warmup(samples: np.ndarray, fraction: float) -> tuple[np.ndarray, int]:
    """Drops the first `fraction` of samples in arrival order.

    Arrival order matters: warmup is a property of *when* a message ran, not
    of how slow it was, so this must cut by position, never by value.
    """
    dropped = int(samples.size * fraction)
    if dropped >= samples.size:
        raise ValueError(f"warmup fraction {fraction} leaves no samples")
    return samples[dropped:], dropped


def timer_quantum(samples: np.ndarray) -> float:
    """Smallest step between distinct sample values.

    The hardware counter on Apple Silicon ticks at 24 MHz (~41.67 ns), so
    every sample is an integer multiple of that step. Binning on anything
    other than the quantum produces comb-like aliasing artifacts that look
    like structure in the data but are really just the clock.

    Tick-to-nanosecond conversion truncates to integers, so a single tick
    shows up as neighbouring values (e.g. 41 and 42 ns); values within 1 ns
    of each other are merged into one cluster. A small fraction of samples
    land between ticks (timestamps taken on different cores whose counters
    are not perfectly phase-aligned), so only clusters holding a meaningful
    share of the samples are used to measure the step.
    """
    distinct, counts = np.unique(samples, return_counts=True)
    if distinct.size < 2:
        return 1.0

    centers: list[float] = []
    masses: list[int] = []
    group_values = [float(distinct[0])]
    group_mass = int(counts[0])
    for value, count in zip(distinct[1:], counts[1:]):
        if value - group_values[-1] <= 1.0:
            group_values.append(float(value))
            group_mass += int(count)
        else:
            centers.append(float(np.mean(group_values)))
            masses.append(group_mass)
            group_values, group_mass = [float(value)], int(count)
    centers.append(float(np.mean(group_values)))
    masses.append(group_mass)

    min_mass = MIN_CLUSTER_SHARE * samples.size
    dominant = np.array([c for c, m in zip(centers, masses) if m >= min_mass])
    steps = np.diff(dominant)
    steps = steps[steps > 0]
    return float(steps.min()) if steps.size else 1.0


def summarize(samples: np.ndarray) -> dict[str, float]:
    stats = {f"p{p:g}": float(np.percentile(samples, p)) for p in PERCENTILES}
    stats["mean"] = float(samples.mean())
    stats["max"] = float(samples.max())
    stats["count"] = float(samples.size)
    return stats


def plot(
    samples: np.ndarray,
    stats: dict[str, float],
    quantum: float,
    clip_pct: float,
    warmup_dropped: int,
    out_path: Path,
) -> None:
    clip_value = float(np.percentile(samples, clip_pct))
    visible = samples[samples <= clip_value]

    n_steps = int(np.ceil(clip_value / quantum)) + 1
    if n_steps <= MAX_BINS:
        # Bin edges centered on each clock tick so every bar is exactly one
        # representable latency value.
        edges = (np.arange(n_steps + 1) - 0.5) * quantum
    else:
        edges = np.linspace(0.0, clip_value, MAX_BINS + 1)

    fig, (ax_hist, ax_ccdf) = plt.subplots(1, 2, figsize=(14, 5.5), dpi=200)

    ax_hist.hist(visible, bins=edges, color="#264653", edgecolor="white", linewidth=0.4)
    ax_hist.set_yscale("log")
    ax_hist.set_xlabel("latency (ns)")
    ax_hist.set_ylabel("messages (log scale)")
    ax_hist.set_title(f"Per-message latency, clipped at p{clip_pct:g} ({clip_value:.0f} ns)")
    for pct, color in MARKER_COLORS.items():
        value = stats[f"p{pct:g}"]
        ax_hist.axvline(value, color=color, linestyle="--", linewidth=1.6, label=f"p{pct:g} = {value:.0f} ns")
    ax_hist.legend(loc="upper right", frameon=False)
    ax_hist.grid(axis="y", alpha=0.25)

    # CCDF: fraction of messages slower than x. A straight, steep drop means
    # a tight tail; a long shallow slope is the signature of preemption,
    # cache misses, or allocation stalls.
    ordered = np.sort(samples)
    tail_fraction = 1.0 - np.arange(1, ordered.size + 1) / ordered.size
    keep = tail_fraction > 0
    ax_ccdf.step(ordered[keep], tail_fraction[keep], where="post", color="#264653", linewidth=1.4)
    ax_ccdf.set_xscale("log")
    ax_ccdf.set_yscale("log")
    ax_ccdf.set_xlabel("latency (ns, log)")
    ax_ccdf.set_ylabel("P(latency > x)")
    ax_ccdf.set_title("Tail latency (complementary CDF)")
    for pct, color in MARKER_COLORS.items():
        ax_ccdf.axvline(stats[f"p{pct:g}"], color=color, linestyle="--", linewidth=1.2)
    ax_ccdf.grid(which="both", alpha=0.25)

    warmup_note = f" (first {warmup_dropped:,} dropped as warmup)" if warmup_dropped else ""
    fig.suptitle(
        f"Chronos-ML replay: {int(stats['count']):,} messages{warmup_note} | "
        f"mean {stats['mean']:.1f} ns | max {stats['max']:.0f} ns | timer quantum {quantum:.2f} ns",
        fontsize=11,
    )
    fig.tight_layout()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out_path, bbox_inches="tight")
    plt.close(fig)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path, nargs="?", default=Path("latency.csv"), help="replay harness CSV output")
    parser.add_argument("-o", "--output", type=Path, default=Path("latency_histogram.png"), help="output image path")
    parser.add_argument(
        "--warmup-fraction",
        type=float,
        default=DEFAULT_WARMUP_FRACTION,
        help="leading fraction of messages (in arrival order) to exclude as warmup (default: 0.10)",
    )
    parser.add_argument(
        "--clip-percentile",
        type=float,
        default=99.95,
        help="histogram x-axis cutoff, so rare multi-microsecond outliers don't flatten the body (CCDF is unclipped)",
    )
    parser.add_argument(
        "--quantum-ns",
        type=float,
        default=None,
        help="timer resolution in ns (default: inferred from the data; 41.67 on Apple Silicon's 24 MHz counter)",
    )
    args = parser.parse_args(argv)

    if not 0.0 <= args.warmup_fraction < 1.0:
        print(f"error: --warmup-fraction must be in [0, 1), got {args.warmup_fraction}", file=sys.stderr)
        return 2

    try:
        samples, dropped = strip_warmup(load_latencies(args.csv), args.warmup_fraction)
    except (OSError, ValueError) as err:
        print(f"error: {err}", file=sys.stderr)
        return 1

    stats = summarize(samples)
    quantum = args.quantum_ns if args.quantum_ns is not None else timer_quantum(samples)
    plot(samples, stats, quantum, args.clip_percentile, dropped, args.output)

    print(f"warmup    {dropped:,} dropped ({args.warmup_fraction:.0%})")
    print(f"samples   {int(stats['count']):,}")
    print(f"quantum   {quantum:.2f} ns")
    for pct in PERCENTILES:
        print(f"p{pct:<6g}   {stats[f'p{pct:g}']:.1f} ns")
    print(f"mean      {stats['mean']:.1f} ns")
    print(f"max       {stats['max']:.0f} ns")
    print(f"wrote     {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
