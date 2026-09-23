#include "rm_core.h"
#include "rm_materials.h"
#include "rm_sodium.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846
#define KAPPA_J 3.10e-11                 /* J recoverable per fission */
#define FRAC_FUEL 0.935

/* decay heat: fission products after long operation, fitted to ANS-5.1-type data */
static const double DH_A[RM_NDH] = {
    1.170712e-03, 9.628229e-04, 2.123659e-03, 3.299984e-03, 4.373504e-03, 6.870269e-03,
    9.439654e-03, 1.032776e-02, 1.326452e-02, 1.322303e-02, 3.775172e-03};
static const double DH_L[RM_NDH] = {
    3.162278e-08, 1.873817e-07, 1.110336e-06, 6.579332e-06, 3.898604e-05, 2.310130e-04,
    1.368875e-03, 8.111308e-03, 4.806381e-02, 2.848036e-01, 1.000000e+01};

/* isotope chains */
#define L_I135 2.926e-5
#define L_XE135 2.107e-5
#define L_PM149 3.627e-6
#define L_PA233 2.974e-7
#define Y_I135 0.0629
#define Y_XE135 0.0025
#define Y_PM149 0.0108

static double *dal(size_t n) { return calloc(n ? n : 1, sizeof(double)); }

static double dh_total(void)
{
    double s = 0;
    for (int i = 0; i < RM_NDH; i++) s += DH_A[i];
    return s;
}

static void channel_geometry(rm_channel_geom *g)
{
    memset(g, 0, sizeof *g);
    g->n_fuel_pins = 150;
    g->n_zrh_pins = 19;
    g->pellet_r = 0.0033;
    g->clad_ir = 0.0035;
    g->clad_or = 0.00395;
    g->zrh_r = 0.0035;
    g->pin_pitch = 0.00948;
    g->flow_area = 50.718e-4;
    g->bypass_area = 22.141e-4;
    g->dh_bundle = 4.386e-3;
    g->dh_bypass = 6.0e-3;
    g->duct_perim_in = 0.4323;
    g->duct_perim_out = 0.4531;
    g->duct_area = 13.281e-4;
    g->graphite_area = 41.219e-4;
    g->graphite_perim = 1.018;
    g->graphite_gap_h = 600.0;
    g->frac_fuel = FRAC_FUEL;
    g->frac_zrh = 0.012;
    g->frac_graphite = 0.030;
    g->frac_coolant = 0.008;
    g->frac_duct = 0.015;
}

