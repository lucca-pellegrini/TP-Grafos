#ifndef GRAPH_H
#define GRAPH_H

#include <stdbool.h>

struct graph {
	int n; /* number of nodes */
	int m; /* number of edges */
	const int *offsets; /* [n+1] CSR offsets */
	const int *edges; /* [m] CSR destinations */
	const char (*iata)[4];
	const char (*name)[64];
	const char (*city)[64];
	const char (*country)[64];
	bool dynamic; /* if true, arrays are heap-allocated */
};

struct node_features {
	double degree; /* normalized degree centrality */
	double interconnect; /* normalized neighbor interconnectivity */
	double betweenness; /* normalized betweenness centrality */
	double score; /* combined criticity score */
};

/* Load the graph from the generated graph_data.h constants */
struct graph graph_load(void);

/* Load a graph from a CSR binary directory created by preprocess.pl --csr-out */
struct graph graph_load_from(const char *dir);

/* Free graph resources (frees heap memory if dynamic is set) */
void graph_free(struct graph *g);

#endif /* GRAPH_H */
