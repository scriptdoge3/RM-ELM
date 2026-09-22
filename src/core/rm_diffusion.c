#include "rm_diffusion.h"
#include "rm_kernels.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static double *zalloc(size_t n)
{
    return calloc(n ? n : 1, sizeof(double));
}

int rm_diff_init(rm_diff *s, int rings, int nz, double pitch, const double *dz)
{
    memset(s, 0, sizeof *s);
    if (rm_hexgrid_init(&s->grid, rings) != 0) return -1;
    s->ncol = s->grid.n;
    s->nz = nz;
    s->nn = s->ncol * nz;
    s->pitch = pitch;
    s->area = sqrt(3.0) / 2.0 * pitch * pitch;
    s->dz = zalloc((size_t)nz);
    memcpy(s->dz, dz, sizeof(double) * (size_t)nz);

    /* order columns by colour so each colour is one contiguous batch */
    s->perm = malloc(sizeof(int) * (size_t)s->ncol);
    s->iperm = malloc(sizeof(int) * (size_t)s->ncol);
    int slot = 0;
    for (int c = 0; c < 3; c++) {
        s->color_start[c] = slot;
        for (int col = 0; col < s->ncol; col++)
            if (s->grid.color[col] == c) {
                s->perm[slot] = col;
                s->iperm[col] = slot;
                slot++;
            }
    }
    s->color_start[3] = slot;
    s->nb = malloc(sizeof(int) * 6 * (size_t)s->ncol);
    for (int i = 0; i < s->ncol; i++) {
        int col = s->perm[i];
        for (int d = 0; d < 6; d++) {
            int nc = s->grid.nbr[col * 6 + d];
            s->nb[i * 6 + d] = nc < 0 ? -1 : s->iperm[nc];
        }
    }
    size_t nn = (size_t)s->nn;
    for (int g = 0; g < RM_NG; g++) {
        s->D[g] = zalloc(nn);
        s->sr[g] = zalloc(nn);
        s->nsf[g] = zalloc(nn);
        s->ksf[g] = zalloc(nn);
        s->crad[g] = zalloc(nn * 6);
        s->cup[g] = zalloc(nn);
        s->cdn[g] = zalloc(nn);
        s->diag[g] = zalloc(nn);
        s->phi[g] = zalloc(nn);
        s->adj[g] = zalloc(nn);
        s->ta[g] = zalloc(nn);
        s->tc[g] = zalloc(nn);
        for (size_t i = 0; i < nn; i++) {
            s->phi[g][i] = 1.0;
            s->adj[g][i] = 1.0;
        }
    }
    s->s12 = zalloc(nn);
    s->s21 = zalloc(nn);
    s->td = zalloc(nn);
    s->tx = zalloc(nn);
    s->tw = zalloc(nn);
    s->src = zalloc(nn);
    s->fiss = zalloc(nn);
    s->k = 1.0;
    s->k_adj = 1.0;
    return 0;
}

void rm_diff_free(rm_diff *s)
{
    rm_hexgrid_free(&s->grid);
    free(s->dz);
    free(s->perm);
    free(s->iperm);
    free(s->nb);
    for (int g = 0; g < RM_NG; g++) {
        free(s->D[g]); free(s->sr[g]); free(s->nsf[g]); free(s->ksf[g]);
        free(s->crad[g]); free(s->cup[g]); free(s->cdn[g]); free(s->diag[g]);
        free(s->phi[g]); free(s->adj[g]); free(s->ta[g]); free(s->tc[g]);
    }
    free(s->s12); free(s->s21); free(s->td); free(s->tx); free(s->tw);
    free(s->src); free(s->fiss);
}