int rm_core_init(rm_core *c)
{
    memset(c, 0, sizeof *c);
    double dz[RM_NZ];
    for (int z = 0; z < RM_NZ; z++)
        dz[z] = (z < RM_NZ_BOT || z >= RM_NZ_BOT + RM_NZ_ACT) ? 15.0 : RM_ACTIVE_H / RM_NZ_ACT;
    if (rm_diff_init(&c->dif, RM_CORE_RINGS + RM_REFL_RINGS, RM_NZ, RM_PITCH, dz) != 0) return -1;
    rm_hexgrid *g = &c->dif.grid;
    c->ncol = g->n;
    c->coltype = malloc(sizeof(int) * (size_t)c->ncol);
    c->chan_of_col = malloc(sizeof(int) * (size_t)c->ncol);
    c->ctrl_of_col = malloc(sizeof(int) * (size_t)c->ncol);
    c->col_of_chan = malloc(sizeof(int) * (size_t)c->ncol);
    c->ctrl_col = malloc(sizeof(int) * (size_t)c->ncol);
    c->ctrl_bank = malloc(sizeof(int) * (size_t)c->ncol);
    for (int col = 0; col < c->ncol; col++) {
        int q = g->q[col], r = g->r[col], ring = g->ring[col];
        c->chan_of_col[col] = -1;
        c->ctrl_of_col[col] = -1;
        if (ring > RM_CORE_RINGS) {
            c->coltype[col] = COL_REFL;
        } else if ((((q + 3 * r) % 7) + 7) % 7 == 0) {
            /* index-7 sub-lattice: every fuel channel touches at most one rod */
            c->coltype[col] = COL_CTRL;
            int k = c->nctrl++;
            c->ctrl_col[k] = col;
            c->ctrl_of_col[col] = k;
            c->ctrl_bank[k] = -1;
        } else {
            c->coltype[col] = COL_FUEL;
            int k = c->nchan++;
            c->col_of_chan[k] = col;
            c->chan_of_col[col] = k;
        }
    }
    /* banks: rods at equal radius form 6-fold symmetric shells; deal the
     * shells out to the banks from the centre outwards */
    {
        static const int cycle[] = {BANK_SHIM_A, BANK_SAFETY, BANK_REG, BANK_SHIM_B, BANK_SHIM_C,
                                    BANK_SAFETY, BANK_SHIM_D, BANK_SHIM_A, BANK_SAFETY, BANK_SHIM_B,
                                    BANK_SHIM_C, BANK_SHIM_D, BANK_REG, BANK_SAFETY};
        int shell = 0;
        for (;;) {
            double rmin = 1e30;
            for (int k = 0; k < c->nctrl; k++) {
                if (c->ctrl_bank[k] >= 0) continue;
                double x, y;
                rm_hexgrid_xy(g, c->ctrl_col[k], 1.0, &x, &y);
                double rr = x * x + y * y;
                if (rr < rmin) rmin = rr;
            }
            if (rmin > 1e29) break;
            int bank = cycle[shell % (int)(sizeof cycle / sizeof cycle[0])];
            for (int k = 0; k < c->nctrl; k++) {
                if (c->ctrl_bank[k] >= 0) continue;
                double x, y;
                rm_hexgrid_xy(g, c->ctrl_col[k], 1.0, &x, &y);
                if (fabs(x * x + y * y - rmin) < 1e-6) c->ctrl_bank[k] = bank;
            }
            shell++;
        }
    }
    c->rod_ins = dal((size_t)c->nctrl);
    c->rod_vel = dal((size_t)c->nctrl);
    c->rod_target = dal((size_t)c->nctrl);
    c->bank_speed[BANK_REG] = 1.5;
    for (int b = BANK_SHIM_A; b <= BANK_SHIM_D; b++) c->bank_speed[b] = 0.5;
    c->bank_speed[BANK_SAFETY] = 2.0;

    rm_channel_geom geo;
    channel_geometry(&geo);
    rm_coreth_init(&c->th, c->nchan, RM_NZ_ACT, RM_ACTIVE_H / 100.0, &geo);
    rm_coreth_isothermal(&c->th, 653.0);

    size_t nf = (size_t)c->nchan * RM_NZ_ACT;
    for (int i = 0; i < RM_NISO; i++) c->iso[i] = dal(nf);
    c->zrh_x = dal(nf);
    for (size_t n = 0; n < nf; n++) {
        c->iso[ISO_U235][n] = rm_xs_ref_density[RM_NUC_U235];
        c->zrh_x[n] = RM_ZRH_X0;
    }
    /* radially zoned burnable absorber: most in the centre to flatten power */
    for (int ch = 0; ch < c->nchan; ch++) {
        double x, y;
        rm_hexgrid_xy(g, c->col_of_chan[ch], 1.0, &x, &y);
        double r = sqrt(x * x + y * y) / (RM_CORE_RINGS + 0.5);
        if (r > 1) r = 1;
        double nb = RM_BP_CENTRE + (RM_BP_EDGE - RM_BP_CENTRE) * r * r;
        for (int k = 0; k < RM_NZ_ACT; k++) c->iso[ISO_B10][core_fnode(c, ch, k)] = nb;
    }
    c->node_power = dal((size_t)c->dif.nn);
    c->phi_abs[0] = dal(nf);
    c->phi_abs[1] = dal(nf);

    for (int i = 0; i < 6; i++) {
        c->pk.beta[i] = rm_xs_beta[i];
        c->pk.lambda[i] = rm_xs_lambda[i];
    }
    c->pk.Lambda = rm_xs_Lambda;
    c->pks.n = 1e-9;
    for (int i = 0; i < 6; i++) c->pks.y[i] = c->pk.beta[i] * c->pks.n / c->pk.lambda[i];
    /* startup source: ~1e-9 of rated power at 5% subcritical (n = -S Lambda / rho) */
    c->src = 5e-5;
    c->rho_doppler_coef = -2.5e-5;
    /* fuel heat capacity */
    double vol_fuel = c->nchan * geo.n_fuel_pins * PI * geo.pellet_r * geo.pellet_r * RM_ACTIVE_H / 100.0;
    c->c_fuel = vol_fuel * rm_fuel_rhocp(900.0);
    /* all rods fully inserted to start with */
    for (int k = 0; k < c->nctrl; k++) c->rod_ins[k] = c->rod_target[k] = RM_ACTIVE_H;
    return 0;
}

