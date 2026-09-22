/*
 * IAPWS-IF97 water/steam properties, regions 1, 2 and 4.
 *
 * All inputs and outputs are SI: p [Pa], T [K], h [J/kg], s [J/kg/K],
 * v [m3/kg], cp [J/kg/K], mu [Pa s], k [W/m/K].
 *
 * Region 3 (near-critical, p > 16.53 MPa and 623 K < T < T_B23) is not
 * implemented. Pressures above RM_IF97_PMAX are clamped, which is fine for
 * the plant's 14 MPa steam cycle whose relief valves lift well below that.
 */
#ifndef RM_IF97_H
#define RM_IF97_H

#define RM_IF97_PMAX 16.5e6
#define RM_IF97_PMIN 611.657
#define RM_IF97_TMIN 273.15
#define RM_IF97_TMAX 1073.15

typedef struct {
    double v, h, s, cp;
} rm_if97_pt;

typedef struct {
    double T;      /* temperature */
    double v;      /* specific volume (mixture for two-phase) */
    double rho;    /* 1/v */
    double x;      /* quality: <0 subcooled (by enthalpy), 0..1 two-phase, >1 superheated */
    double s;      /* entropy */
    double cp;     /* isobaric heat capacity; saturated-phase value inside the dome */
    int region;    /* 1 = liquid, 2 = vapour, 4 = two-phase */
} rm_if97_state;

typedef struct {
    double p, T;
    double hf, hg, vf, vg, sf, sg, cpf, cpg;
} rm_if97_sat;

double rm_if97_psat(double T);
double rm_if97_tsat(double p);

void rm_if97_region1(double p, double T, rm_if97_pt *o);
void rm_if97_region2(double p, double T, rm_if97_pt *o);

void rm_if97_sat_p(double p, rm_if97_sat *sat);

/* Full property flash from pressure and enthalpy. */
void rm_if97_ph(double p, double h, rm_if97_state *st);
/* Same, reusing saturation data already computed for this pressure. */
void rm_if97_ph_sat(const rm_if97_sat *sat, double h, rm_if97_state *st);

double rm_if97_h_pT(double p, double T);
double rm_if97_h_ps(double p, double s);
double rm_if97_s_ph(double p, double h);

/* Transport properties (IAPWS 2008 viscosity, IAPWS 2011 conductivity,
 * without the critical enhancement terms). */
double rm_if97_mu(double rho, double T);
double rm_if97_k(double rho, double T);

#endif
