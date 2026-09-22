#include "rm_coreth.h"
#include "rm_kernels.h"
#include "rm_materials.h"
#include "rm_sodium.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846
#define G_ACC 9.80665
#define CP_LATENT 1.0e9   /* effective J/kg/K on a phase plateau (boiling or freezing) */

static double *dalloc(size_t n) { return calloc(n ? n : 1, sizeof(double)); }

int rm_coreth_init(rm_coreth *th, int nch, int nz, double height, const rm_channel_geom *geo)
{
    memset(th, 0, sizeof *th);
    th->nch = nch;
    th->nz = nz;
    th->dz = height / nz;
    th->geo = *geo;
    size_t nn = (size_t)nch * (size_t)nz;
    for (int i = 0; i < RM_TH_NCHAIN; i++) th->T[i] = dalloc(nn);
    th->T_zrh = dalloc(nn);
    th->h_cool = dalloc(nn);
    th->h_byp = dalloc(nn);
    th->void_frac = dalloc(nn);
    th->q_node = dalloc(nn);
    th->T_center = dalloc(nn);
    th->W = dalloc((size_t)nch);
    th->W_byp = dalloc((size_t)nch);
    th->T_in = dalloc((size_t)nch);
    th->T_out = dalloc((size_t)nch);
    th->T_out_byp = dalloc((size_t)nch);
    th->T_mixed_out = dalloc((size_t)nch);
    size_t ns = (size_t)nch * RM_TH_NCHAIN;
    th->a = dalloc(ns); th->b = dalloc(ns); th->c = dalloc(ns);
    th->d = dalloc(ns); th->x = dalloc(ns); th->w = dalloc(ns);
    th->s_cpc = dalloc((size_t)nch); th->s_cpb = dalloc((size_t)nch);
    th->s_zfac = dalloc((size_t)nch); th->s_zrhs = dalloc((size_t)nch);
    th->p_out = 0.25e6;
    th->p_in = 0.70e6;
    return 0;
}

void rm_coreth_free(rm_coreth *th)
{
    for (int i = 0; i < RM_TH_NCHAIN; i++) free(th->T[i]);
    free(th->T_zrh); free(th->h_cool); free(th->h_byp); free(th->void_frac);
    free(th->q_node); free(th->T_center); free(th->W); free(th->W_byp); free(th->T_in);
    free(th->T_out); free(th->T_out_byp); free(th->T_mixed_out);
    free(th->a); free(th->b); free(th->c); free(th->d); free(th->x); free(th->w);
    free(th->s_cpc); free(th->s_cpb); free(th->s_zfac); free(th->s_zrhs);
}

void rm_coreth_isothermal(rm_coreth *th, double T)
{
    size_t nn = (size_t)th->nch * (size_t)th->nz;
    for (size_t n = 0; n < nn; n++) {
        for (int i = 0; i < RM_TH_NCHAIN; i++) th->T[i][n] = T;
        th->T_zrh[n] = T;
        th->T_center[n] = T;
        th->h_cool[n] = rm_na_h(T);
        th->h_byp[n] = rm_na_h(T);
        th->void_frac[n] = 0.0;
    }
    for (int c = 0; c < th->nch; c++) {
        th->T_in[c] = T;
        th->T_out[c] = T;
        th->T_out_byp[c] = T;
        th->T_mixed_out[c] = T;
    }
}

/* Liquid-metal rod-bundle Nusselt number (Mikityuk 2009). */
static double nu_bundle(double pe, double pd)
{
    if (pe < 0.0) pe = 0.0;
    return 0.047 * (1.0 - exp(-3.8 * (pd - 1.0))) * (pow(pe, 0.77) + 250.0);
}

/* Annular/gap liquid-metal flow (Seban-Shimazaki). */
static double nu_gap(double pe)
{
    if (pe < 0.0) pe = 0.0;
    return 5.0 + 0.025 * pow(pe, 0.8);
}

/* Effective dh/dT for sodium at enthalpy h: finite on the liquid branch,
 * huge on the melting plateau and on the saturation plateau. */
static double cp_eff(double h, double hsat_l)
{
    double hs = rm_na_h_solidus(), hl = rm_na_h_liquidus();
    if (h > hs && h < hl) return CP_LATENT;
    if (h >= hsat_l) return CP_LATENT;
    double T = rm_na_T(h);
    if (T < RM_NA_TMELT) return 1200.0;
    return rm_na_cp(T);
}