void rm_core_free(rm_core *c)
{
    rm_diff_free(&c->dif);
    rm_coreth_free(&c->th);
    free(c->coltype); free(c->chan_of_col); free(c->ctrl_of_col); free(c->col_of_chan);
    free(c->ctrl_col); free(c->ctrl_bank); free(c->rod_ins); free(c->rod_vel); free(c->rod_target);
    for (int i = 0; i < RM_NISO; i++) free(c->iso[i]);
    free(c->zrh_x); free(c->node_power); free(c->phi_abs[0]); free(c->phi_abs[1]);
}

/* depth range (cm below the core top) covered by axial node z */
static void node_depth(int z, double *top, double *bot)
{
    if (z < RM_NZ_BOT) {
        *top = RM_ACTIVE_H + 15.0 * (RM_NZ_BOT - 1 - z);
        *bot = *top + 15.0;
    } else if (z < RM_NZ_BOT + RM_NZ_ACT) {
        int k = z - RM_NZ_BOT;
        double h = RM_ACTIVE_H / RM_NZ_ACT;
        *bot = RM_ACTIVE_H - h * k;
        *top = *bot - h;
    } else {
        int k = z - RM_NZ_BOT - RM_NZ_ACT;
        *bot = -15.0 * k;
        *top = *bot - 15.0;
    }
}

static double overlap(double a0, double a1, double b0, double b1)
{
    double lo = a0 > b0 ? a0 : b0, hi = a1 < b1 ? a1 : b1;
    return hi > lo ? hi - lo : 0.0;
}

static void set_node(rm_diff *d, int n, const rm_xs2 *x)
{
    for (int g = 0; g < 2; g++) {
        d->D[g][n] = x->D[g];
        d->nsf[g][n] = x->nsf[g];
        d->ksf[g][n] = x->ksf[g];
    }
    d->sr[0][n] = x->sa[0] + x->s12;
    d->sr[1][n] = x->sa[1] + x->s21;
    d->s12[n] = x->s12;
    d->s21[n] = x->s21;
}

static void mix(rm_xs2 *out, const rm_xs2 *const *parts, const double *f, int np)
{
    memset(out, 0, sizeof *out);
    double invD[2] = {0, 0};
    for (int i = 0; i < np; i++) {
        const rm_xs2 *p = parts[i];
        for (int g = 0; g < 2; g++) {
            invD[g] += f[i] / p->D[g];
            out->sa[g] += f[i] * p->sa[g];
            out->sf[g] += f[i] * p->sf[g];
            out->nsf[g] += f[i] * p->nsf[g];
            out->ksf[g] += f[i] * p->ksf[g];
        }
        out->s12 += f[i] * p->s12;
        out->s21 += f[i] * p->s21;
    }
    for (int g = 0; g < 2; g++) out->D[g] = 1.0 / invD[g];
}

static double fuel_T_eff(const rm_coreth *th, int n)
{
    return (th->T[TH_F1][n] + 3.0 * th->T[TH_F2][n] + 5.0 * th->T[TH_F3][n]) / 9.0;
}

