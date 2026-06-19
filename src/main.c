#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "graph.h"
#include "features.h"
#include "scoring.h"

static void print_header(const char *title)
{
	printf("┌──────────┬──────────────────────────────────────┬──────────┐\n");
	printf("│ %-8s │ %-36s │ %-8s │\n", "IATA", title, "Score");
	printf("├──────────┼──────────────────────────────────────┼──────────┤\n");
}

static void print_footer(void)
{
	printf("└──────────┴──────────────────────────────────────┴──────────┘\n");
}

static void print_row(const char *iata, const char *name, double score)
{
	char buf[37];
	int len = snprintf(buf, sizeof(buf), "%s", name);
	if (len > 36) {
		memcpy(buf + 33, "...", 3);
		buf[36] = '\0';
	}
	printf("│ %-8s │ %-36s │ %8.5f │\n", iata, buf, score);
}

static double wall_clock(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
	int k = 0; // Zero corresponde ao padrão (√n)
	int top_n = 20;
	int do_exact = 0;
	int do_compare = 0;
	const char *csv_out = NULL;
	const char *graph_dir = NULL;

	// TODO: usar getopt_long no lugar dessa atrocidade ofensiva!
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-k") == 0 && i + 1 < argc) {
			k = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
			top_n = atoi(argv[++i]);
		} else if (strcmp(argv[i], "--exact") == 0) {
			do_exact = 1;
		} else if (strcmp(argv[i], "--compare") == 0) {
			do_compare = 1;
		} else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
			csv_out = argv[++i];
		} else if ((strcmp(argv[i], "--graph") == 0 || strcmp(argv[i], "-g") == 0) &&
			   i + 1 < argc) {
			graph_dir = argv[++i];
		} else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("Usage: %s [options]\n", argv[0]);
			printf("  -k N        Samples for betweenness (default: sqrt(|V|))\n");
			printf("  -t N        Top N critical airports (default: 20)\n");
			printf("  --exact     Use exact betweenness (Brandes, O(VE))\n");
			printf("  --compare   Compare approximate vs exact + timing\n");
			printf("  -o FILE     Output all scores as CSV\n");
			printf("  -g DIR      Load graph from CSR binary directory\n");
			return 0;
		}
	}

	// NOTE: não tenho certeza se a seed aleatória faria sentido do ponto
	//       de vista de reprodutibilidade... então inventei uma seed aqui.
	srand(0xD061A5); // Entendeu? “Douglas” ⇒ “D061A5” 🤣

	printf("╔═══════════════════════════════════════════════════════════════╗\n");
	printf("║        airnet-gnn — Airline Network Resilience Analysis      ║\n");
	printf("╚═══════════════════════════════════════════════════════════════╝\n\n");

	// Instancia o grafo.
	double t0 = wall_clock();
	struct graph g = graph_dir ? graph_load_from(graph_dir) : graph_load();
	if (g.n == 0) {
		fprintf(stderr, "Failed to load graph\n");
		return 1;
	}
	double t_load = wall_clock() - t0;
	printf("Graph loaded: %d nodes, %d edges (%.4fs)\n\n", g.n, g.m, t_load);

	// Graus
	printf("── Computing degree centrality ...\n");
	t0 = wall_clock();
	double *degree = compute_degrees_raw(&g);
	if (!degree) {
		fprintf(stderr, "Out of memory\n");
		return 1;
	}
	printf("   done (%.4fs)\n\n", wall_clock() - t0);

	// Interconectividade
	printf("── Computing neighbor interconnectivity ...\n");
	t0 = wall_clock();
	double *interconnect = compute_interconnectivity(&g);
	if (!interconnect) {
		fprintf(stderr, "Out of memory\n");
		return 1;
	}
	printf("   done (%.4fs)\n\n", wall_clock() - t0);

	// Betweenness
	double *betweenness_approx = NULL;
	double *betweenness_exact = NULL;

	if (k <= 0)
		k = (int)sqrt((double)g.n);

	if (do_compare) {
		printf("── Computing betweenness (APPROXIMATE, K=%d) ...\n", k);
		t0 = wall_clock();
		betweenness_approx = compute_betweenness_sampled(&g, k);
		if (!betweenness_approx) {
			fprintf(stderr, "Out of memory\n");
			return 1;
		}
		double t_approx = wall_clock() - t0;
		printf("   done (%.4fs)\n\n", t_approx);

		printf("── Computing betweenness (EXACT, all sources) ...\n");
		t0 = wall_clock();
		betweenness_exact = compute_betweenness_exact(&g);
		if (!betweenness_exact) {
			fprintf(stderr, "Out of memory\n");
			return 1;
		}
		double t_exact = wall_clock() - t0;
		printf("   done (%.4fs)\n\n", t_exact);

		// Executa a comparação
		double total_error = 0.0;
		double max_error = 0.0;
		for (int v = 0; v < g.n; v++) {
			double err = fabs(betweenness_approx[v] - betweenness_exact[v]);
			total_error += err;
			if (err > max_error)
				max_error = err;
		}
		double mae = total_error / (double)g.n;
		printf("── Comparison:\n");
		printf("   Approximate (K=%d):  %.4fs\n", k, t_approx);
		printf("   Exact (K=%d):        %.4fs\n", g.n, t_exact);
		printf("   Speedup:             %.2fx\n", t_exact / t_approx);
		printf("   Mean Absolute Error: %g\n", mae);
		printf("   Max Absolute Error:  %g\n\n", max_error);

	} else if (do_exact) { // Usa comparação exata
		printf("── Computing betweenness (EXACT, all sources) ...\n");
		t0 = wall_clock();
		betweenness_exact = compute_betweenness_exact(&g);
		if (!betweenness_exact) {
			fprintf(stderr, "Out of memory\n");
			return 1;
		}
		printf("   done (%.4fs)\n\n", wall_clock() - t0);
	} else { // Usa comparação aproximada
		printf("── Computing betweenness (APPROXIMATE, K=%d) ...\n", k);
		t0 = wall_clock();
		betweenness_approx = compute_betweenness_sampled(&g, k);
		if (!betweenness_approx) {
			fprintf(stderr, "Out of memory\n");
			return 1;
		}
		printf("   done (%.4fs)\n\n", wall_clock() - t0);
	}

	double *btwn = betweenness_approx ? betweenness_approx : betweenness_exact;

	// Computa criticalidades
	double weights[3] = { 0.3, 0.3, 0.4 };
	printf("── Computing criticity scores (w=[%.1f, %.1f, %.1f]) ...\n", weights[0], weights[1],
	       weights[2]);
	t0 = wall_clock();
	struct node_features *features = compute_scores(&g, degree, interconnect, btwn, weights);
	if (!features) {
		fprintf(stderr, "Out of memory\n");
		return 1;
	}
	printf("   done (%.4fs)\n\n", wall_clock() - t0);

	// Exibe resultados
	printf("── Top %d critical airports:\n\n", top_n);
	int *ranking = rank_nodes(features, g.n);
	if (!ranking) {
		fprintf(stderr, "Out of memory\n");
		return 1;
	}

	print_header("Airport");
	for (int i = 0; i < top_n && i < g.n; i++) {
		int v = ranking[i];
		print_row(g.iata[v], g.name[v], features[v].score);
	}
	print_footer();
	printf("\n");

	// Salva resultado como CSV
	if (csv_out) {
		FILE *fp = fopen(csv_out, "w");
		if (!fp) {
			fprintf(stderr, "Cannot open %s for writing\n", csv_out);
		} else {
			fprintf(fp,
				"node_idx,rank,iata,name,city,country,degree,interconnectivity,betweenness,score\n");
			for (int i = 0; i < g.n; i++) {
				int v = ranking[i];
				fprintf(fp, "%d,%d,%s,\"%s\",\"%s\",\"%s\",%.6f,%.6f,%.6f,%.6f\n",
					v, i + 1, g.iata[v], g.name[v], g.city[v], g.country[v],
					features[v].degree, features[v].interconnect,
					features[v].betweenness, features[v].score);
			}
			fclose(fp);
			printf("All scores written to %s\n\n", csv_out);
		}
	}

	// Estatísticas
	{
		double avg_deg = 0;
		int max_deg = 0;
		int min_deg = g.n;
		for (int v = 0; v < g.n; v++) {
			int d = g.offsets[v + 1] - g.offsets[v];
			avg_deg += d;
			if (d > max_deg)
				max_deg = d;
			if (d < min_deg)
				min_deg = d;
		}
		avg_deg /= g.n;

		printf("── Graph statistics:\n");
		printf("   Nodes:            %d\n", g.n);
		printf("   Edges:            %d\n", g.m);
		printf("   Density:          %g\n",
		       (double)g.m / ((double)g.n * (double)(g.n - 1) / 2.0));
		printf("   Avg degree:       %.2f\n", avg_deg);
		printf("   Min degree:       %d\n", min_deg);
		printf("   Max degree:       %d\n", max_deg);
		printf("\n");
	}

	graph_free(&g);
	free(degree);
	free(interconnect);
	free(betweenness_approx);
	free(betweenness_exact);
	free(features);
	free(ranking);

	return 0;
}
