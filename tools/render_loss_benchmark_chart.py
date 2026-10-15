from __future__ import annotations

import argparse
import csv
import subprocess
from pathlib import Path

import matplotlib.pyplot as plt


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run the YCNet loss benchmark and render a portfolio chart."
    )
    parser.add_argument("--messages", type=int, default=100)
    parser.add_argument(
        "--exe",
        type=Path,
        default=Path("build/Release/loss_benchmark.exe"),
    )
    parser.add_argument("--out-dir", type=Path, default=Path("docs"))
    return parser.parse_args()


def run_benchmark(exe: Path, messages: int) -> list[dict[str, str]]:
    if not exe.exists():
        raise SystemExit(f"benchmark executable not found: {exe}")

    completed = subprocess.run(
        [str(exe), str(messages)],
        check=True,
        capture_output=True,
        text=True,
    )
    reader = csv.DictReader(completed.stdout.splitlines())
    rows = list(reader)
    if not rows:
        raise SystemExit("benchmark produced no CSV rows")
    return rows


def write_csv(rows: list[dict[str, str]], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def render_chart(rows: list[dict[str, str]], path: Path) -> None:
    loss = [int(row["loss_percent"]) for row in rows]
    retransmissions = [int(row["retransmissions"]) for row in rows]
    delivered = [int(row["delivered"]) for row in rows]
    messages = [int(row["messages"]) for row in rows]
    rto = [int(row["final_rto_ms"]) for row in rows]

    fig, ax = plt.subplots(figsize=(9, 5.2))
    bars = ax.bar([str(value) for value in loss], retransmissions)
    ax.set_title("YCNet RUDP: deterministic packet-loss verification")
    ax.set_xlabel("Injected datagram loss (%)")
    ax.set_ylabel("Retransmissions")
    ax.set_ylim(0, max(retransmissions) * 1.28 + 1)
    ax.grid(axis="y", alpha=0.25)

    for index, bar in enumerate(bars):
        ax.text(
            bar.get_x() + bar.get_width() / 2,
            bar.get_height() + 0.5,
            f"{delivered[index]}/{messages[index]} delivered\nRTO {rto[index]} ms",
            ha="center",
            va="bottom",
            fontsize=9,
        )

    fig.text(
        0.5,
        0.01,
        f"Reliable messages: {messages[0]} per condition · deterministic simulator · pre-established sessions",
        ha="center",
        fontsize=9,
    )
    fig.tight_layout(rect=(0, 0.04, 1, 1))
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(path, dpi=180)
    plt.close(fig)


def main() -> None:
    args = parse_args()
    rows = run_benchmark(args.exe, args.messages)
    csv_path = args.out_dir / "loss_benchmark.csv"
    chart_path = args.out_dir / "loss_benchmark.png"
    write_csv(rows, csv_path)
    render_chart(rows, chart_path)
    print(csv_path)
    print(chart_path)


if __name__ == "__main__":
    main()
