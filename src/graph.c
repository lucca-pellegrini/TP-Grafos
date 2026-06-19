#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graph.h"
#include "graph_data.h"

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
	g.dynamic = false;
	return g;
}

static char *read_line(FILE *fp)
{
	size_t cap = 256;
	size_t len = 0;
	char *buf = malloc(cap);
	if (!buf)
		return NULL;
	int c;
	while ((c = fgetc(fp)) != EOF && c != '\n') {
		if (len + 1 >= cap) {
			cap *= 2;
			char *tmp = realloc(buf, cap);
			if (!tmp) {
				free(buf);
				return NULL;
			}
			buf = tmp;
		}
		buf[len++] = (char)c;
	}
	buf[len] = '\0';
	return buf;
}

static char **parse_csv_line(const char *line, int *out_nfields)
{
	int cap = 8, nf = 0;
	char **fields = malloc(sizeof(char *) * (size_t)cap);
	if (!fields)
		return NULL;
	const char *p = line;
	while (*p) {
		if (nf >= cap) {
			cap *= 2;
			char **tmp = realloc(fields, sizeof(char *) * (size_t)cap);
			if (!tmp)
				goto fail;
			fields = tmp;
		}
		const char *start;
		size_t len;
		if (*p == '"') {
			p++;
			start = p;
			while (*p && *p != '"')
				p++;
			len = (size_t)(p - start);
			if (*p == '"')
				p++;
		} else {
			start = p;
			while (*p && *p != ',')
				p++;
			len = (size_t)(p - start);
		}
		fields[nf] = malloc(len + 1);
		if (!fields[nf])
			goto fail;
		memcpy(fields[nf], start, len);
		fields[nf][len] = '\0';
		nf++;
		if (*p == ',')
			p++;
	}
	*out_nfields = nf;
	return fields;
fail:
	for (int i = 0; i < nf; i++)
		free(fields[i]);
	free(fields);
	return NULL;
}

struct graph graph_load_from(const char *dir)
{
	struct graph g = { 0 };
	char path[4096];
	FILE *fp;

	snprintf(path, sizeof(path), "%s/graph.meta", dir);
	fp = fopen(path, "r");
	if (!fp) {
		fprintf(stderr, "Cannot open %s\n", path);
		return g;
	}

	char *line = read_line(fp);
	if (!line) {
		fclose(fp);
		return g;
	}
	g.n = atoi(line);
	free(line);

	line = read_line(fp);
	if (!line) {
		fclose(fp);
		return g;
	}
	g.m = atoi(line);
	free(line);

	free(read_line(fp));
	free(read_line(fp));
	fclose(fp);

	if (g.n <= 0 || g.m <= 0) {
		fprintf(stderr, "Invalid graph metadata in %s\n", path);
		g.n = 0;
		g.m = 0;
		return g;
	}

	snprintf(path, sizeof(path), "%s/offsets.bin", dir);
	fp = fopen(path, "rb");
	if (!fp) {
		fprintf(stderr, "Cannot open %s\n", path);
		g.n = 0;
		return g;
	}

	int *offsets = malloc(sizeof(int) * (size_t)(g.n + 1));
	if (!offsets) {
		fclose(fp);
		g.n = 0;
		return g;
	}

	if (fread(offsets, sizeof(int), (size_t)(g.n + 1), fp) != (size_t)(g.n + 1)) {
		fprintf(stderr, "Short read: %s\n", path);
		free(offsets);
		fclose(fp);
		g.n = 0;
		return g;
	}
	fclose(fp);

	snprintf(path, sizeof(path), "%s/edges.bin", dir);
	fp = fopen(path, "rb");
	if (!fp) {
		fprintf(stderr, "Cannot open %s\n", path);
		free(offsets);
		g.n = 0;
		return g;
	}

	int *edges = malloc(sizeof(int) * (size_t)g.m);
	if (!edges) {
		fclose(fp);
		free(offsets);
		g.n = 0;
		return g;
	}

	if (fread(edges, sizeof(int), (size_t)g.m, fp) != (size_t)g.m) {
		fprintf(stderr, "Short read: %s\n", path);
		free(edges);
		free(offsets);
		fclose(fp);
		g.n = 0;
		return g;
	}
	fclose(fp);

	snprintf(path, sizeof(path), "%s/nodes.csv", dir);
	fp = fopen(path, "r");
	if (!fp) {
		fprintf(stderr, "Cannot open %s\n", path);
		free(edges);
		free(offsets);
		g.n = 0;
		return g;
	}

	char (*iata)[4] = calloc((size_t)g.n, sizeof(*iata));
	char (*name)[64] = calloc((size_t)g.n, sizeof(*name));
	char (*city)[64] = calloc((size_t)g.n, sizeof(*city));
	char (*country)[64] = calloc((size_t)g.n, sizeof(*country));

	if (!iata || !name || !city || !country) {
		free(iata);
		free(name);
		free(city);
		free(country);
		free(edges);
		free(offsets);
		fclose(fp);
		g.n = 0;
		return g;
	}

	free(read_line(fp));
	for (int i = 0; i < g.n; i++) {
		char *csv_line = read_line(fp);
		if (!csv_line)
			break;
		int nf;
		char **flds = parse_csv_line(csv_line, &nf);
		if (flds && nf >= 4) {
			strncpy(iata[i], flds[0], 3);
			iata[i][3] = '\0';
			strncpy(name[i], flds[1], 63);
			name[i][63] = '\0';
			strncpy(city[i], flds[2], 63);
			city[i][63] = '\0';
			strncpy(country[i], flds[3], 63);
			country[i][63] = '\0';
			for (int j = 0; j < nf; j++)
				free(flds[j]);
			free(flds);
		}
		free(csv_line);
	}
	fclose(fp);

	g.offsets = offsets;
	g.edges = edges;
	g.iata = iata;
	g.name = name;
	g.city = city;
	g.country = country;
	g.dynamic = true;
	return g;
}

void graph_free(struct graph *g)
{
	if (g->dynamic) {
		free((void *)g->offsets);
		free((void *)g->edges);
		free((void *)g->iata);
		free((void *)g->name);
		free((void *)g->city);
		free((void *)g->country);
		g->offsets = NULL;
		g->edges = NULL;
		g->iata = NULL;
		g->name = NULL;
		g->city = NULL;
		g->country = NULL;
		g->n = g->m = 0;
		g->dynamic = false;
	}
}
