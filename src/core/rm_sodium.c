#include "rm_sodium.h"

#include <math.h>

/* Solid sodium heat capacity (kJ/kg/K), a linear fit that integrates to the
 * tabulated solid enthalpy between room temperature and the melting point. */
#define CPS_A 1.20e3
#define CPS_B 0.75

static double clampT(double T)
{
    if (T < RM_NA_TMELT) return RM_NA_TMELT;
    if (T > 2400.0) return 2400.0;
    return T;
}

double rm_na_rho(double T)
{
    T = clampT(T);
    double t = 1.0 - T / RM_NA_TCRIT;
    return 219.0 + 275.32 * t + 511.58 * sqrt(t);
}

double rm_na_h_liq(double T)
{
    /* kJ/kg -> J/kg, valid 371..2000 K */
    return 1e3 * (-365.77 + 1.6582 * T - 4.2395e-4 * T * T + 1.4847e-7 * T * T * T + 2992.6 / T);
}

double rm_na_cp(double T)
{
    T = clampT(T);
    return 1e3 * (1.6582 - 8.479e-4 * T + 4.4541e-7 * T * T - 2992.6 / (T * T));
}

double rm_na_k(double T)
{
    T = clampT(T);
    return 124.67 - 0.11381 * T + 5.5226e-5 * T * T - 1.1842e-8 * T * T * T;
}

double rm_na_mu(double T)
{
    T = clampT(T);
    return exp(-6.4406 - 0.3958 * log(T) + 556.835 / T);
}

double rm_na_psat(double T)
{
    if (T < 300.0) T = 300.0;
    return 1e6 * exp(11.9463 - 12633.73 / T - 0.4672 * log(T));
}

double rm_na_tsat(double p)
{
    /* Newton on ln p(T). */
    double lp = log(p * 1e-6);
    double T = 1156.0;
    for (int i = 0; i < 30; i++) {
        double f = 11.9463 - 12633.73 / T - 0.4672 * log(T) - lp;
        double df = 12633.73 / (T * T) - 0.4672 / T;
        double dT = f / df;
        T -= dT;
        if (fabs(dT) < 1e-8) break;
    }
    return T;
}

double rm_na_hfg(double T)
{
    if (T >= RM_NA_TCRIT) return 0.0;
    double t = 1.0 - T / RM_NA_TCRIT;
    return 1e3 * (393.37 * t + 4398.6 * pow(t, 0.29302));
}

double rm_na_rho_vap(double T)
{
    /* Ideal gas of monomers with a crude 10% dimer allowance. */
    return 1.1 * rm_na_psat(T) * RM_NA_MOLAR / (8.314462 * T);
}

double rm_na_h_liquidus(void)
{
    return rm_na_h_liq(RM_NA_TMELT);
}

double rm_na_h_solidus(void)
{
    return rm_na_h_liquidus() - RM_NA_HFUS;
}

static double h_solid(double T)
{
    /* integral of cp_s from T to Tmelt subtracted from the solidus */
    double a = T - 298.15, b = RM_NA_TMELT - 298.15;
    double ib = CPS_A * b + 0.5 * CPS_B * b * b;
    double ia = CPS_A * a + 0.5 * CPS_B * a * a;
    return rm_na_h_solidus() - (ib - ia);
}

double rm_na_h(double T)
{
    if (T < RM_NA_TMELT) return h_solid(T);
    return rm_na_h_liq(T);
}

double rm_na_T(double h)
{
    double hs = rm_na_h_solidus(), hl = rm_na_h_liquidus();
    if (h <= hs) {
        /* invert the quadratic solid enthalpy */
        double b = RM_NA_TMELT - 298.15;
        double ib = CPS_A * b + 0.5 * CPS_B * b * b;
        double ia = ib - (hs - h);
        /* 0.5 B a^2 + A a - ia = 0 */
        double a = (-CPS_A + sqrt(CPS_A * CPS_A + 2.0 * CPS_B * ia)) / CPS_B;
        return 298.15 + a;
    }
    if (h < hl) return RM_NA_TMELT;
    double T = RM_NA_TMELT + (h - hl) / 1300.0;
    for (int i = 0; i < 20; i++) {
        double f = rm_na_h_liq(T) - h;
        double dT = f / rm_na_cp(T);
        T -= dT;
        if (fabs(dT) < 1e-9) break;
    }
    return T;
}

double rm_na_liquid_fraction(double h)
{
    double hs = rm_na_h_solidus(), hl = rm_na_h_liquidus();
    if (h <= hs) return 0.0;
    if (h >= hl) return 1.0;
    return (h - hs) / (hl - hs);
}

double rm_na_pr(double T)
{
    return rm_na_cp(T) * rm_na_mu(T) / rm_na_k(T);
}