static void update_xs_offset(rm_core *c, double dTf)
{
    rm_diff *d = &c->dif;
    const rm_coreth *th = &c->th;
    for (int col = 0; col < c->ncol; col++) {
        int slot = d->iperm[col];
        for (int z = 0; z < RM_NZ; z++) {
            int n = z * d->ncol + slot;
            int type = c->coltype[col];
            rm_xs2 x;
            if (type == COL_REFL) {
                set_node(d, n, rm_xs_fixed(RM_XS_REFL));
            } else if (type == COL_FUEL) {
                if (z < RM_NZ_BOT) {
                    set_node(d, n, rm_xs_fixed(RM_XS_AX_LOW));
                } else if (z >= RM_NZ_BOT + RM_NZ_ACT) {
                    set_node(d, n, rm_xs_fixed(RM_XS_AX_UP));
                } else {
                    int ch = c->chan_of_col[col];
                    int fn = core_fnode(c, ch, z - RM_NZ_BOT);
                    rm_xs_state st;
                    st.Tf = fuel_T_eff(th, fn) + dTf;
                    double Tc = th->T[TH_C][fn];
                    if (Tc < RM_NA_TMELT) Tc = RM_NA_TMELT;
                    st.na_rel = rm_na_rho(Tc) * (1.0 - th->void_frac[fn]) / rm_xs_na_ref_rho;
                    st.Tg = th->T[TH_G][fn];
                    st.Tz = th->T_zrh[fn];
                    st.zrh_x = c->zrh_x[fn];
                    rm_xs_fuel(&st, &x);
                    /* isotopic corrections relative to the fresh reference */
                    static const int nuc_of_iso[RM_NISO] = {-1, RM_NUC_XE135, -1, RM_NUC_SM149,
                                                            RM_NUC_PA233, RM_NUC_U233, RM_NUC_U235,
                                                            RM_NUC_B10};
                    for (int i = 0; i < RM_NISO; i++) {
                        int nu = nuc_of_iso[i];
                        if (nu < 0) continue;
                        double dN = c->iso[i][fn] - rm_xs_ref_density[nu];
                        if (dN == 0.0) continue;
                        const rm_micro2 *m = rm_xs_micro(nu);
                        for (int g = 0; g < 2; g++) {
                            x.sa[g] += dN * (m->f[g] + m->c[g]);
                            x.sf[g] += dN * m->f[g];
                            x.nsf[g] += dN * m->nf[g];
                            x.ksf[g] += dN * m->f[g] * KAPPA_J;
                        }
                    }
                    set_node(d, n, &x);
                }
            } else {
                int k = c->ctrl_of_col[col];
                double ins = c->rod_ins[k];
                double top, bot;
                node_depth(z, &top, &bot);
                double h = bot - top;
                double fa = overlap(top, bot, ins - RM_ACTIVE_H, ins) / h;
                double fd = overlap(top, bot, ins, ins + RM_ACTIVE_H) / h;
                double fn_ = 1.0 - fa - fd;
                if (fn_ < 0) fn_ = 0;
                const rm_xs2 *parts[3] = {rm_xs_fixed(RM_XS_CR_ABS), rm_xs_fixed(RM_XS_CR_DISP),
                                          rm_xs_fixed(RM_XS_CR_NA)};
                double f[3] = {fa, fd, fn_};
                mix(&x, parts, f, 3);
                set_node(d, n, &x);
            }
        }
    }
    rm_diff_couplings(d);
}

void rm_core_update_xs(rm_core *c)
{
    update_xs_offset(c, 0.0);
}

/* distribute power over nodes following kappa-Sigma_f * phi */
static void distribute_power(rm_core *c, double p_thermal, double p_fission)
{
    rm_diff *d = &c->dif;
    double tot = 0.0;
    for (int n = 0; n < d->nn; n++) {
        double v = d->area * d->dz[n / d->ncol];
        double p = (d->ksf[0][n] * d->phi[0][n] + d->ksf[1][n] * d->phi[1][n]) * v;
        c->node_power[n] = p;
        tot += p;
    }
    double f = tot > 0 ? p_thermal / tot : 0.0;
    double ff = tot > 0 ? p_fission / tot : 0.0;
    double mean = p_thermal / (c->nchan * RM_NZ_ACT), peak = 0.0;
    for (int n = 0; n < d->nn; n++) c->node_power[n] *= f;
    for (int ch = 0; ch < c->nchan; ch++) {
        int slot = d->iperm[c->col_of_chan[ch]];
        for (int k = 0; k < RM_NZ_ACT; k++) {
            int n = (k + RM_NZ_BOT) * d->ncol + slot;
            int fn = core_fnode(c, ch, k);
            c->th.q_node[fn] = c->node_power[n];
            if (c->node_power[n] > peak) peak = c->node_power[n];
            c->phi_abs[0][fn] = d->phi[0][n] * ff;
            c->phi_abs[1][fn] = d->phi[1][n] * ff;
        }
    }
    c->peak_factor = mean > 0 ? peak / mean : 0.0;
}

