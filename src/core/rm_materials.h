/*
 * Solid material properties for the core and structures. SI units, T in K.
 *
 *   U-10Zr   metallic fuel slug (Billone correlation, degraded for porosity)
 *   HT9      ferritic-martensitic cladding and ducts (Leibowitz & Blomquist)
 *   SS316    vessel, piping, heat exchanger tubes
 *   Graphite canned moderator blocks and reflector (Butland & Maddison cp)
 *   ZrH1.6   moderator pins (Simnad)
 */
#ifndef RM_MATERIALS_H
#define RM_MATERIALS_H

#define RM_FUEL_SOLIDUS 1470.0     /* K, U-10Zr at moderate burnup */
#define RM_FUEL_LIQUIDUS 1520.0
#define RM_FCCI_ONSET 923.0        /* K, fuel/clad eutectic penetration becomes significant */
#define RM_HT9_MELT 1700.0
#define RM_ZRH_X0 1.60             /* as-fabricated H/Zr ratio */

double rm_fuel_k(double T);
double rm_fuel_rhocp(double T);    /* J/m3/K */

double rm_ht9_k(double T);
double rm_ht9_rhocp(double T);

double rm_ss316_k(double T);
double rm_ss316_rhocp(double T);

double rm_graphite_cp(double T);   /* J/kg/K */
#define RM_GRAPHITE_RHO 1750.0

double rm_zrh_k(double T);
double rm_zrh_rhocp(double T);
/* Equilibrium hydrogen dissociation pressure over ZrH_x, Pa. */
double rm_zrh_ph2(double T, double x);

#endif
