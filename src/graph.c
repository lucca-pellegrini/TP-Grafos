#include "graph.h"
#include "graph_data.h"

/// Inicializa um grafo na stack a partir dos dados gerados pelo script.
struct graph graph_load(void)
{
	struct graph g;
	g.n = NUM_NODES;
	g.m = NUM_EDGES;
	g.offsets = node_offsets;
	g.edges = edges;
	g.iata = airport_iata;
	g.name = airport_name;
	g.city = airport_city;
	g.country = airport_country;
	return g;
}
