#!/usr/bin/env python3
"""Visualize benchmark results from Google Benchmark JSON output."""

import json
import sys
from pathlib import Path
from collections import defaultdict

import matplotlib.pyplot as plt
import numpy as np


def load_benchmark_json(filepath):
    """Load and parse a benchmark JSON file."""
    with open(filepath, "r") as f:
        return json.load(f)


def extract_benchmark_data(json_data):
    """Extract relevant benchmark data from JSON."""
    results = defaultdict(lambda: defaultdict(dict))

    for bench in json_data["benchmarks"]:
        if bench["aggregate_name"] != "mean":
            continue

        label = bench.get("label", "")

        # Parse label
        parts = {}
        for item in label.split(","):
            if "=" in item:
                key, value = item.split("=", 1)
                parts[key] = value

        implementation = parts.get("implementation", "unknown")
        dataset = parts.get("dataset", "unknown")
        workload = parts.get("workload", "unknown")

        # Convert nanoseconds to milliseconds
        time_ms = bench["cpu_time"] / 1_000_000

        results[implementation][dataset][workload] = time_ms

    return results


def create_comparison_chart(all_results, output_path):
    """Create a grouped bar chart comparing all implementations."""

    # Define workload order and colors
    workloads = ["Insert", "LookupHit", "LookupMiss", "Erase"]
    datasets = ["Scalar", "HandlePayload", "CompositeKey"]

    # Color scheme
    colors = {
        "Insert": "#3498db",
        "LookupHit": "#2ecc71",
        "LookupMiss": "#e74c3c",
        "Erase": "#f39c12",
    }

    implementations = list(all_results.keys())

    # Create subplots for each dataset
    fig, axes = plt.subplots(1, 3, figsize=(18, 6))
    title = "HashMap Benchmark Comparison (1M entries, lower is better)"
    fig.suptitle(title, fontsize=16, fontweight="bold")

    for idx, dataset in enumerate(datasets):
        ax = axes[idx]

        x = np.arange(len(implementations))
        width = 0.2

        for i, workload in enumerate(workloads):
            values = []
            for impl in implementations:
                val = all_results[impl].get(dataset, {}).get(workload, 0)
                values.append(val)

            offset = width * (i - 1.5)
            ax.bar(x + offset, values, width, label=workload, color=colors[workload])

        ax.set_ylabel("Time (ms)", fontsize=12)
        ax.set_title(f"{dataset} Dataset", fontsize=14, fontweight="bold")
        ax.set_xticks(x)
        ax.set_xticklabels(implementations, rotation=15, ha="right")
        ax.legend()
        ax.grid(axis="y", alpha=0.3)

    plt.tight_layout()
    plt.savefig(output_path, dpi=300, bbox_inches="tight")
    print(f"Chart saved to: {output_path}")


def main():
    # Load all benchmark JSONs from parent directory
    script_dir = Path(__file__).parent
    root_dir = script_dir.parent

    json_files = {
        "cheesemap": root_dir / "cheesemap.json",
        "std::unordered_map": root_dir / "unordered.json",
        "tidwall": root_dir / "tidwall.json",
        "absl::flat_hash_map": root_dir / "abseil.json",
    }

    all_results = {}

    for name, filepath in json_files.items():
        if not filepath.exists():
            print(f"Warning: {filepath} not found, skipping...")
            continue

        print(f"Loading {filepath}...")
        data = load_benchmark_json(filepath)
        results = extract_benchmark_data(data)

        # Use the implementation name from the data if available
        if results:
            impl_name = list(results.keys())[0]
            all_results[impl_name] = results[impl_name]
        else:
            all_results[name] = {}

    if not all_results:
        print("Error: No valid benchmark data found!")
        sys.exit(1)

    # Create visualizations in root directory
    print("\nGenerating chart...")
    create_comparison_chart(all_results, root_dir / "benchmarks.png")

    print("\nDone!")


if __name__ == "__main__":
    main()
