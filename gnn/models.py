import torch
import torch.nn.functional as F
from torch_geometric.nn import GCNConv, APPNP


class MLP(torch.nn.Module):
    def __init__(self, in_channels: int, hidden_channels: int = 64):
        super().__init__()
        self.lin1 = torch.nn.Linear(in_channels, hidden_channels)
        self.lin2 = torch.nn.Linear(hidden_channels, hidden_channels)
        self.lin3 = torch.nn.Linear(hidden_channels, 1)

    def forward(self, x, edge_index=None):
        x = self.lin1(x).relu()
        x = F.dropout(x, p=0.3, training=self.training)
        x = self.lin2(x).relu()
        x = F.dropout(x, p=0.3, training=self.training)
        x = self.lin3(x)
        return x.squeeze(-1)


class GCN(torch.nn.Module):
    def __init__(self, in_channels: int, hidden_channels: int = 64):
        super().__init__()
        self.conv1 = GCNConv(in_channels, hidden_channels)
        self.conv2 = GCNConv(hidden_channels, hidden_channels)
        self.conv3 = GCNConv(hidden_channels, 1)

    def forward(self, x, edge_index):
        x = self.conv1(x, edge_index).relu()
        x = F.dropout(x, p=0.3, training=self.training)
        x = self.conv2(x, edge_index).relu()
        x = F.dropout(x, p=0.3, training=self.training)
        x = self.conv3(x, edge_index)
        return x.squeeze(-1)


class LTGNN(torch.nn.Module):
    def __init__(
        self,
        in_channels: int,
        hidden_channels: int = 64,
        alpha: float = 0.1,
        K: int = 10,
    ):
        super().__init__()
        self.lin1 = torch.nn.Linear(in_channels, hidden_channels)
        self.lin2 = torch.nn.Linear(hidden_channels, 1)
        self.prop = APPNP(K=K, alpha=alpha)

    def forward(self, x, edge_index):
        x = self.lin1(x).relu()
        x = self.lin2(x)
        x = self.prop(x, edge_index)
        return x.squeeze(-1)