static void update_isotopes(rm_core *c, double dt)
{
    const rm_micro2 *mx = rm_xs_micro(RM_NUC_XE135), *ms = rm_xs_micro(RM_NUC_SM149);
    const rm_micro2 *mpa = rm_xs_micro(RM_NUC_PA233), *mu3 = rm_xs_micro(RM_NUC_U233);
    const rm_micro2 *mu5 = rm_xs_micro(RM_NUC_U235), *mth = rm_xs_micro(RM_NUC_TH232);
    const double nth = rm_xs_ref_density[RM_NUC_TH232];
    size_t nf = (size_t)c->nchan * RM_NZ_ACT;
    for (size_t n = 0; n < nf; n++) {
        double p1 = c->phi_abs[0][n] * 1e-24, p2 = c->phi_abs[1][n] * 1e-24;
        double *I = &c->iso[ISO_I135][n], *X = &c->iso[ISO_XE135][n];
        double *Pm = &c->iso[ISO_PM149][n], *S = &c->iso[ISO_SM149][n];
        double *Pa = &c->iso[ISO_PA233][n], *U3 = &c->iso[ISO_U233][n], *U5 = &c->iso[ISO_U235][n];
        /* fission rate density in atoms/b-cm/s units */
        double F = *U5 * (mu5->f[0] * p1 + mu5->f[1] * p2) + *U3 * (mu3->f[0] * p1 + mu3->f[1] * p2)
                 + nth * (mth->f[0] * p1);
        double sx = mx->c[0] * p1 + mx->c[1] * p2;
        double ss = ms->c[0] * p1 + ms->c[1] * p2;
        double spa = mpa->c[0] * p1 + mpa->c[1] * p2;
        double su3 = (mu3->f[0] + mu3->c[0]) * p1 + (mu3->f[1] + mu3->c[1]) * p2;
        double su5 = (mu5->f[0] + mu5->c[0]) * p1 + (mu5->f[1] + mu5->c[1]) * p2;
        double cth = mth->c[0] * p1 + mth->c[1] * p2;
        /* exponential integrators, sources held over the step */
        double e;
        double I0 = *I;
        e = exp(-L_I135 * dt);
        *I = I0 * e + Y_I135 * F / L_I135 * (1 - e);
        double lx = L_XE135 + sx;
        e = exp(-lx * dt);
        *X = *X * e + (Y_XE135 * F + L_I135 * 0.5 * (I0 + *I)) / lx * (1 - e);
        double Pm0 = *Pm;
        e = exp(-L_PM149 * dt);
        *Pm = Pm0 * e + Y_PM149 * F / L_PM149 * (1 - e);
        if (ss > 0) {
            e = exp(-ss * dt);
            *S = *S * e + L_PM149 * 0.5 * (Pm0 + *Pm) / ss * (1 - e);
        } else {
            *S += L_PM149 * 0.5 * (Pm0 + *Pm) * dt;
        }
        double Pa0 = *Pa;
        double lpa = L_PA233 + spa;
        e = exp(-lpa * dt);
        *Pa = Pa0 * e + nth * cth / lpa * (1 - e);
        if (su3 > 0) {
            e = exp(-su3 * dt);
            *U3 = *U3 * e + L_PA233 * 0.5 * (Pa0 + *Pa) / su3 * (1 - e);
        } else {
            *U3 += L_PA233 * 0.5 * (Pa0 + *Pa) * dt;
        }
        *U5 *= exp(-su5 * dt);
        const rm_micro2 *mb = rm_xs_micro(RM_NUC_B10);
        c->iso[ISO_B10][n] *= exp(-(mb->c[0] * p1 + mb->c[1] * p2) * dt);
    }
}

