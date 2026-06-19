import struct
import numpy as np
import torch
from pathlib import Path


def load_csr_graph(graph_dir: str, make_undirected: bool = True) -> dict:
    d = Path(graph_dir)

    with open(d / "graph.meta") as f:
        n = int(f.readline())
        m = int(f.readline())
        name = f.readline().strip()
        directed = bool(int(f.readline()))

    offsets = np.fromfile(d / "offsets.bin", dtype=np.int32)
    edges = np.fromfile(d / "edges.bin", dtype=np.int32)

    assert len(offsets) == n + 1
    assert len(edges) == m

    src = np.repeat(np.arange(n, dtype=np.int32), np.diff(offsets))
    dst = edges

    if make_undirected:
        src_all = np.concatenate([src, dst])
        dst_all = np.concatenate([dst, src])
        edge_index = torch.tensor(
            np.stack([src_all, dst_all], axis=0), dtype=torch.long
        )
    else:
        edge_index = torch.tensor(np.stack([src, dst], axis=0), dtype=torch.long)

    import pandas as pd

    nodes_df = pd.read_csv(d / "nodes.csv")

    return {
        "n": n,
        "m": m,
        "name": name,
        "directed": directed,
        "edge_index": edge_index,
        "offsets": offsets,
        "edges": edges,
        "nodes": nodes_df,
    }


def load_classical_scores(csv_path: str) -> dict:
    import pandas as pd

    df = pd.read_csv(csv_path)
    n = len(df)
    scores = torch.zeros(n, dtype=torch.float)
    degree = torch.zeros(n, dtype=torch.float)
    interconnect = torch.zeros(n, dtype=torch.float)
    betweenness = torch.zeros(n, dtype=torch.float)
    for _, row in df.iterrows():
        idx = int(row["node_idx"])
        scores[idx] = row["score"]
        degree[idx] = row["degree"]
        interconnect[idx] = row["interconnectivity"]
        betweenness[idx] = row["betweenness"]
    features = torch.stack([degree, interconnect, betweenness], dim=1)
    return {
        "scores": scores,
        "features": features,
        "df": df,
    }


def csr_to_edge_list(offsets: np.ndarray, edges: np.ndarray) -> tuple:
    n = len(offsets) - 1
    src = np.repeat(np.arange(n, dtype=np.int32), np.diff(offsets))
    dst = edges
    return src, dst
