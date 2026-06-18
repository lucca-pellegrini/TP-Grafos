#ifndef SCORING_H
#define SCORING_H

#include "graph.h"

/* Normalize array of length n to [0,1] using min-max normalization. */
void normalize(double *arr, int n);

/* Compute criticity score for each node.
   weights: {w_degree, w_interconnect, w_betweenness}, must sum to 1.0.
   If degree[v] < 2, interconnect weight is redistributed to degree and betweenness.
   Returns newly allocated array of node features. */
struct node_features *compute_scores(const struct graph *g, const double *degree,
				     const double *interconnect, const double *betweenness,
				     const double weights[3]);

/* Sort nodes by score descending. Returns indices [0..n-1] sorted. */
int *rank_nodes(const struct node_features *features, int n);

#endif /* SCORING_H */
