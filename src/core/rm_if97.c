/*
 * IAPWS-IF97 regions 1, 2 and 4, plus the IAPWS 2008/2011 transport
 * properties. Coefficients live in rm_if97_coeffs.h, generated from the
 * reference `iapws` Python package by tools/gen_if97_coeffs.py.
 */
#include "rm_if97.h"
#include "rm_if97_coeffs.h"

#include <math.h>

#define R_IF97 461.526 /* J/kg/K */
#define N_R1 34
#define N_R2_0 9
#define N_R2 43

static const double sat_n[11] = {
    0.0,
    0.11670521452767e4, -0.72421316703206e6, -0.17073846940092e2,
    0.12020824702470e5, -0.32325550322333e7, 0.14915108613530e2,
    -0.48232657361591e4, 0.40511340542057e6, -0.23855557567849e0,
    0.65017534844798e3,
};

static double ipow(double x, int n)
{
    double r = 1.0;
    int neg = n < 0;
    unsigned int e = (unsigned int)(neg ? -n : n);
    while (e) {
        if (e & 1u) r *= x;
        x *= x;
        e >>= 1;
    }
    return neg ? 1.0 / r : r;
}

static double clamp_p(double p)
{
    if (p < RM_IF97_PMIN) return RM_IF97_PMIN;
    if (p > RM_IF97_PMAX) return RM_IF97_PMAX;
    return p;
}

double rm_if97_psat(double T)
{
    if (T < 273.15) T = 273.15;
    if (T > 647.096) T = 647.096;
    double th = T + sat_n[9] / (T - sat_n[10]);
    double A = th * th + sat_n[1] * th + sat_n[2];
    double B = sat_n[3] * th * th + sat_n[4] * th + sat_n[5];
    double C = sat_n[6] * th * th + sat_n[7] * th + sat_n[8];
    double q = 2.0 * C / (-B + sqrt(B * B - 4.0 * A * C));
    return q * q * q * q * 1e6;
}

double rm_if97_tsat(double p)
{
    double pm = p * 1e-6;
    if (pm < 611.212677e-6) pm = 611.212677e-6;
    if (pm > 22.064) pm = 22.064;
    double beta = sqrt(sqrt(pm));
    double E = beta * beta + sat_n[3] * beta + sat_n[6];
    double F = sat_n[1] * beta * beta + sat_n[4] * beta + sat_n[7];
    double G = sat_n[2] * beta * beta + sat_n[5] * beta + sat_n[8];
    double D = 2.0 * G / (-F - sqrt(F * F - 4.0 * E * G));
    double a = sat_n[10] + D;
    return 0.5 * (a - sqrt(a * a - 4.0 * (sat_n[9] + sat_n[10] * D)));
}

void rm_if97_region1(double p, double T, rm_if97_pt *o)
{
    double pi = p / 16.53e6;
    double tau = 1386.0 / T;
    double a = 7.1 - pi, b = tau - 1.222;
    double g = 0.0, gp = 0.0, gt = 0.0, gtt = 0.0;
    for (int i = 0; i < N_R1; i++) {
        int I = if97_r1_I[i], J = if97_r1_J[i];
        double n = if97_r1_n[i];
        double aI = ipow(a, I), bJ = ipow(b, J);
        double aI1 = I ? ipow(a, I - 1) : 0.0;
        double bJ1 = J ? ipow(b, J - 1) : 0.0;
        g += n * aI * bJ;
        gp -= n * I * aI1 * bJ;
        gt += n * aI * J * bJ1;
        gtt += n * aI * J * (J - 1) * (J > 1 || J < 0 ? ipow(b, J - 2) : 0.0);
    }
    o->v = pi * gp * R_IF97 * T / p;
    o->h = tau * gt * R_IF97 * T;
    o->s = R_IF97 * (tau * gt - g);
    o->cp = -R_IF97 * tau * tau * gtt;
}

