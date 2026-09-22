#include "rm_hexgrid.h"

#include <math.h>
#include <stdlib.h>

static const int DQ[6] = {1, 0, -1, -1, 0, 1};
static const int DR[6] = {0, 1, 1, 0, -1, -1};

static int hexdist(int q, int r)
{
    int s = -q - r;
    int a = abs(q), b = abs(r), c = abs(s);
    return a > b ? (a > c ? a : c) : (b > c ? b : c);
}

int rm_hexgrid_init(rm_hexgrid *g, int rings)
{
    g->rings = rings;
    g->n = 1 + 3 * rings * (rings + 1);
    g->q = malloc(sizeof(int) * (size_t)g->n);
    g->r = malloc(sizeof(int) * (size_t)g->n);
    g->ring = malloc(sizeof(int) * (size_t)g->n);
    g->nbr = malloc(sizeof(int) * 6 * (size_t)g->n);
    g->color = malloc(sizeof(int) * (size_t)g->n);
    if (!g->q || !g->r || !g->ring || !g->nbr || !g->color) return -1;
    int k = 0;
    g->q[0] = 0;
    g->r[0] = 0;
    g->ring[0] = 0;
    k = 1;
    for (int ring = 1; ring <= rings; ring++) {
        /* start at direction 4 times ring, walk the six sides */
        int q = DQ[4] * ring, r = DR[4] * ring;
        for (int side = 0; side < 6; side++) {
            for (int step = 0; step < ring; step++) {
                g->q[k] = q;
                g->r[k] = r;
                g->ring[k] = ring;
                k++;
                q += DQ[side];
                r += DR[side];
            }
        }
    }
    for (int c = 0; c < g->n; c++) {
        for (int d = 0; d < 6; d++)
            g->nbr[c * 6 + d] = rm_hexgrid_find(g, g->q[c] + DQ[d], g->r[c] + DR[d]);
        g->color[c] = (((g->q[c] - g->r[c]) % 3) + 3) % 3;
    }
    return 0;
}

void rm_hexgrid_free(rm_hexgrid *g)
{
    free(g->q);
    free(g->r);
    free(g->ring);
    free(g->nbr);
    free(g->color);
}

int rm_hexgrid_find(const rm_hexgrid *g, int q, int r)
{
    int ring = hexdist(q, r);
    if (ring > g->rings) return -1;
    if (ring == 0) return 0;
    /* index of the first column in this ring */
    int base = 1 + 3 * (ring - 1) * ring;
    /* walk: start (DQ[4]*ring, DR[4]*ring) = (-ring, ring)... side-by-side */
    int q0 = DQ[4] * ring, r0 = DR[4] * ring;
    for (int side = 0; side < 6; side++) {
        for (int step = 0; step < ring; step++) {
            if (q0 == q && r0 == r) return base + side * ring + step;
            q0 += DQ[side];
            r0 += DR[side];
        }
    }
    return -1;
}

void rm_hexgrid_xy(const rm_hexgrid *g, int col, double pitch, double *x, double *y)
{
    *x = pitch * (g->q[col] + 0.5 * g->r[col]);
    *y = pitch * (sqrt(3.0) / 2.0) * g->r[col];
}
