import torch
import time


def train_model(
    model, x, edge_index, y, lr=0.01, weight_decay=5e-4, max_epochs=2000, patience=100
):
    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    model = model.to(device)
    x = x.to(device)
    edge_index = edge_index.to(device)
    y = y.to(device)

    n = x.size(0)
    perm = torch.randperm(n, device=device)
    split = int(0.8 * n)
    train_idx = perm[:split]
    val_idx = perm[split:]

    optimizer = torch.optim.Adam(model.parameters(), lr=lr, weight_decay=weight_decay)
    best_val_loss = float("inf")
    best_state = None
    epochs_since_improvement = 0
    final_epoch = max_epochs

    t0 = time.time()

    for epoch in range(1, max_epochs + 1):
        final_epoch = epoch
        model.train()
        optimizer.zero_grad()
        out = model(x, edge_index)
        loss = torch.nn.functional.mse_loss(out[train_idx], y[train_idx])
        loss.backward()
        optimizer.step()

        model.eval()
        with torch.no_grad():
            val_out = model(x, edge_index)
            val_loss = torch.nn.functional.mse_loss(val_out[val_idx], y[val_idx])

        if val_loss < best_val_loss:
            best_val_loss = val_loss
            best_state = {k: v.cpu().clone() for k, v in model.state_dict().items()}
            epochs_since_improvement = 0
        else:
            epochs_since_improvement += 1

        if epochs_since_improvement >= patience:
            break

    elapsed = time.time() - t0

    model.load_state_dict(best_state)
    model = model.to("cpu")
    model.eval()
    with torch.no_grad():
        pred = model(x.cpu(), edge_index.cpu())

    return {
        "model": model,
        "predictions": pred.cpu(),
        "best_val_loss": best_val_loss,
        "epochs": final_epoch,
        "time": elapsed,
    }
