/*
 * Two-group hex-z nodal diffusion (finite difference, one node per hex
 * column per axial layer) with forward and adjoint power iteration.
 *
 * Inner iterations are line-SOR: each hex column's axial line is solved
 * exactly (batched tridiagonal kernel, assembly on x86-64) while radial
 * neighbours are lagged. Columns are stored grouped by a 3-colouring of the
 * hex lattice so each colour is one contiguous batch of independent lines.
 *
 * Node arrays are laid out [z][column in solver order]. Callers address
 * columns by their grid index; rm_diff_node() does the translation.
 */
#ifndef RM_DIFFUSION_H
#define RM_DIFFUSION_H

#include "rm_hexgrid.h"

#define RM_NG 2

typedef struct {
    rm_hexgrid grid;
    int ncol, nz, nn;
    double pitch;          /* cm, flat to flat */
    double area;           /* hex area, cm2 */
    double *dz;            /* [nz] cm */
    int *perm;             /* solver slot -> grid column */
    int *iperm;            /* grid column -> solver slot */
    int *nb;               /* [ncol*6] neighbour slot or -1 */
    int color_start[4];

    /* cross sections per node (1/cm) */
    double *D[RM_NG];
    double *sr[RM_NG];     /* removal: absorption + out-scatter */
    double *nsf[RM_NG];    /* nu*Sigma_f */
    double *ksf[RM_NG];    /* kappa*Sigma_f (J/cm per fission rate) */
    double *s12, *s21;     /* group transfer */

    /* derived couplings (per unit volume) */
    double *crad[RM_NG];   /* [nn*6] */
    double *cup[RM_NG], *cdn[RM_NG], *diag[RM_NG];

    double *phi[RM_NG];    /* forward flux (arbitrary normalisation) */
    double *adj[RM_NG];    /* adjoint flux */
    double k, k_adj;

    /* scratch */
    double *ta[RM_NG], *tc[RM_NG], *td, *tx, *tw, *src;
    double *fiss;
} rm_diff;

int rm_diff_init(rm_diff *s, int rings, int nz, double pitch, const double *dz);
void rm_diff_free(rm_diff *s);
static inline int rm_diff_node(const rm_diff *s, int col, int z)
{
    return z * s->ncol + s->iperm[col];
}

/* Recompute couplings after changing D or sr. */
void rm_diff_couplings(rm_diff *s);

/* Forward power iterations; returns the eigenvalue estimate. */
double rm_diff_forward(rm_diff *s, int outers, int inners, double omega);
double rm_diff_adjoint(rm_diff *s, int outers, int inners, double omega);

/* Importance-weighted reactivity of the current state using the stored
 * forward shape and adjoint: rho = 1 - <adj, L phi> / <adj, F phi>. */
double rm_diff_reactivity(const rm_diff *s);

/* Relative residual of the forward eigenproblem (convergence check). */
double rm_diff_residual(const rm_diff *s);

/* Scale phi so that sum(kappa*Sigma_f*phi*V) = power (W). */
void rm_diff_normalise(rm_diff *s, double power);

#endif
