import numpy as np
from scipy.sparse.csgraph import connected_components
from tqdm import tqdm


def compute_robustness_scores(offsets: np.ndarray, edges: np.ndarray) -> np.ndarray:
    n = len(offsets) - 1

    src = np.repeat(np.arange(n, dtype=np.int32), np.diff(offsets))
    dst = edges

    n_comps_base, labels_base = connected_components(
        csr_from_edges(src, dst, n), directed=False, return_labels=True
    )
    base_lcc = _largest_component_size(labels_base, n)

    criticality = np.zeros(n, dtype=np.float64)

    for v in tqdm(range(n), desc="Node removal simulation", unit="node"):
        mask = (src != v) & (dst != v)
        src_v = src[mask]
        dst_v = dst[mask]
        n_comps, labels = connected_components(
            csr_from_edges(src_v, dst_v, n), directed=False, return_labels=True
        )
        lcc_size = _largest_component_size(labels, n)

        drop_lcc = (base_lcc - lcc_size) / base_lcc
        increase_comps = (n_comps - n_comps_base) / max(n_comps_base, 1)

        criticality[v] = 0.6 * drop_lcc + 0.4 * increase_comps

    criticality = (criticality - criticality.min()) / max(
        criticality.max() - criticality.min(), 1e-10
    )
    return criticality


def _largest_component_size(labels: np.ndarray, n: int) -> int:
    _, counts = np.unique(labels, return_counts=True)
    return int(counts.max())


def csr_from_edges(src: np.ndarray, dst: np.ndarray, n: int):
    from scipy.sparse import csr_matrix

    data = np.ones(len(src), dtype=np.float64)
    return csr_matrix((data, (src, dst)), shape=(n, n))