static void move_rods(rm_core *c, double dt)
{
    for (int k = 0; k < c->nctrl; k++) {
        if (c->scram) {
            /* gravity plus spring, sodium dashpot limits to ~1.4 m/s */
            c->rod_vel[k] += 9.0 * 100.0 * dt;
            if (c->rod_vel[k] > 140.0) c->rod_vel[k] = 140.0;
            c->rod_ins[k] += c->rod_vel[k] * dt;
            if (c->rod_ins[k] >= RM_ACTIVE_H) {
                c->rod_ins[k] = RM_ACTIVE_H;
                c->rod_vel[k] = 0.0;
            }
            c->rod_target[k] = RM_ACTIVE_H;
            continue;
        }
        double v = c->bank_speed[c->ctrl_bank[k]];
        double diff = c->rod_target[k] - c->rod_ins[k];
        double step = v * dt;
        if (fabs(diff) <= step) {
            c->rod_ins[k] = c->rod_target[k];
            c->rod_vel[k] = 0.0;
        } else {
            c->rod_ins[k] += diff > 0 ? step : -step;
            c->rod_vel[k] = diff > 0 ? v : -v;
        }
    }
}

void rm_core_bank_move(rm_core *c, int bank, double target)
{
    if (target < 0) target = 0;
    if (target > RM_ACTIVE_H) target = RM_ACTIVE_H;
    for (int k = 0; k < c->nctrl; k++)
        if (c->ctrl_bank[k] == bank) c->rod_target[k] = target;
}

void rm_core_bank_shift(rm_core *c, int bank, double d)
{
    for (int k = 0; k < c->nctrl; k++)
        if (c->ctrl_bank[k] == bank) rm_core_rod_move(c, k, c->rod_target[k] + d);
}

void rm_core_rod_move(rm_core *c, int rod, double target)
{
    if (rod < 0 || rod >= c->nctrl) return;
    if (target < 0) target = 0;
    if (target > RM_ACTIVE_H) target = RM_ACTIVE_H;
    c->rod_target[rod] = target;
}

double rm_core_bank_pos(const rm_core *c, int bank)
{
    double s = 0;
    int n = 0;
    for (int k = 0; k < c->nctrl; k++)
        if (c->ctrl_bank[k] == bank) {
            s += c->rod_ins[k];
            n++;
        }
    return n ? s / n : 0.0;
}

void rm_core_scram(rm_core *c)
{
    if (!c->scram) c->scram_time = c->t;
    c->scram = 1;
}

void rm_core_reset_scram(rm_core *c)
{
    c->scram = 0;
    for (int k = 0; k < c->nctrl; k++) c->rod_target[k] = c->rod_ins[k];
}

static void set_bank_now(rm_core *c, int bank, double ins)
{
    for (int k = 0; k < c->nctrl; k++)
        if (c->ctrl_bank[k] == bank) c->rod_ins[k] = c->rod_target[k] = ins;
}

static double eigen(rm_core *c, int outers)
{
    rm_core_update_xs(c);
    double k = 0;
    for (int i = 0; i < outers; i++) k = rm_diff_forward(&c->dif, 1, 2, 1.4);
    return k;
}

static void set_flows(rm_core *c, double frac)
{
    /* nominal core flow for 2300 MWt between 380 and 540 C, orificed by channel power */
    double W_core = RM_P_RATED / (1270.0 * 160.0);
    rm_diff *d = &c->dif;
    double *pch = malloc(sizeof(double) * (size_t)c->nchan);
    double ptot = 0.0;
    for (int ch = 0; ch < c->nchan; ch++) {
        int slot = d->iperm[c->col_of_chan[ch]];
        double p = 0.0;
        for (int z = RM_NZ_BOT; z < RM_NZ_BOT + RM_NZ_ACT; z++) {
            int n = z * d->ncol + slot;
            p += d->ksf[0][n] * d->phi[0][n] + d->ksf[1][n] * d->phi[1][n];
        }
        pch[ch] = p;
        ptot += p;
    }
    for (int ch = 0; ch < c->nchan; ch++) {
        double share = ptot > 0 ? pch[ch] / ptot : 1.0 / c->nchan;
        /* orifices follow power but never starve an edge channel */
        double w = 0.7 * share + 0.3 / c->nchan;
        c->th.W[ch] = frac * 0.93 * W_core * w;
        c->th.W_byp[ch] = frac * 0.07 * W_core / c->nchan;
    }
    free(pch);
}

