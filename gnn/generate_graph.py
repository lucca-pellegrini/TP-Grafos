import numpy as np
from pathlib import Path
import struct


def _fake_iata(i: int) -> str:
    a = i // 26
    b = i % 26
    return f"{chr(65 + a)}{chr(65 + b)}{chr(65 + (i % 26))}"


def generate_er(n: int, p: float, seed: int = 42) -> tuple:
    rng = np.random.default_rng(seed)
    adj = rng.random((n, n)) < p
    adj = np.triu(adj, 1)
    src, dst = np.where(adj)
    return src.astype(np.int32), dst.astype(np.int32)


def generate_ba(n: int, m0: int, m: int, seed: int = 42) -> tuple:
    rng = np.random.default_rng(seed)
    adj = np.zeros((n, n), dtype=bool)
    for i in range(m0):
        for j in range(i + 1, m0):
            adj[i, j] = adj[j, i] = rng.random() < 0.5
    degrees = np.sum(adj, axis=1)
    for i in range(m0, n):
        probs = degrees[:i].astype(np.float64) / np.sum(degrees[:i])
        targets = rng.choice(i, size=m, replace=False, p=probs)
        for t in targets:
            adj[i, t] = adj[t, i] = True
            degrees[i] += 1
            degrees[t] += 1
    src, dst = np.where(np.triu(adj, 1))
    return src.astype(np.int32), dst.astype(np.int32)


def generate_ws(n: int, k: int, p: float, seed: int = 42) -> tuple:
    rng = np.random.default_rng(seed)
    adj = np.zeros((n, n), dtype=bool)
    half = k // 2
    for i in range(n):
        for j in range(1, half + 1):
            adj[i, (i + j) % n] = True
            adj[(i + j) % n, i] = True
    for i in range(n):
        for j in range(i + 1, n):
            if adj[i, j] and rng.random() < p:
                adj[i, j] = adj[j, i] = False
                new_j = int(rng.integers(0, n))
                while new_j == i or adj[i, new_j]:
                    new_j = int(rng.integers(0, n))
                adj[i, new_j] = adj[new_j, i] = True
    src, dst = np.where(np.triu(adj, 1))
    return src.astype(np.int32), dst.astype(np.int32)


def write_csr_graph(outdir: str, name: str, src: np.ndarray, dst: np.ndarray):
    d = Path(outdir)
    d.mkdir(parents=True, exist_ok=True)

    n = int(max(np.max(src), np.max(dst))) + 1
    m = len(src)

    offsets = np.zeros(n + 1, dtype=np.int32)
    np.add.at(offsets, src + 1, 1)
    np.cumsum(offsets, out=offsets)

    edges_sorted = np.zeros(m, dtype=np.int32)
    for u in range(n):
        lo = offsets[u]
        hi = offsets[u + 1]
        mask = src == u
        edges_sorted[lo:hi] = dst[mask]

    with open(d / "graph.meta", "w") as f:
        f.write(f"{n}\n{m}\n{name}\n0\n")

    with open(d / "offsets.bin", "wb") as f:
        f.write(offsets.tobytes())

    with open(d / "edges.bin", "wb") as f:
        f.write(edges_sorted.tobytes())

    with open(d / "nodes.csv", "w") as f:
        f.write("iata,name,city,country\n")
        for i in range(n):
            iata = _fake_iata(i)
            f.write(f"{iata},Airport {i},City {i},Country {i // 100}\n")

    print(f"[generate] {name}: {n} nodes, {m} edges -> {d}/")


def generate_all(output_dir: str = "build/gen/csr", seed: int = 42):
    base = Path(output_dir)

    graphs = []

    er_opts = [(500, 0.01), (1000, 0.005), (2000, 0.003)]
    for n, p in er_opts:
        name = f"er_n{n}_p{p}"
        out = base / name
        s, d = generate_er(n, p, seed)
        write_csr_graph(str(out), name, s, d)
        graphs.append(name)

    ba_opts = [(500, 5, 3), (1000, 5, 3), (2000, 10, 4)]
    for n, m0, m in ba_opts:
        name = f"ba_n{n}_m0{m0}_m{m}"
        out = base / name
        s, d = generate_ba(n, m0, m, seed)
        write_csr_graph(str(out), name, s, d)
        graphs.append(name)

    ws_opts = [(500, 10, 0.1), (1000, 10, 0.1)]
    for n, k, p in ws_opts:
        name = f"ws_n{n}_k{k}_p{p}"
        out = base / name
        s, d = generate_ws(n, k, p, seed)
        write_csr_graph(str(out), name, s, d)
        graphs.append(name)

    return [str(base / g) for g in graphs]


if __name__ == "__main__":
    import sys

    outdir = sys.argv[1] if len(sys.argv) > 1 else "build/gen/csr"
    generate_all(outdir)
