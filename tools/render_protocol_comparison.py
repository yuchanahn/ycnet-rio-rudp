from __future__ import annotations

import csv
import argparse
from pathlib import Path

import matplotlib.pyplot as plt


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("--name", default="protocol_comparison_policy")
    args = parser.parse_args()
    with (root / f"docs/{args.name}.csv").open(encoding="utf-8", newline="") as source:
        rows = list(csv.DictReader(source))
    protocols = ["ycnet", "kcp_normal", "kcp_fast"]
    colors = ["#bf5c49", "#6482a1", "#318c72"]
    complete = [sum(r["complete"] == "1" for r in rows if r["protocol"] == p) for p in protocols]
    invalid = [sum(r["valid"] == "0" for r in rows if r["protocol"] == p) for p in protocols]
    counts = [sum(r["protocol"] == p for r in rows) for p in protocols]
    fig, (left, right) = plt.subplots(1, 2, figsize=(11, 4.5), constrained_layout=True)
    left.bar(protocols, complete, color=colors)
    left.set_ylim(0, max(counts) * 1.18)
    left.set_title("Completed scenarios (all payloads / seeds)")
    left.set_ylabel("Scenarios")
    for i, (done, total, bad) in enumerate(zip(complete, counts, invalid)):
        left.text(i, done + 3, f"{done}/{total}\ninvalid: {bad}", ha="center", va="bottom")

    subset = [r for r in rows if r["payload_bytes"] == "256" and r["rtt_ms"] == "80"
              and r["loss_percent"] == "10"]
    for i, (protocol, color) in enumerate(zip(protocols, colors)):
        ratios = [int(r["p95_end_to_end_ms"]) for r in subset if r["protocol"] == protocol]
        right.scatter([i] * len(ratios), ratios, color=color, s=65, alpha=0.8)
    right.set_xticks(range(len(protocols)), protocols)
    right.set_yscale("log")
    right.set_ylabel("p95 end-to-end latency (ms, log scale)")
    right.set_title("RTT 80 ms / loss 10% / payload 256 B\nSeeds 42, 43, 44")
    for axis in (left, right):
        axis.spines[["top", "right"]].set_visible(False)
        axis.grid(axis="y", alpha=0.2)
        axis.set_axisbelow(True)
    fig.suptitle("YCNet / KCP protocol simulation — completion and latency", fontsize=14)
    fig.savefig(root / f"docs/{args.name}.png", dpi=170)
    plt.close(fig)


if __name__ == "__main__":
    main()