void rm_diff_couplings(rm_diff *s)
{
    const double p = s->pitch;
    const double kr = 2.0 / (3.0 * p * p);    /* face/volume/distance for hexes */
    const double sv = 2.0 / (3.0 * p);        /* face area / volume */
    const int nc = s->ncol;
    for (int g = 0; g < RM_NG; g++) {
        const double *D = s->D[g];
        for (int z = 0; z < s->nz; z++) {
            const double dz = s->dz[z];
            for (int i = 0; i < nc; i++) {
                int n = z * nc + i;
                double Di = D[n];
                double diag = s->sr[g][n];
                for (int d = 0; d < 6; d++) {
                    int j = s->nb[i * 6 + d];
                    double c;
                    if (j >= 0) {
                        double Dj = D[z * nc + j];
                        c = kr * 2.0 * Di * Dj / (Di + Dj);
                        s->crad[g][n * 6 + d] = c;
                    } else {
                        c = sv / (p / (2.0 * Di) + 2.0);
                        s->crad[g][n * 6 + d] = 0.0;
                    }
                    diag += c;
                }
                /* axial */
                double up, dn;
                if (z + 1 < s->nz) {
                    int m = n + nc;
                    up = 1.0 / (dz * (dz / (2.0 * Di) + s->dz[z + 1] / (2.0 * D[m])));
                    s->cup[g][n] = up;
                } else {
                    up = 1.0 / (dz * (dz / (2.0 * Di) + 2.0));
                    s->cup[g][n] = 0.0;
                }
                if (z > 0) {
                    int m = n - nc;
                    dn = 1.0 / (dz * (dz / (2.0 * Di) + s->dz[z - 1] / (2.0 * D[m])));
                    s->cdn[g][n] = dn;
                } else {
                    dn = 1.0 / (dz * (dz / (2.0 * Di) + 2.0));
                    s->cdn[g][n] = 0.0;
                }
                diag += up + dn;
                s->diag[g][n] = diag;
                s->ta[g][n] = -s->cdn[g][n];
                s->tc[g][n] = -s->cup[g][n];
            }
        }
    }
}

/* One line-SOR sweep of group g with volumetric source src. */
static void sweep(rm_diff *s, int g, double *phi, double omega)
{
    const int nc = s->ncol;
    for (int c = 0; c < 3; c++) {
        int a = s->color_start[c], b = s->color_start[c + 1];
        for (int z = 0; z < s->nz; z++) {
            for (int i = a; i < b; i++) {
                int n = z * nc + i;
                double r = s->src[n];
                const double *cr = s->crad[g] + (size_t)n * 6;
                const int *nb = s->nb + i * 6;
                for (int d = 0; d < 6; d++)
                    if (nb[d] >= 0) r += cr[d] * phi[z * nc + nb[d]];
                s->td[n] = r;
            }
        }
        rm_tridiag_batch((size_t)(b - a), (size_t)s->nz, (size_t)nc,
                         s->ta[g] + a, s->diag[g] + a, s->tc[g] + a,
                         s->td + a, s->tx + a, s->tw + a);
        for (int z = 0; z < s->nz; z++)
            for (int i = a; i < b; i++) {
                int n = z * nc + i;
                phi[n] += omega * (s->tx[n] - phi[n]);
            }
    }
}

static double vol(const rm_diff *s, int n)
{
    return s->area * s->dz[n / s->ncol];
}

double rm_diff_forward(rm_diff *s, int outers, int inners, double omega)
{
    const int nn = s->nn;
    double fold = 0.0;
    for (int n = 0; n < nn; n++) {
        s->fiss[n] = s->nsf[0][n] * s->phi[0][n] + s->nsf[1][n] * s->phi[1][n];
        fold += s->fiss[n] * vol(s, n);
    }
    for (int it = 0; it < outers; it++) {
        for (int n = 0; n < nn; n++) s->src[n] = s->fiss[n] / s->k + s->s21[n] * s->phi[1][n];
        for (int k = 0; k < inners; k++) sweep(s, 0, s->phi[0], omega);
        for (int n = 0; n < nn; n++) s->src[n] = s->s12[n] * s->phi[0][n];
        for (int k = 0; k < inners; k++) sweep(s, 1, s->phi[1], omega);
        double fnew = 0.0;
        for (int n = 0; n < nn; n++) {
            s->fiss[n] = s->nsf[0][n] * s->phi[0][n] + s->nsf[1][n] * s->phi[1][n];
            fnew += s->fiss[n] * vol(s, n);
        }
        s->k *= fnew / fold;
        /* keep the flux magnitude bounded */
        double scale = 1.0 / fnew;
        for (int g = 0; g < RM_NG; g++)
            for (int n = 0; n < nn; n++) s->phi[g][n] *= scale;
        for (int n = 0; n < nn; n++) s->fiss[n] *= scale;
        fold = 1.0;
    }
    return s->k;
}

