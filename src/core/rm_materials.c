#include "rm_materials.h"

#include <math.h>

double rm_fuel_k(double T)
{
    /* Billone U-Pu-Zr correlation at w_Zr = 0.10, w_Pu = 0, times 0.75 for
     * the porosity and sodium logging of fuel swollen out to the cladding. */
    const double wz = 0.10;
    double k = 17.5 * (1.0 - 2.23 * wz) / (1.0 + 1.61 * wz)
             + 1.54e-2 * (1.0 + 0.061 * wz) / (1.0 + 1.61 * wz) * T
             + 9.38e-6 * T * T;
    return 0.75 * k;
}

double rm_fuel_rhocp(double T)
{
    double cp = 140.0 + 0.05 * (T - 300.0);
    if (T > RM_FUEL_SOLIDUS) cp = 140.0 + 0.05 * (RM_FUEL_SOLIDUS - 300.0);
    return 15.8e3 * cp;
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
