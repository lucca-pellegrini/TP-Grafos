#include <stdlib.h>
#include <string.h>

#include "scoring.h"
#include "graph.h"

void normalize(double *arr, int n)
{
	double min_val = arr[0];
	double max_val = arr[0];
	for (int i = 1; i < n; i++) {
		if (arr[i] < min_val)
			min_val = arr[i];
		if (arr[i] > max_val)
			max_val = arr[i];
	}
	double range = max_val - min_val;
	if (range == 0.0) {
		for (int i = 0; i < n; i++)
			arr[i] = 0.0;
	} else {
		for (int i = 0; i < n; i++) {
			arr[i] = (arr[i] - min_val) / range;
		}
	}
}

struct node_features *compute_scores(const struct graph *g, const double *raw_degree,
				     const double *interconnect, const double *betweenness,
				     const double weights[3])
{
	int n = g->n;
	struct node_features *feat = calloc((size_t)n, sizeof(struct node_features));
	if (!feat)
		return NULL;

	/* Copy and normalize each feature */
	double *deg_norm = malloc(sizeof(double[n]));
	double *inter_norm = malloc(sizeof(double[n]));
	double *btwn_norm = malloc(sizeof(double[n]));

	if (!deg_norm || !inter_norm || !btwn_norm) {
		free(deg_norm);
		free(inter_norm);
		free(btwn_norm);
		free(feat);
		return NULL;
	}

	memcpy(deg_norm, raw_degree, sizeof(double[n]));
	memcpy(inter_norm, interconnect, sizeof(double[n]));
	memcpy(btwn_norm, betweenness, sizeof(double[n]));

	normalize(deg_norm, n);
	normalize(inter_norm, n);
	normalize(btwn_norm, n);

	double w_deg = weights[0];
	double w_inter = weights[1];
	double w_btwn = weights[2];

	for (int v = 0; v < n; v++) {
		double wd = w_deg;
		double wi = w_inter;
		double wb = w_btwn;

		int d = g->offsets[v + 1] - g->offsets[v];
		if (d < 2 && wi > 0) {
			/* Redistribute interconnect weight proportionally */
			double total = wd + wb;
			if (total > 0) {
				wd += wi * (wd / total);
				wb += wi * (wb / total);
			} else {
				wd += wi / 2.0;
				wb += wi / 2.0;
			}
			wi = 0.0;
		}

		feat[v].degree = deg_norm[v];
		feat[v].interconnect = inter_norm[v];
		feat[v].betweenness = btwn_norm[v];
		feat[v].score = wd * deg_norm[v] + wi * inter_norm[v] + wb * btwn_norm[v];
	}

	free(deg_norm);
	free(inter_norm);
	free(btwn_norm);

	return feat;
}

int *rank_nodes(const struct node_features *features, int n)
{
	/* Build index array and sort by score descending */
	int *idx = malloc(sizeof(int[n]));
	if (!idx)
		return NULL;
	for (int i = 0; i < n; i++)
		idx[i] = i;

	/* Insertion sort by score descending (simple, n=3216 is fine) */
	for (int i = 1; i < n; i++) {
		int key = idx[i];
		double key_score = features[key].score;
		int j = i - 1;
		while (j >= 0 && features[idx[j]].score < key_score) {
			idx[j + 1] = idx[j];
			j--;
		}
		idx[j + 1] = key;
	}

	return idx;
}
