import sys
import subprocess
from pathlib import Path

from graph_loader import load_csr_graph, load_classical_scores
from robustness import compute_robustness_scores
from models import MLP, GCN, LTGNN
from train import train_model
from compare import compare_methods, compute_rankings

import torch
import numpy as np


def main():
    if len(sys.argv) < 2:
        print("Usage: python run.py <graph_dir> [--classical-csv <path>]")
        sys.exit(1)

    graph_dir = sys.argv[1]
    classical_csv = None
    if "--classical-csv" in sys.argv:
        idx = sys.argv.index("--classical-csv")
        classical_csv = sys.argv[idx + 1]

    print("=" * 70)
    print("  Pipeline de Comparação: Clássico vs GCN vs LTGNN")
    print("=" * 70)

    print(f"\n[1/5] Loading graph from {graph_dir}...")
    graph = load_csr_graph(graph_dir, make_undirected=True)
    n = graph["n"]
    print(f"   {n} nodes, {graph['m']} edges")

    edge_index = graph["edge_index"]
    offsets = graph["offsets"]
    edges = graph["edges"]

    classical_data = None
    classical_rank = None
    if classical_csv:
        print(f"\n[2/5] Loading classical scores from {classical_csv}...")
        classical_data = load_classical_scores(classical_csv)
        print(f"   Loaded scores for {len(classical_data['scores'])} nodes")

    print(f"\n[3/5] Computing ground truth via exhaustive node removal...")
    gt_scores = compute_robustness_scores(
        offsets.numpy() if isinstance(offsets, torch.Tensor) else offsets,
        edges.numpy() if isinstance(edges, torch.Tensor) else edges,
    )
    gt_tensor = torch.tensor(gt_scores, dtype=torch.float)

    results = {
        "ground_truth": gt_tensor,
        "ground_truth_rank": compute_rankings(gt_tensor),
    }

    if classical_data:
        results["classical"] = classical_data["scores"]
        results["classical_rank"] = compute_rankings(classical_data["scores"])

    n_features = 3
    if classical_data:
        x = classical_data["features"]
    else:
        deg = np.diff(offsets).astype(np.float32)
        deg = (deg - deg.min()) / max(deg.max() - deg.min(), 1e-10)
        inter = np.zeros(n, dtype=np.float32)
        btwn = np.zeros(n, dtype=np.float32)
        x = torch.tensor(np.stack([deg, inter, btwn], axis=1), dtype=torch.float)

    x_f3 = x

    deg_raw = torch.tensor(
        np.diff(offsets).astype(np.float32).reshape(-1, 1), dtype=torch.float
    )
    deg_raw = (deg_raw - deg_raw.min()) / max(deg_raw.max() - deg_raw.min(), 1e-10)
    x_f1 = deg_raw

    print(f"\n[4/5] Training models...")
    model_configs = [
        ("MLP (F3)", "mlp_f3", MLP, {"in_channels": 3}),
        ("GCN (F3)", "gcn_f3", GCN, {"in_channels": 3}),
        ("LTGNN (F3)", "ltgnn_f3", LTGNN, {"in_channels": 3}),
        ("MLP (F1)", "mlp_f1", MLP, {"in_channels": 1}),
        ("GCN (F1)", "gcn_f1", GCN, {"in_channels": 1}),
        ("LTGNN (F1)", "ltgnn_f1", LTGNN, {"in_channels": 1}),
    ]

    for name, key, model_cls, kwargs in model_configs:
        in_ch = kwargs["in_channels"]
        features = x_f3 if in_ch == 3 else x_f1
        print(f"   Training {name} ...")
        train_result = train_model(model_cls(**kwargs), features, edge_index, gt_tensor)
        results[f"{key}"] = train_result["predictions"]
        results[f"{key}_rank"] = compute_rankings(train_result["predictions"])
        results[f"{key}_time"] = train_result["time"]
        print(
            f"     val_loss={train_result['best_val_loss']:.6f}, "
            f"epochs={train_result['epochs']}, time={train_result['time']:.2f}s"
        )

    print(f"\n[5/5] Comparison results:")
    compare_methods(results, nodes_df=graph["nodes"])

    print("\nDone.")


if __name__ == "__main__":
    main()
