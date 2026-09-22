/*
 * Solid material properties for the core and structures. SI units, T in K.
 *
 *   (U,Th)C  carbide fuel pellets, 90% of theoretical density, helium bonded
 *            (heavy metal 10 wt% U-235, 40 wt% U-238, 50 wt% Th-232)
 *   HT9      ferritic-martensitic cladding and ducts (Leibowitz & Blomquist)
 *   SS316    vessel, piping, heat exchanger tubes
 *   Graphite canned moderator blocks and reflector (Butland & Maddison cp)
 *   ZrH1.6   moderator pins (Simnad)
 */
#ifndef RM_MATERIALS_H
#define RM_MATERIALS_H

#define RM_FUEL_SOLIDUS 2700.0     /* K, (U,Th)C solid solution */
#define RM_FUEL_LIQUIDUS 2780.0
#define RM_FUEL_DENSITY 10.8e3     /* kg/m3 at 90% TD (TD = 12.0 g/cc) */
#define RM_CLAD_CARBURISATION 973.0 /* K, carbon transfer into HT9 becomes rapid */
#define RM_HT9_MELT 1700.0
#define RM_ZRH_X0 1.60             /* as-fabricated H/Zr ratio */

double rm_fuel_k(double T);
double rm_fuel_rhocp(double T);    /* J/m3/K */
/* Helium bond gap conductance for a partly closed pellet-clad gap, W/m2/K. */
double rm_gap_h(double T_gap);

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
