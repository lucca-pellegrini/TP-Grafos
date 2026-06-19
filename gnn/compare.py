import numpy as np
import torch
from scipy.stats import spearmanr


def top_k_overlap(ranking_a: np.ndarray, ranking_b: np.ndarray, k: int) -> float:
    top_a = set(ranking_a[:k])
    top_b = set(ranking_b[:k])
    return len(top_a & top_b) / k


def compute_rankings(scores: torch.Tensor) -> np.ndarray:
    return np.argsort(-scores.numpy())


def compare_methods(results: dict):
    gt_scores = results["ground_truth"]
    gt_rank = compute_rankings(gt_scores)

    configs = [
        ("classical_rank", "Classical (heuristic)", "classical", False),
        ("mlp_f3_rank", "MLP (F3)", "mlp_f3", True),
        ("gcn_f3_rank", "GCN (F3)", "gcn_f3", True),
        ("ltgnn_f3_rank", "LTGNN (F3)", "ltgnn_f3", True),
        ("mlp_f1_rank", "MLP (F1)", "mlp_f1", True),
        ("gcn_f1_rank", "GCN (F1)", "gcn_f1", True),
        ("ltgnn_f1_rank", "LTGNN (F1)", "ltgnn_f1", True),
    ]

    print(f"\n{'─' * 90}")
    print(f"{'Comparison vs Ground Truth':^90}")
    print(f"{'─' * 90}")
    print(
        f"{'Method':<25} {'Spearman ρ':<12} {'MAE':<12} {'Top-10':<8} {'Top-20':<8} {'Time (s)':<10}"
    )
    print(f"{'─' * 90}")

    gt_np = gt_scores.numpy()

    for rank_key, label, scores_key, has_time in configs:
        scores = results.get(scores_key)
        if scores is None:
            continue
        pred_np = scores.numpy() if isinstance(scores, torch.Tensor) else scores
        pred_rank = compute_rankings(
            torch.tensor(pred_np) if not isinstance(scores, torch.Tensor) else scores
        )
        rho, _ = spearmanr(gt_np, pred_np)
        mae = np.mean(np.abs(gt_np - pred_np))
        top10 = top_k_overlap(gt_rank, pred_rank, 10)
        top20 = top_k_overlap(gt_rank, pred_rank, 20)
        train_time = results.get(f"{scores_key}_time", 0) if has_time else 0
        print(
            f"{label:<25} {rho:<12.4f} {mae:<12.6f} {top10:<8.2f} {top20:<8.2f} {train_time:<10.2f}"
        )

    print(f"{'─' * 90}")

    n_methods = sum(1 for _, _, k, _ in configs if results.get(k) is not None)
    if n_methods > 0:
        print(f"\n{'─' * 90}")
        print(f"{'Top-10 Critical Nodes by Each Method':^90}")
        print(f"{'─' * 90}")
        header = f"{'Rank':<6}"
        for rk, label, sk, _ in configs:
            if results.get(sk) is not None:
                short = label if len(label) <= 14 else label[:11] + "..."
                header += f"{short:<15}"
        print(header)
        print(f"{'─' * 90}")

        for i in range(10):
            row = f"{i + 1:<6}"
            for rank_key, _, scores_key, _ in configs:
                rank_arr = (
                    results.get(rank_key)
                    if results.get(scores_key) is not None
                    else None
                )
                node = rank_arr[i] if rank_arr is not None else -1
                row += f"{node:<15}"
            print(row)

        print(f"{'─' * 90}")
