#include "rm_xs.h"
#include "rm_xsdata.h"

#include <math.h>
#include <string.h>

/* Piecewise-linear interpolation of a branch table, extrapolating from the
 * end segments. xs are the (transformed) branch coordinates, ascending. */
static double interp(const double *xs, const double *ys, int n, double x)
{
    int i = 0;
    if (x <= xs[0]) i = 0;
    else if (x >= xs[n - 1]) i = n - 2;
    else
        while (i < n - 2 && x > xs[i + 1]) i++;
    double t = (x - xs[i]) / (xs[i + 1] - xs[i]);
    return ys[i] + t * (ys[i + 1] - ys[i]);
}

static void add_branch(const rm_xs_branch *b, double x, rm_xs2 *o)
{
    double *dst = (double *)o;
    for (int k = 0; k < RM_XS_NVALS; k++) {
        double ys[RM_XS_MAXPTS];
        for (int p = 0; p < b->n; p++) ys[p] = b->delta[p][k];
        dst[k] += interp(b->x, ys, b->n, x);
    }
}

void rm_xs_fuel(const rm_xs_state *st, rm_xs2 *out)
{
    memcpy(out, &rm_xs_fuel_ref, sizeof *out);
    double Tf = st->Tf < 250.0 ? 250.0 : st->Tf;
    add_branch(&rm_xs_br_tf, sqrt(Tf), out);
    double na = st->na_rel < 0.0 ? 0.0 : st->na_rel;
    add_branch(&rm_xs_br_na, na, out);
    add_branch(&rm_xs_br_tg, st->Tg, out);
    add_branch(&rm_xs_br_tz, st->Tz, out);
    add_branch(&rm_xs_br_x, st->zrh_x, out);
    for (int g = 0; g < 2; g++) {
        if (out->sa[g] < 1e-8) out->sa[g] = 1e-8;
        if (out->nsf[g] < 0) out->nsf[g] = 0;
        if (out->sf[g] < 0) out->sf[g] = 0;
        if (out->ksf[g] < 0) out->ksf[g] = 0;
    }
    if (out->s12 < 0) out->s12 = 0;
    if (out->s21 < 0) out->s21 = 0;
}

const rm_xs2 *rm_xs_fixed(int type)
{
    return &rm_xs_fixed_tab[type];
}

const rm_micro2 *rm_xs_micro(int nuc)
{
    return &rm_xs_micro_tab[nuc];
}
