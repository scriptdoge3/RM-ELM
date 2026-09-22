/*
 * Hexagonal lattice bookkeeping (axial coordinates q, r). Column 0 is the
 * core centre; columns are numbered ring by ring outwards. Neighbour order
 * is fixed: +q, +r-q... see rm_hexgrid.c.
 */
#ifndef RM_HEXGRID_H
#define RM_HEXGRID_H

typedef struct {
    int rings;          /* rings around the centre (0 = single column) */
    int n;              /* number of columns = 1 + 3*rings*(rings+1) */
    int *q, *r;         /* axial coordinates */
    int *ring;          /* ring index of each column */
    int *nbr;           /* [n*6] neighbour column or -1 outside the grid */
    int *color;         /* 3-colouring: no two neighbours share a colour */
} rm_hexgrid;

int rm_hexgrid_init(rm_hexgrid *g, int rings);
void rm_hexgrid_free(rm_hexgrid *g);
int rm_hexgrid_find(const rm_hexgrid *g, int q, int r);
/* Cartesian centre of a column for a given flat-to-flat pitch. */
void rm_hexgrid_xy(const rm_hexgrid *g, int col, double pitch, double *x, double *y);

#endif