double rm_core_steady(rm_core *c, double power_frac, int iterate_th, int with_xenon)
{
    /* safety bank out, regulating bank at mid travel, shim banks moved
     * together to find criticality */
    set_bank_now(c, BANK_SAFETY, 0.0);
    set_bank_now(c, BANK_REG, RM_ACTIVE_H / 2);
    double lo, hi, pos = 80.0;
    int passes = iterate_th ? 6 : 1;
    for (int pass = 0; pass < passes; pass++) {
        lo = 0.0;
        hi = RM_ACTIVE_H;
        for (int it = 0; it < 24; it++) {
            pos = 0.5 * (lo + hi);
            for (int b = BANK_SHIM_A; b <= BANK_SHIM_D; b++) set_bank_now(c, b, pos);
            double k = eigen(c, (pass == 0 && it == 0) ? 500 : (it < 8 ? 40 : 80));
            if (k > 1.0) lo = pos;
            else hi = pos;
        }
        for (int b = BANK_SHIM_A; b <= BANK_SHIM_D; b++) set_bank_now(c, b, pos);
        eigen(c, 200);
        for (int i = 0; i < 200; i++) rm_diff_adjoint(&c->dif, 1, 2, 1.4);
        double pf = power_frac * RM_P_RATED;
        c->pks.n = power_frac > 0 ? power_frac : 1e-9;
        for (int i = 0; i < 6; i++) c->pks.y[i] = c->pk.beta[i] * c->pks.n / c->pk.lambda[i];
        for (int i = 0; i < RM_NDH; i++) c->dh[i] = DH_A[i] * c->pks.n / DH_L[i];
        c->p_fission = c->pks.n * RM_P_RATED * (1.0 - dh_total());
        c->p_decay = 0;
        for (int i = 0; i < RM_NDH; i++) c->p_decay += DH_L[i] * c->dh[i] * RM_P_RATED;
        c->p_thermal = c->p_fission + c->p_decay;
        (void)pf;
        distribute_power(c, c->p_thermal, c->p_fission);
        if (pass == 0) set_flows(c, power_frac > 0.2 ? 1.0 : 0.2);
        if (with_xenon) {
            /* march the isotopes to their short-lived equilibrium (not Pa/U-233) */
            for (int i = 0; i < 60; i++) {
                double U3 = 0, Pa = 0;
                (void)U3; (void)Pa;
                update_isotopes(c, 3600.0);
            }
            size_t nf = (size_t)c->nchan * RM_NZ_ACT;
            for (size_t n = 0; n < nf; n++) {
                c->iso[ISO_PA233][n] = 0.0;
                c->iso[ISO_U233][n] = 0.0;
                c->iso[ISO_U235][n] = rm_xs_ref_density[RM_NUC_U235];
            }
        }
        if (iterate_th) {
            for (int i = 0; i < 400; i++) {
                for (int ch = 0; ch < c->nchan; ch++) c->th.T_in[ch] = 653.15;
                rm_coreth_step(&c->th, 2.0);
            }
        }
    }
    c->rho = rm_diff_reactivity(&c->dif);
    c->t_adjoint = c->t_doppler = c->t;
    return pos;
}

