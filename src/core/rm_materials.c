#include "rm_materials.h"

#include <math.h>

double rm_fuel_k(double T)
{
    /* (U0.5Th0.5)C: UC-like conductivity lowered by alloy scattering, then
     * (1-p)/(1+2p) for 10% porosity. */
    double k_td = 18.5 + 2.0e-3 * (T - 773.0);
    return k_td * 0.9 / 1.2;
}

double rm_fuel_rhocp(double T)
{
    if (T > RM_FUEL_SOLIDUS) T = RM_FUEL_SOLIDUS;
    return RM_FUEL_DENSITY * (200.0 + 0.045 * (T - 300.0));
}

double rm_gap_h(double T_gap)
{
    /* helium conduction across ~60 um effective gap plus solid contact */
    double k_he = 2.639e-3 * pow(T_gap, 0.7085);
    return k_he / 60e-6 + 1500.0;
}

double rm_ht9_k(double T)
{
    if (T > 1200.0) T = 1200.0;
    return 17.622 + 2.42e-2 * T - 1.696e-5 * T * T;
}

double rm_ht9_rhocp(double T)
{
    return 7800.0 * (450.0 + 0.2 * (T - 300.0));
}

double rm_ss316_k(double T)
{
    return 9.248 + 0.01571 * T;
}

double rm_ss316_rhocp(double T)
{
    return 7950.0 * (462.0 + 0.134 * (T - 300.0));
}

double rm_graphite_cp(double T)
{
    if (T < 250.0) T = 250.0;
    return 1.0 / (11.07 * pow(T, -1.644) + 0.0003688 * pow(T, 0.02191));
}

double rm_zrh_k(double T)
{
    (void)T;
    return 18.0;
}

double rm_zrh_rhocp(double T)
{
    /* Simnad: 2.04 + 4.17e-3 T[C] J/cm3/K */
    return (2.04 + 4.17e-3 * (T - 273.15)) * 1e6;
}

double rm_zrh_ph2(double T, double x)
{
    double K1 = -3.8415 + 38.6433 * x - 34.2639 * x * x + 9.2821 * x * x * x;
    double K2 = -31.2982 + 23.5741 * x - 6.0280 * x * x;
    return 101325.0 * pow(10.0, K1 + K2 * 1e3 / T);
}