void rm_coreth_step(rm_coreth *th, double dt)
{
    const rm_channel_geom *g = &th->geo;
    const int nch = th->nch;
    const double L = th->dz;
    const double H = L * th->nz;
    const double Rp = g->pellet_r;
    const double r1 = Rp / 6.0, r2 = Rp / 2.0, r3 = 5.0 * Rp / 6.0;
    const double nf = g->n_fuel_pins, nzr = g->n_zrh_pins;
    const double rcm = 0.5 * (g->clad_ir + g->clad_or);
    const double pd = g->pin_pitch / (2.0 * g->clad_or);
    const double V1 = nf * PI * (Rp * Rp / 9.0) * L;
    const double V2 = nf * PI * (4.0 * Rp * Rp / 9.0 - Rp * Rp / 9.0) * L;
    const double V3 = nf * PI * (Rp * Rp - 4.0 * Rp * Rp / 9.0) * L;
    const double Vf = V1 + V2 + V3;
    const double Vcl = nf * PI * (g->clad_or * g->clad_or - g->clad_ir * g->clad_ir) * L;
    const double Vz = nzr * PI * g->zrh_r * g->zrh_r * L;
    const double Vzcl = nzr * PI * (g->clad_or * g->clad_or - g->zrh_r * g->zrh_r) * L;
    const double Vd = g->duct_area * L;
    const double Vg = g->graphite_area * L;
    double *a = th->a, *b = th->b, *c = th->c, *d = th->d, *x = th->x;

    for (int z = 0; z < th->nz; z++) {
        /* local pressure and saturation */
        double p = th->p_out + 830.0 * G_ACC * (H - (z + 0.5) * L);
        double Tsat = rm_na_tsat(p);
        double hsat = rm_na_h_liq(Tsat);
        double hfg = rm_na_hfg(Tsat);
        double rho_v = rm_na_rho_vap(Tsat);

        for (int ch = 0; ch < nch; ch++) {
            int n = z * nch + ch;
            double Tf1 = th->T[TH_F1][n], Tf2 = th->T[TH_F2][n], Tf3 = th->T[TH_F3][n];
            double Tcl = th->T[TH_CL][n], Tc = th->T[TH_C][n], Td = th->T[TH_D][n];
            double Tb = th->T[TH_B][n], Tg = th->T[TH_G][n], Tz = th->T_zrh[n];
            double q = th->q_node[n];
            double W = th->W[ch], Wb = th->W_byp[ch];

            /* heat capacities (J/K) */
            double C1 = rm_fuel_rhocp(Tf1) * V1, C2 = rm_fuel_rhocp(Tf2) * V2, C3 = rm_fuel_rhocp(Tf3) * V3;
            double Ccl = rm_ht9_rhocp(Tcl) * Vcl;
            double Cz = rm_zrh_rhocp(Tz) * Vz + rm_ht9_rhocp(Tz) * Vzcl;
            double Cd = rm_ht9_rhocp(Td) * Vd;
            double Cg = RM_GRAPHITE_RHO * rm_graphite_cp(Tg) * Vg;
            double hc0 = th->h_cool[n], hb0 = th->h_byp[n];
            double Tcool_prop = Tc < RM_NA_TMELT ? RM_NA_TMELT : Tc;
            double Tb_prop = Tb < RM_NA_TMELT ? RM_NA_TMELT : Tb;
            double rho_c = rm_na_rho(Tcool_prop), rho_b = rm_na_rho(Tb_prop);
            double Mc = rho_c * g->flow_area * L * (1.0 - th->void_frac[n]);
            double Mb = rho_b * g->bypass_area * L;
            double cpc = cp_eff(hc0, hsat), cpb = cp_eff(hb0, hsat);

            /* conductances (W/K) */
            double kf1 = rm_fuel_k(0.5 * (Tf1 + Tf2)), kf2 = rm_fuel_k(0.5 * (Tf2 + Tf3)), kf3 = rm_fuel_k(Tf3);
            double G12 = 2.0 * PI * kf1 * L * nf / log(r2 / r1);
            double G23 = 2.0 * PI * kf2 * L * nf / log(r3 / r2);
            double G3s = 2.0 * PI * kf3 * L * nf / log(Rp / r3);
            double Ggap = rm_gap_h(0.5 * (Tf3 + Tcl)) * 2.0 * PI * Rp * L * nf;
            double kcl = rm_ht9_k(Tcl);
            double Gcli = 2.0 * PI * kcl * L * nf / log(rcm / g->clad_ir);
            double Gclo = 2.0 * PI * kcl * L * nf / log(g->clad_or / rcm);
            double G3cl = 1.0 / (1.0 / G3s + 1.0 / Ggap + 1.0 / Gcli);

            double kna = rm_na_k(Tcool_prop), cpna = rm_na_cp(Tcool_prop);
            double pe = fabs(W) * g->dh_bundle * cpna / (g->flow_area * kna);
            double hfilm = nu_bundle(pe, pd) * kna / g->dh_bundle;
            double alpha = th->void_frac[n];
            if (alpha > 0.9) hfilm *= 0.02;                 /* dry patches */
            else if (alpha > 0.5) hfilm *= 1.0 - 1.96 * (alpha - 0.5);
            double Gclc = 1.0 / (1.0 / Gclo + 1.0 / (hfilm * 2.0 * PI * g->clad_or * L * nf));
            double Gzc = 1.0 / (1.0 / (8.0 * PI * rm_zrh_k(Tz) * L * nzr) +
                                log(g->clad_or / g->zrh_r) / (2.0 * PI * rm_ht9_k(Tz) * L * nzr) +
                                1.0 / (hfilm * 2.0 * PI * g->clad_or * L * nzr));
            double Gcd = hfilm * g->duct_perim_in * L;
            double knab = rm_na_k(Tb_prop), cpnab = rm_na_cp(Tb_prop);
            double peb = fabs(Wb) * g->dh_bypass * cpnab / (g->bypass_area * knab);
            double hb = nu_gap(peb) * knab / g->dh_bypass;
            double Gdb = hb * g->duct_perim_out * L;
            double Ggb = 1.0 / (1.0 / (hb * g->graphite_perim * L) + 1.0 / (g->graphite_gap_h * g->graphite_perim * L));

            /* sources */
            double qf = q * g->frac_fuel;
            double q1 = qf * V1 / Vf, q2 = qf * V2 / Vf, q3 = qf * V3 / Vf;
            double qz = q * g->frac_zrh, qg = q * g->frac_graphite, qc = q * g->frac_coolant, qd = q * g->frac_duct;

            /* upstream coolant (already advanced) */
            double hup, hbup;
            if (z == 0) {
                hup = rm_na_h(th->T_in[ch]);
                hbup = hup;
            } else {
                hup = th->h_cool[n - nch];
                hbup = th->h_byp[n - nch];
            }
            double Wc = W > 0 ? W : 0.0, Wbb = Wb > 0 ? Wb : 0.0;

            /* eliminate the ZrH node: Tz = (Cz/dt*Tz0 + qz + Gzc*Tc)/(Cz/dt + Gzc) */
            double zden = Cz / dt + Gzc;
            double zfac = Gzc / zden;
            double zrhs = (Cz / dt * Tz + qz) / zden;

            size_t s = (size_t)ch;
            size_t st = (size_t)nch;
#define A(i) a[(i) * st + s]
#define B(i) b[(i) * st + s]
#define C(i) c[(i) * st + s]
#define D(i) d[(i) * st + s]
            /* fuel ring 1 */
            A(TH_F1) = 0.0;
            B(TH_F1) = C1 / dt + G12;
            C(TH_F1) = -G12;
            D(TH_F1) = C1 / dt * Tf1 + q1;
            /* ring 2 */
            A(TH_F2) = -G12;
            B(TH_F2) = C2 / dt + G12 + G23;
            C(TH_F2) = -G23;
            D(TH_F2) = C2 / dt * Tf2 + q2;
            /* ring 3 */
            A(TH_F3) = -G23;
            B(TH_F3) = C3 / dt + G23 + G3cl;
            C(TH_F3) = -G3cl;
            D(TH_F3) = C3 / dt * Tf3 + q3;
            /* clad */
            A(TH_CL) = -G3cl;
            B(TH_CL) = Ccl / dt + G3cl + Gclc;
            C(TH_CL) = -Gclc;
            D(TH_CL) = Ccl / dt * Tcl;
            /* bundle coolant, enthalpy linearised: h = hc0 + cpc*(T - Tc) */
            A(TH_C) = -Gclc;
            B(TH_C) = Mc * cpc / dt + Wc * cpc + Gclc + Gzc * (1.0 - zfac) + Gcd;
            C(TH_C) = -Gcd;
            D(TH_C) = Mc * cpc / dt * Tc + Wc * (hup - hc0 + cpc * Tc) + qc + Gzc * zrhs;
            /* duct */
            A(TH_D) = -Gcd;
            B(TH_D) = Cd / dt + Gcd + Gdb;
            C(TH_D) = -Gdb;
            D(TH_D) = Cd / dt * Td + qd;
            /* bypass coolant */
            A(TH_B) = -Gdb;
            B(TH_B) = Mb * cpb / dt + Wbb * cpb + Gdb + Ggb;
            C(TH_B) = -Ggb;
            D(TH_B) = Mb * cpb / dt * Tb + Wbb * (hbup - hb0 + cpb * Tb);
            /* graphite */
            A(TH_G) = -Ggb;
            B(TH_G) = Cg / dt + Ggb;
            C(TH_G) = 0.0;
            D(TH_G) = Cg / dt * Tg + qg;
#undef A
#undef B
#undef C
#undef D
            th->s_zrhs[ch] = zrhs;
            th->s_zfac[ch] = zfac;
            th->s_cpc[ch] = cpc;
            th->s_cpb[ch] = cpb;
        }

        rm_tridiag_batch((size_t)nch, RM_TH_NCHAIN, (size_t)nch, a, b, c, d, x, th->w);

        for (int ch = 0; ch < nch; ch++) {
            int n = z * nch + ch;
            double Tc_old = th->T[TH_C][n], Tb_old = th->T[TH_B][n];
            for (int i = 0; i < RM_TH_NCHAIN; i++) th->T[i][n] = x[(size_t)i * nch + ch];
            th->T_zrh[n] = th->s_zrhs[ch] + th->s_zfac[ch] * th->T[TH_C][n];

            /* coolant enthalpy update and phase bookkeeping */
            double hc = th->h_cool[n] + th->s_cpc[ch] * (th->T[TH_C][n] - Tc_old);
            double hb = th->h_byp[n] + th->s_cpb[ch] * (th->T[TH_B][n] - Tb_old);
            th->h_cool[n] = hc;
            th->h_byp[n] = hb;
            if (hc > hsat) {
                double xq = (hc - hsat) / hfg;
                if (xq > 1.0) xq = 1.0;
                double rl = rm_na_rho(Tsat);
                th->void_frac[n] = xq / (xq + (1.0 - xq) * rho_v / rl);
                th->T[TH_C][n] = Tsat;
            } else {
                th->void_frac[n] = 0.0;
                th->T[TH_C][n] = rm_na_T(hc);
            }
            th->T[TH_B][n] = hb > hsat ? Tsat : rm_na_T(hb);

            /* centreline estimate from the inner node */
            double qv = th->q_node[n] * g->frac_fuel / Vf;
            th->T_center[n] = th->T[TH_F1][n] + qv * r1 * r1 / (4.0 * rm_fuel_k(th->T[TH_F1][n]));
        }
    }
    for (int ch = 0; ch < nch; ch++) {
        int n = (th->nz - 1) * nch + ch;
        th->T_out[ch] = th->T[TH_C][n];
        th->T_out_byp[ch] = th->T[TH_B][n];
        double W = th->W[ch] > 0 ? th->W[ch] : 0.0, Wb = th->W_byp[ch] > 0 ? th->W_byp[ch] : 0.0;
        if (W + Wb > 1e-9) {
            double hm = (W * th->h_cool[n] + Wb * th->h_byp[n]) / (W + Wb);
            th->T_mixed_out[ch] = rm_na_T(hm);
        } else {
            th->T_mixed_out[ch] = th->T_out[ch];
        }
    }
}

double rm_coreth_dp(const rm_coreth *th, int ch, double W)
{
    const rm_channel_geom *g = &th->geo;
    double H = th->dz * th->nz;
    double T = 0.5 * (th->T_in[ch] + th->T_out[ch]);
    double rho = rm_na_rho(T), mu = rm_na_mu(T);
    double v = W / (rho * g->flow_area);
    double re = fabs(rho * v * g->dh_bundle / mu);
    double f = re > 1.0 ? 64.0 / re : 64.0;
    double ft = re > 1.0 ? 0.316 * pow(re, -0.25) * 1.25 : 0.0;
    if (ft > f) f = ft;
    double K = 6.0; /* inlet orifice, grid plate, spacer wires, outlet */
    /* shield and gas-plenum lengths above/below the active core add friction */
    double Lh = H + 1.6;
    double dyn = 0.5 * rho * v * fabs(v);
    return (f * Lh / g->dh_bundle + K) * dyn;
}
