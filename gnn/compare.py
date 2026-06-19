import numpy as np
import torch
from scipy.stats import spearmanr


def top_k_overlap(ranking_a: np.ndarray, ranking_b: np.ndarray, k: int) -> float:
    top_a = set(ranking_a[:k])
    top_b = set(ranking_b[:k])
    return len(top_a & top_b) / k


def compute_rankings(scores: torch.Tensor) -> np.ndarray:
    return np.argsort(-scores.numpy())


def _label_short(label: str, width: int = 14) -> str:
    return label if len(label) <= width else label[: width - 3] + "..."


def compare_methods(results: dict, nodes_df=None, k_top: int = 30):
    gt_scores = results["ground_truth"]
    gt_rank = compute_rankings(gt_scores)
    gt_np = gt_scores.numpy()

    configs = [
        ("classical_rank", "Classical (heuristic)", "classical", False),
        ("mlp_f3_rank", "MLP (F3)", "mlp_f3", True),
        ("gcn_f3_rank", "GCN (F3)", "gcn_f3", True),
        ("ltgnn_f3_rank", "LTGNN (F3)", "ltgnn_f3", True),
        ("mlp_f1_rank", "MLP (F1)", "mlp_f1", True),
        ("gcn_f1_rank", "GCN (F1)", "gcn_f1", True),
        ("ltgnn_f1_rank", "LTGNN (F1)", "ltgnn_f1", True),
    ]

    active = [
        (rk, lb, sk, ht) for rk, lb, sk, ht in configs if results.get(sk) is not None
    ]

    ks = [5, 10, 20, 50, 100]
    print(f"\n{'─' * 110}")
    print(f"{'Comparison vs Ground Truth (exhaustive node removal)':^110}")
    print(f"{'─' * 110}")
    header = f"{'Method':<22}"
    header += "".join(f"{'Top-' + str(k):<10}" for k in ks)
    header += f"{'Spearman ρ':<12} {'MAE':<12} {'Time (s)':<10}"
    print(header)
    print(f"{'─' * 110}")

    for rank_key, label, scores_key, has_time in active:
        scores = results[scores_key]
        pred_np = scores.numpy() if isinstance(scores, torch.Tensor) else scores
        pred_rank = compute_rankings(
            torch.tensor(pred_np) if not isinstance(scores, torch.Tensor) else scores
        )
        rho, _ = spearmanr(gt_np, pred_np)
        mae = np.mean(np.abs(gt_np - pred_np))
        overlaps = "".join(f"{top_k_overlap(gt_rank, pred_rank, k):<10.2f}" for k in ks)
        train_time = results.get(f"{scores_key}_time", 0) if has_time else 0
        print(f"{label:<22} {overlaps} {rho:<12.4f} {mae:<12.6f} {train_time:<10.2f}")

    print(f"{'─' * 110}")

    # ── Ground-truth top-K with airport names ──
    print(f"\n{'─' * 100}")
    print(f"{f'Ground Truth Top-{k_top} Critical Nodes':^100}")
    print(f"{'─' * 100}")
    gt_header = f"{'GT Rank':<8} {'Idx':<6} {'IATA':<6} {'Airport Name':<36} {'City':<22} {'Score':<10}"
    print(gt_header)
    print(f"{'─' * 100}")

    for i in range(min(k_top, len(gt_rank))):
        v = gt_rank[i]
        if nodes_df is not None:
            row = nodes_df.iloc[v]
            iata = row.get("iata", "")
            name = (row.get("name", "") or "")[:35]
            city = (row.get("city", "") or "")[:21]
        else:
            iata, name, city = "", "", ""
        print(f"{i + 1:<8} {v:<6} {iata:<6} {name:<36} {city:<22} {gt_np[v]:<10.6f}")

    print(f"{'─' * 100}")

    # ── Per-method: which GT critical nodes did each method find? ──
    top_k_check = 20
    print(f"\n{'─' * 110}")
    print(
        f"{'Which Ground-Truth Top-20 Nodes Each Method Found (rank assigned, or — if missed)':^110}"
    )
    print(f"{'─' * 110}")
    sub_header = f"{'GT Rank':<8} {'Idx':<5} {'IATA':<5}"
    for _, label, sk, _ in active:
        sub_header += f"{_label_short(label, 13):<14}"
    print(sub_header)
    print(f"{'─' * 110}")

    gt_top_set = set(gt_rank[:top_k_check])

    for i in range(top_k_check):
        v = gt_rank[i]
        iata = (nodes_df.iloc[v].get("iata", "") if nodes_df is not None else "") or ""
        row = f"{i + 1:<8} {v:<5} {iata:<5}"
        for rank_key, label, scores_key, ht in active:
            rank_arr = results[rank_key]
            # Find where this node appears in the method's ranking
            try:
                pos = int(np.where(rank_arr == v)[0][0]) + 1
                marker = f"#{pos}"
            except (IndexError, TypeError):
                marker = "—"
            row += f"{marker:<14}"
        print(row)

    print(f"{'─' * 110}")

    # ── Miss count ──
    print(
        f"\n  Summary: How many of the GT Top-{top_k_check} are found in each method's Top-K?"
    )
    for k in [5, 10, 20]:
        gt_set = set(gt_rank[:k])
        parts = f"    Top-{k}:  ".rjust(12)
        for _, label, scores_key, ht in active:
            rank_arr = (
                results[f"{scores_key}_rank"]
                if f"{scores_key}_rank" in results
                else results.get(scores_key + "_rank")
            )
            if rank_arr is None:
                continue
            method_top = set(rank_arr[:k])
            found = len(gt_set & method_top)
            parts += f"  {_label_short(label, 13)}: {found}/{k}"
        print(parts)
    print()