void rm_if97_region2(double p, double T, rm_if97_pt *o)
{
    double pi = p / 1e6;
    double tau = 540.0 / T;
    double g0 = log(pi), g0p = 1.0 / pi, g0t = 0.0, g0tt = 0.0;
    for (int i = 0; i < N_R2_0; i++) {
        int J = if97_r2_J0[i];
        double n = if97_r2_n0[i];
        g0 += n * ipow(tau, J);
        g0t += n * J * ipow(tau, J - 1);
        g0tt += n * J * (J - 1) * ipow(tau, J - 2);
    }
    double b = tau - 0.5;
    double gr = 0.0, grp = 0.0, grt = 0.0, grtt = 0.0;
    for (int i = 0; i < N_R2; i++) {
        int I = if97_r2_I[i], J = if97_r2_J[i];
        double n = if97_r2_n[i];
        double pI = ipow(pi, I), bJ = ipow(b, J);
        double bJ1 = J ? ipow(b, J - 1) : 0.0;
        double bJ2 = J > 1 ? ipow(b, J - 2) : 0.0;
        gr += n * pI * bJ;
        grp += n * I * ipow(pi, I - 1) * bJ;
        grt += n * pI * J * bJ1;
        grtt += n * pI * J * (J - 1) * bJ2;
    }
    o->v = pi * (g0p + grp) * R_IF97 * T / p;
    o->h = tau * (g0t + grt) * R_IF97 * T;
    o->s = R_IF97 * (tau * (g0t + grt) - (g0 + gr));
    o->cp = -R_IF97 * tau * tau * (g0tt + grtt);
}

void rm_if97_sat_p(double p, rm_if97_sat *sat)
{
    rm_if97_pt l, g;
    p = clamp_p(p);
    double T = rm_if97_tsat(p);
    rm_if97_region1(p, T, &l);
    rm_if97_region2(p, T, &g);
    sat->p = p;
    sat->T = T;
    sat->hf = l.h; sat->hg = g.h;
    sat->vf = l.v; sat->vg = g.v;
    sat->sf = l.s; sat->sg = g.s;
    sat->cpf = l.cp; sat->cpg = g.cp;
}

/* Safeguarded Newton solve of f(T) = target inside [lo, hi] for a monotonic
 * increasing property. which: 0 = h, 1 = s. */
static double solve_T(int region, int which, double p, double target,
                      double lo, double hi, double T0, rm_if97_pt *out)
{
    double T = T0;
    if (!(T > lo && T < hi)) T = 0.5 * (lo + hi);
    for (int it = 0; it < 60; it++) {
        if (region == 1) rm_if97_region1(p, T, out);
        else rm_if97_region2(p, T, out);
        double f = (which == 0 ? out->h : out->s) - target;
        double df = which == 0 ? out->cp : out->cp / T;
        if (f > 0.0) hi = T; else lo = T;
        double Tn = T - f / df;
        if (!(Tn > lo && Tn < hi)) Tn = 0.5 * (lo + hi);
        if (fabs(Tn - T) < 1e-9 * T) {
            T = Tn;
            break;
        }
        T = Tn;
    }
    if (region == 1) rm_if97_region1(p, T, out);
    else rm_if97_region2(p, T, out);
    return T;
}

void rm_if97_ph_sat(const rm_if97_sat *sat, double h, rm_if97_state *st)
{
    double p = sat->p;
    double hfg = sat->hg - sat->hf;
    rm_if97_pt o;
    if (h < sat->hf) {
        double T0 = sat->T - (sat->hf - h) / sat->cpf;
        st->T = solve_T(1, 0, p, h, RM_IF97_TMIN - 1.0, sat->T, T0, &o);
        st->v = o.v; st->s = o.s; st->cp = o.cp;
        st->x = (h - sat->hf) / hfg;
        st->region = 1;
    } else if (h > sat->hg) {
        double T0 = sat->T + (h - sat->hg) / sat->cpg;
        st->T = solve_T(2, 0, p, h, sat->T, RM_IF97_TMAX + 200.0, T0, &o);
        st->v = o.v; st->s = o.s; st->cp = o.cp;
        st->x = 1.0 + (h - sat->hg) / hfg;
        st->region = 2;
    } else {
        double x = (h - sat->hf) / hfg;
        st->T = sat->T;
        st->v = sat->vf + x * (sat->vg - sat->vf);
        st->s = sat->sf + x * (sat->sg - sat->sf);
        st->cp = x < 0.5 ? sat->cpf : sat->cpg;
        st->x = x;
        st->region = 4;
    }
    st->rho = 1.0 / st->v;
}