double rm_diff_adjoint(rm_diff *s, int outers, int inners, double omega)
{
    const int nn = s->nn;
    /* adjoint fission source f* = chi . adj = adj[0] (chi1 = 1) */
    double fold = 0.0;
    for (int n = 0; n < nn; n++)
        fold += (s->nsf[0][n] + s->nsf[1][n]) * s->adj[0][n] * vol(s, n);
    for (int it = 0; it < outers; it++) {
        for (int n = 0; n < nn; n++)
            s->src[n] = s->nsf[1][n] * s->adj[0][n] / s->k_adj + s->s21[n] * s->adj[0][n];
        for (int k = 0; k < inners; k++) sweep(s, 1, s->adj[1], omega);
        for (int n = 0; n < nn; n++)
            s->src[n] = s->nsf[0][n] * s->adj[0][n] / s->k_adj + s->s12[n] * s->adj[1][n];
        for (int k = 0; k < inners; k++) sweep(s, 0, s->adj[0], omega);
        double fnew = 0.0;
        for (int n = 0; n < nn; n++)
            fnew += (s->nsf[0][n] + s->nsf[1][n]) * s->adj[0][n] * vol(s, n);
        s->k_adj *= fnew / fold;
        double scale = 1.0 / fnew;
        for (int g = 0; g < RM_NG; g++)
            for (int n = 0; n < nn; n++) s->adj[g][n] *= scale;
        fold = 1.0;
    }
    return s->k_adj;
}

/* (L_g phi)_n = diag*phi - sum crad*phi_nb - cup*phi_up - cdn*phi_dn */
static double apply_L(const rm_diff *s, int g, const double *phi, int n)
{
    const int nc = s->ncol;
    int i = n % nc, z = n / nc;
    double r = s->diag[g][n] * phi[n];
    const double *cr = s->crad[g] + (size_t)n * 6;
    const int *nb = s->nb + i * 6;
    for (int d = 0; d < 6; d++)
        if (nb[d] >= 0) r -= cr[d] * phi[z * nc + nb[d]];
    if (z + 1 < s->nz) r -= s->cup[g][n] * phi[n + nc];
    if (z > 0) r -= s->cdn[g][n] * phi[n - nc];
    return r;
}

double rm_diff_reactivity(const rm_diff *s)
{
    double num = 0.0, den = 0.0;
    for (int n = 0; n < s->nn; n++) {
        double v = vol(s, n);
        double L1 = apply_L(s, 0, s->phi[0], n) - s->s21[n] * s->phi[1][n];
        double L2 = apply_L(s, 1, s->phi[1], n) - s->s12[n] * s->phi[0][n];
        double F = s->nsf[0][n] * s->phi[0][n] + s->nsf[1][n] * s->phi[1][n];
        num += v * (s->adj[0][n] * L1 + s->adj[1][n] * L2);
        den += v * s->adj[0][n] * F;
    }
    return 1.0 - num / den;
}

double rm_diff_residual(const rm_diff *s)
{
    double r2 = 0.0, f2 = 0.0;
    for (int n = 0; n < s->nn; n++) {
        double F = s->nsf[0][n] * s->phi[0][n] + s->nsf[1][n] * s->phi[1][n];
        double L1 = apply_L(s, 0, s->phi[0], n) - s->s21[n] * s->phi[1][n] - F / s->k;
        double L2 = apply_L(s, 1, s->phi[1], n) - s->s12[n] * s->phi[0][n];
        r2 += L1 * L1 + L2 * L2;
        f2 += F * F / (s->k * s->k);
    }
    return sqrt(r2 / (f2 > 0 ? f2 : 1.0));
}

void rm_diff_normalise(rm_diff *s, double power)
{
    double p = 0.0;
    for (int n = 0; n < s->nn; n++)
        p += (s->ksf[0][n] * s->phi[0][n] + s->ksf[1][n] * s->phi[1][n]) * vol(s, n);
    if (p <= 0.0) return;
    double f = power / p;
    for (int g = 0; g < RM_NG; g++)
        for (int n = 0; n < s->nn; n++) s->phi[g][n] *= f;
}
