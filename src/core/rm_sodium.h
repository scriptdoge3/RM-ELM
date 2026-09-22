/*
 * Sodium properties from Fink & Leibowitz, "Thermodynamic and Transport
 * Properties of Sodium Liquid and Vapor", ANL/RE-95/2 (1995).
 *
 * SI units throughout. Enthalpy is referenced to solid sodium at 298.15 K
 * and includes the solid phase and the heat of fusion, so pipes and tanks
 * can freeze and thaw.
 */
#ifndef RM_SODIUM_H
#define RM_SODIUM_H

#define RM_NA_TMELT 370.98    /* K */
#define RM_NA_TCRIT 2503.7    /* K */
#define RM_NA_HFUS 113.2e3    /* J/kg */
#define RM_NA_MOLAR 22.98977e-3

double rm_na_rho(double T);       /* liquid density, kg/m3 */
double rm_na_cp(double T);        /* liquid heat capacity, J/kg/K */
double rm_na_k(double T);         /* liquid conductivity, W/m/K */
double rm_na_mu(double T);        /* liquid viscosity, Pa s */
double rm_na_psat(double T);      /* vapour pressure, Pa */
double rm_na_tsat(double p);      /* boiling temperature at p, K */
double rm_na_hfg(double T);       /* latent heat of vaporisation, J/kg */
double rm_na_rho_vap(double T);   /* saturated vapour density, kg/m3 */

/* Enthalpy of liquid sodium (T >= melting point). */
double rm_na_h_liq(double T);

/* Enthalpy/temperature with freezing: below the solidus the metal is solid,
 * between h_solidus and h_liquidus it is a slush at the melting point. */
double rm_na_h(double T);
double rm_na_T(double h);
double rm_na_liquid_fraction(double h);
double rm_na_h_solidus(void);
double rm_na_h_liquidus(void);

/* Prandtl and Peclet helpers. */
double rm_na_pr(double T);

#endif