void rm_core_shutdown(rm_core *c, double T)
{
    for (int k = 0; k < c->nctrl; k++) {
        c->rod_ins[k] = c->rod_target[k] = RM_ACTIVE_H;
        c->rod_vel[k] = 0.0;
    }
    c->scram = 0;
    rm_coreth_isothermal(&c->th, T);
    size_t nf = (size_t)c->nchan * RM_NZ_ACT;
    for (size_t n = 0; n < nf; n++) {
        c->iso[ISO_I135][n] = c->iso[ISO_XE135][n] = 0.0;
        c->iso[ISO_SM149][n] += c->iso[ISO_PM149][n];
        c->iso[ISO_PM149][n] = 0.0;
        c->iso[ISO_U233][n] += c->iso[ISO_PA233][n];
        c->iso[ISO_PA233][n] = 0.0;
    }
    for (int i = 0; i < RM_NDH; i++) c->dh[i] = 0.0;
    eigen(c, 400);
    for (int i = 0; i < 200; i++) rm_diff_adjoint(&c->dif, 1, 2, 1.4);
    c->rho = rm_diff_reactivity(&c->dif);
    /* source-driven equilibrium: rho n / Lambda + S = 0 */
    double n = c->rho < -1e-5 ? -c->src * c->pk.Lambda / c->rho : 1e-9;
    c->pks.n = n;
    for (int i = 0; i < 6; i++) c->pks.y[i] = c->pk.beta[i] * n / c->pk.lambda[i];
    c->p_fission = n * RM_P_RATED * (1.0 - dh_total());
    c->p_decay = 0.0;
    c->p_thermal = c->p_fission;
    distribute_power(c, c->p_thermal, c->p_fission);
    c->t_adjoint = c->t_doppler = c->t;
}

void rm_core_step(rm_core *c, double dt)
{
    move_rods(c, dt);
    rm_core_update_xs(c);
    rm_diff *d = &c->dif;

    /* Doppler coefficient for the in-step prompt feedback */
    if (c->t - c->t_doppler >= 5.0 || c->t_doppler == 0.0) {
        update_xs_offset(c, 20.0);
        double r1 = rm_diff_reactivity(d);
        rm_core_update_xs(c);
        double r0 = rm_diff_reactivity(d);
        c->rho_doppler_coef = (r1 - r0) / 20.0;
        c->t_doppler = c->t > 0 ? c->t : 1e-9;
    }
    c->rho = rm_diff_reactivity(d);

    rm_pk_step_in in;
    in.rho0 = c->rho;
    in.drho_dt = 0.0;
    in.drho_dE = c->rho_doppler_coef * RM_P_RATED * FRAC_FUEL * (1.0 - dh_total()) / c->c_fuel;
    in.source = c->src;
    in.dt = dt;
    in.max_rel = 0.01;
    in.h_min = 1e-9;
    rm_pk_step_out out;
    c->pk_substeps = rm_pk_advance(&c->pk, &c->pks, &in, &out);
    double n_avg = out.energy / dt;

    /* flux shape and adjoint */
    rm_diff_forward(d, 1, 1, 1.4);
    if (c->t - c->t_adjoint >= 2.0) {
        rm_diff_adjoint(d, 3, 1, 1.4);
        c->t_adjoint = c->t;
    }

    /* decay heat */
    for (int i = 0; i < RM_NDH; i++) {
        double e = exp(-DH_L[i] * dt);
        c->dh[i] = c->dh[i] * e + DH_A[i] * n_avg / DH_L[i] * (1 - e);
    }
    c->p_fission = n_avg * RM_P_RATED * (1.0 - dh_total());
    c->p_decay = 0;
    for (int i = 0; i < RM_NDH; i++) c->p_decay += DH_L[i] * c->dh[i] * RM_P_RATED;
    c->p_thermal = c->p_fission + c->p_decay;
    distribute_power(c, c->p_thermal, c->p_fission);

    rm_coreth_step(&c->th, dt);
    update_isotopes(c, dt);
    c->t += dt;
}

double rm_core_mean_fuel_T(const rm_core *c)
{
    size_t nf = (size_t)c->nchan * RM_NZ_ACT;
    double s = 0;
    for (size_t n = 0; n < nf; n++) s += fuel_T_eff(&c->th, (int)n);
    return s / nf;
}

double rm_core_max_fuel_T(const rm_core *c)
{
    size_t nf = (size_t)c->nchan * RM_NZ_ACT;
    double m = 0;
    for (size_t n = 0; n < nf; n++)
        if (c->th.T_center[n] > m) m = c->th.T_center[n];
    return m;
}

double rm_core_max_clad_T(const rm_core *c)
{
    size_t nf = (size_t)c->nchan * RM_NZ_ACT;
    double m = 0;
    for (size_t n = 0; n < nf; n++)
        if (c->th.T[TH_CL][n] > m) m = c->th.T[TH_CL][n];
    return m;
}
