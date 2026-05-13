#!/usr/bin/env python3

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

import matplotlib.pyplot as plt


BENCHES = [
    ("cheesemap", "cheesemap_bench"),
    ("cheesemap_old", "cheesemap_old_bench"),
    ("abseil", "abseil_bench"),
    ("khash", "khash_bench"),
]

OPERATIONS = [
    "insert",
    "replace",
    "lookup_hit",
    "lookup_miss",
    "remove",
]


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def run_command(cmd: list[str], cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        cmd,
        cwd=cwd,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )


def run_bench(binary: Path, min_time: str, repetitions: int, benchmark_filter: str) -> dict:
    cmd = [
        str(binary),
        "--benchmark_format=json",
        f"--benchmark_min_time={min_time}",
        f"--benchmark_repetitions={repetitions}",
        f"--benchmark_filter={benchmark_filter}",
    ]
    result = run_command(cmd, binary.parent)
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit(result.returncode)
    return json.loads(result.stdout)


def parse_name(name: str) -> tuple[str, str, int] | None:
    match = re.fullmatch(r"([^/]+)/([^/]+)/(\d+)(?:_(?:mean|median|stddev|cv))?", name)
    if match is None:
        return None
    implementation, operation, size = match.groups()
    return implementation, operation, int(size)


def collect_results(root: Path, build_dir: Path, min_time: str, repetitions: int, benchmark_filter: str) -> dict:
    runs = []
    rows = []

    for label, target in BENCHES:
        binary = build_dir / "benches" / target
        if not binary.exists():
            raise SystemExit(f"missing benchmark binary: {binary}")

        print(f"running {label}: {binary}", flush=True)
        data = run_bench(binary, min_time, repetitions, benchmark_filter)
        runs.append({"label": label, "binary": str(binary), "json": data})

        for bench in data.get("benchmarks", []):
            if repetitions > 1 and bench.get("run_type") != "aggregate":
                continue
            if repetitions > 1 and not bench.get("name", "").endswith("_mean"):
                continue

            parsed = parse_name(bench.get("name", ""))
            if parsed is None:
                continue

            implementation, operation, size = parsed
            if operation not in OPERATIONS:
                continue

            throughput = bench.get("items_per_second")
            if throughput is None:
                continue

            rows.append(
                {
                    "implementation": implementation,
                    "operation": operation,
                    "size": size,
                    "items_per_second": throughput,
                    "cpu_time": bench.get("cpu_time"),
                    "real_time": bench.get("real_time"),
                    "time_unit": bench.get("time_unit"),
                }
            )

    return {"runs": runs, "rows": rows}


def plot_results(rows: list[dict], output: Path) -> None:
    by_operation: dict[str, dict[str, list[tuple[int, float]]]] = {
        operation: {} for operation in OPERATIONS
    }

    for row in rows:
        operation = row["operation"]
        implementation = row["implementation"]
        size = row["size"]
        throughput = row["items_per_second"] / 1_000_000.0
        by_operation[operation].setdefault(implementation, []).append((size, throughput))

    fig, axes = plt.subplots(len(OPERATIONS), 1, figsize=(11, 16), sharex=True)
    fig.suptitle("Hash Map Throughput", fontsize=16)

    for ax, operation in zip(axes, OPERATIONS):
        for implementation, points in sorted(by_operation[operation].items()):
            points = sorted(points)
            sizes = [size for size, _ in points]
            throughput = [value for _, value in points]
            ax.plot(sizes, throughput, marker="o", linewidth=2, label=implementation)

        ax.set_xscale("log", base=4)
        ax.set_ylabel("M ops/s")
        ax.set_title(operation)
        ax.grid(True, which="both", linestyle=":", linewidth=0.7)

    axes[-1].set_xlabel("items")
    axes[0].legend(loc="best")
    fig.tight_layout(rect=(0, 0, 1, 0.98))
    fig.savefig(output, dpi=160)
    plt.close(fig)


def main() -> int:
    root = repo_root()
    parser = argparse.ArgumentParser(description="Run all hash map benchmarks and plot throughput.")
    parser.add_argument("--build-dir", default="build", help="CMake/Ninja build directory.")
    parser.add_argument("--output-dir", default=Path(__file__).resolve().parent, help="Directory for JSON and chart output.")
    parser.add_argument("--min-time", default="1s", help="Google Benchmark --benchmark_min_time value.")
    parser.add_argument("--repetitions", type=int, default=3, help="Google Benchmark repetitions.")
    parser.add_argument("--filter", default=".*", help="Google Benchmark filter applied to every binary.")
    args = parser.parse_args()

    build_dir = (root / args.build_dir).resolve()
    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    result = collect_results(root, build_dir, args.min_time, args.repetitions, args.filter)
    json_path = output_dir / "benchmarks.json"
    chart_path = output_dir / "throughput.png"

    json_path.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(f"writing chart: {chart_path}", flush=True)
    plot_results(result["rows"], chart_path)

    print(f"wrote {json_path}")
    print(f"wrote {chart_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
