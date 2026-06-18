#ifndef FEATURES_H
#define FEATURES_H

#include "graph.h"

/* Compute raw degree for each node: deg[v] = number of incident edges */
double *compute_degrees_raw(const struct graph *g);

/* Compute neighbor interconnectivity (clustering coefficient analog):
   For each node v, fraction of neighbor pairs (u,w) that share an edge.
   Uses binary search on sorted neighbor lists for O(d(v)² log d(v)).
   Returns array of doubles in [0,1]. Nodes with degree < 2 get 0. */
double *compute_interconnectivity(const struct graph *g);

/* Betweenness centrality using Brandes' algorithm from sampled sources.
   Samples K source nodes uniformly at random (with replacement allowed).
   Complexity: O(K * (n + m)).
   If K <= 0 or K >= n, uses all nodes (exact Brandes). */
double *compute_betweenness_sampled(const struct graph *g, int k);

/* Convenience: exact betweenness (K = n). */
double *compute_betweenness_exact(const struct graph *g);

#endif /* FEATURES_H */