void rm_if97_ph(double p, double h, rm_if97_state *st)
{
    rm_if97_sat sat;
    rm_if97_sat_p(p, &sat);
    rm_if97_ph_sat(&sat, h, st);
}

double rm_if97_h_pT(double p, double T)
{
    rm_if97_pt o;
    p = clamp_p(p);
    double Ts = rm_if97_tsat(p);
    if (T <= Ts) rm_if97_region1(p, T, &o);
    else rm_if97_region2(p, T, &o);
    return o.h;
}

double rm_if97_h_ps(double p, double s)
{
    rm_if97_sat sat;
    rm_if97_pt o;
    rm_if97_sat_p(p, &sat);
    if (s < sat.sf) {
        solve_T(1, 1, sat.p, s, RM_IF97_TMIN - 1.0, sat.T, sat.T - 50.0, &o);
        return o.h;
    }
    if (s > sat.sg) {
        solve_T(2, 1, sat.p, s, sat.T, RM_IF97_TMAX + 200.0, sat.T + 50.0, &o);
        return o.h;
    }
    double x = (s - sat.sf) / (sat.sg - sat.sf);
    return sat.hf + x * (sat.hg - sat.hf);
}

double rm_if97_s_ph(double p, double h)
{
    rm_if97_state st;
    rm_if97_ph(p, h, &st);
    return st.s;
}

double rm_if97_mu(double rho, double T)
{
    static const double H[4] = {1.67752, 2.20462, 0.6366564, -0.241605};
    static const int li[21] = {0, 1, 2, 3, 0, 1, 2, 3, 5, 0, 1, 2, 3, 4, 0, 1, 0, 3, 4, 3, 5};
    static const int lj[21] = {0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 4, 4, 5, 6, 6};
    static const double Hij[21] = {
        0.520094, 0.850895e-1, -0.108374e1, -0.289555, 0.222531, 0.999115,
        0.188797e1, 0.126613e1, 0.120573, -0.281378, -0.906851, -0.772479,
        -0.489837, -0.257040, 0.161913, 0.257399, -0.325372e-1, 0.698452e-1,
        0.872102e-2, -0.435673e-2, -0.593264e-3};
    double Tr = T / 647.096, Dr = rho / 322.0;
    double den = 0.0;
    for (int i = 0; i < 4; i++) den += H[i] / ipow(Tr, i);
    double mu0 = 100.0 * sqrt(Tr) / den;
    double sum = 0.0;
    for (int i = 0; i < 21; i++) sum += ipow(1.0 / Tr - 1.0, li[i]) * Hij[i] * ipow(Dr - 1.0, lj[i]);
    return mu0 * exp(Dr * sum) * 1e-6;
}

double rm_if97_k(double rho, double T)
{
    static const double no[5] = {2.443221e-3, 1.323095e-2, 6.770357e-3, -3.454586e-3, 4.096266e-4};
    static const double nij[5][6] = {
        {1.60397357, -0.646013523, 0.111443906, 0.102997357, -0.0504123634, 0.00609859258},
        {2.33771842, -2.78843778, 1.53616167, -0.463045512, 0.0832827019, -0.00719201245},
        {2.19650529, -4.54580785, 3.55777244, -1.40944978, 0.275418278, -0.0205938816},
        {-1.21051378, 1.60812989, -0.621178141, 0.0716373224, 0.0, 0.0},
        {-2.7203370, 4.57586331, -3.18369245, 1.1168348, -0.19268305, 0.012913842}};
    double Tr = T / 647.096, d = rho / 322.0;
    double den = 0.0;
    for (int i = 0; i < 5; i++) den += no[i] / ipow(Tr, i);
    double k0 = sqrt(Tr) / den;
    double sum = 0.0;
    for (int i = 0; i < 5; i++) {
        double ti = ipow(1.0 / Tr - 1.0, i);
        for (int j = 0; j < 6; j++) sum += ti * nij[i][j] * ipow(d - 1.0, j);
    }
    return k0 * exp(d * sum) * 1e-3;
}
