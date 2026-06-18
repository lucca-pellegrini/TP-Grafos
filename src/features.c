#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "features.h"
#include "graph.h"

/// Faz busca binária na lista de vizinhos ordenada.
static bool has_edge(const struct graph *g, int u, int v)
{
	int lo = g->offsets[u];
	int hi = g->offsets[u + 1] - 1;
	while (lo <= hi) {
		int mid = lo + (hi - lo) / 2;
		int dst = g->edges[mid];
		if (dst == v)
			return true;
		if (dst < v)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	return false;
}

/// Computa e retorna um array com os graus dos vértices.
double *compute_degrees_raw(const struct graph *g)
{
	int n = g->n;
	double *deg = malloc(sizeof(double[n]));

	if (!deg)
		return NULL;

	for (int v = 0; v < n; v++)
		deg[v] = (g->offsets[v + 1] - g->offsets[v]);

	return deg;
}

/// Computa o coeficiente de clustering local de cada vértice.
/// Para cada vértice v, calcula a fração de pares de vizinhos que estão
/// conectados entre si. Retorna array com o valor para cada vértice, ou 0 se
/// tiver menos do que dois vizinhos.
double *compute_interconnectivity(const struct graph *g)
{
	int n = g->n;
	double *inter = malloc(sizeof(double[n]));
	if (!inter)
		return NULL;

	for (int v = 0; v < n; v++) {
		int d = g->offsets[v + 1] - g->offsets[v];
		if (d < 2) {
			inter[v] = 0.0;
			continue;
		}
		const int *neighbors = &g->edges[g->offsets[v]];
		int common = 0;

		for (int i = 0; i < d; i++) {
			int u = neighbors[i];

			for (int j = i + 1; j < d; j++) {
				int w = neighbors[j];

				// Testamos primeiro o vértice de grau menor, o que é mais rápido.
				int du = g->offsets[u + 1] - g->offsets[u];
				int dw = g->offsets[w + 1] - g->offsets[w];

				if (has_edge(g, du <= dw ? u : w, du <= dw ? w : u))
					common++;
			}
		}

		double total_pairs = (double)d * (double)(d - 1) / 2.0;
		inter[v] = (double)common / total_pairs;
	}

	return inter;
}

/// Tipo de dado auxiliar: vetor flexível de inteiros
struct int_vector {
	int *data;
	int cap;
	int len;
};

static void intlist_init(struct int_vector *l, int cap)
{
	l->data = malloc(sizeof(int[cap]));
	l->cap = cap;
	l->len = 0;
}

static void intlist_append(struct int_vector *l, int v)
{
	if (l->len >= l->cap) {
		l->cap = l->cap ? l->cap * 2 : 16;
		l->data = realloc(l->data, sizeof(int[l->cap]));
	}
	l->data[l->len++] = v;
}

static void intlist_free(struct int_vector *l)
{
	free(l->data);
	l->data = NULL;
	l->cap = 0;
	l->len = 0;
}

/// Roda o algoritmo de Brandes a partir de uma única fonte. (Cortesia do LLM.)
static void brandes_from_source(const struct graph *g, int s, double *betweenness, int *sigma,
				int *dist, double *delta, struct int_vector *pred, int *stack,
				int *queue)
{
	int n = g->n;

	/* Initialize */
	memset(sigma, 0, sizeof(int[n]));
	memset(dist, -1, sizeof(int[n]));
	for (int i = 0; i < n; i++)
		pred[i].len = 0;

	sigma[s] = 1;
	dist[s] = 0;

	int qhead = 0, qtail = 0;
	queue[qtail++] = s;
	int stack_len = 0;

	/* BFS phase */
	while (qhead < qtail) {
		int v = queue[qhead++];
		stack[stack_len++] = v;

		const int *nbors = &g->edges[g->offsets[v]];
		int deg = g->offsets[v + 1] - g->offsets[v];
		for (int i = 0; i < deg; i++) {
			int w = nbors[i];
			if (dist[w] < 0) {
				dist[w] = dist[v] + 1;
				queue[qtail++] = w;
			}
			if (dist[w] == dist[v] + 1) {
				sigma[w] += sigma[v];
				intlist_append(&pred[w], v);
			}
		}
	}

	/* Accumulation phase */
	memset(delta, 0, sizeof(double[n]));
	while (stack_len > 0) {
		int w = stack[--stack_len];
		for (int i = 0; i < pred[w].len; i++) {
			int v = pred[w].data[i];
			delta[v] += ((double)sigma[v] / (double)sigma[w]) * (1.0 + delta[w]);
		}
		if (w != s) {
			betweenness[w] += delta[w];
		}
	}
}

/// Calcula centralidade de intermediação aproximada via amostragem de `k`
/// fontes. Se `k >= n` (ou <= 0), calcula o valor exato usando todos os
/// vértices. (Cortesia do LLM.)
double *compute_betweenness_sampled(const struct graph *g, int k)
{
	int n = g->n;
	double *betweenness = calloc((size_t)n, sizeof(double));
	if (!betweenness)
		return NULL;

	if (k <= 0 || k >= n)
		k = n;

	/* Scratch arrays */
	int *sigma = malloc(sizeof(int[n]));
	int *dist = malloc(sizeof(int[n]));
	double *delta = malloc(sizeof(double[n]));
	int *stack = malloc(sizeof(int[n]));
	int *queue = malloc(sizeof(int[n]));
	struct int_vector *pred = malloc(sizeof(struct int_vector[n]));

	if (!sigma || !dist || !delta || !stack || !queue || !pred) {
		free(sigma);
		free(dist);
		free(delta);
		free(stack);
		free(queue);
		for (int i = 0; i < n && pred; i++)
			intlist_free(&pred[i]);
		free(pred);
		free(betweenness);
		return NULL;
	}

	for (int i = 0; i < n; i++)
		intlist_init(&pred[i], 4);

	int source_count = (k < n) ? k : n;
	int *sources = malloc(sizeof(int[source_count]));
	if (!sources) {
		for (int i = 0; i < n; i++)
			intlist_free(&pred[i]);
		free(sigma);
		free(dist);
		free(delta);
		free(stack);
		free(queue);
		free(pred);
		free(betweenness);
		return NULL;
	}

	/* Sample K sources (uniform, with replacement if k < n, else all) */
	if (k >= n)
		for (int i = 0; i < n; i++)
			sources[i] = i;
	else
		for (int i = 0; i < k; i++)
			sources[i] = rand() % n;

	for (int si = 0; si < source_count; si++) {
		brandes_from_source(g, sources[si], betweenness, sigma, dist, delta, pred, stack,
				    queue);
		/* Clean predecessor lists for next iteration */
		for (int i = 0; i < n; i++)
			pred[i].len = 0;
	}

	/* Normalize: divide by number of source pairs (n-1)*(n-2) approx for
	 * exact, or by source_count * (n-2) for sampled approximate */
	double norm = (k >= n) ? (double)(n - 1) * (double)(n - 2) :
				 (double)source_count * (double)(n - 2);
	if (norm > 0) {
		for (int v = 0; v < n; v++) {
			betweenness[v] /= norm;
		}
	}

	free(sources);
	for (int i = 0; i < n; i++)
		intlist_free(&pred[i]);
	free(pred);
	free(sigma);
	free(dist);
	free(delta);
	free(stack);
	free(queue);

	return betweenness;
}

double *compute_betweenness_exact(const struct graph *g)
{
	return compute_betweenness_sampled(g, g->n);
}
