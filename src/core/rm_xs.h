/*
 * Two-group homogenised cross sections for every node type.
 *
 * The numbers come from lattice calculations on ENDF/B-VIII.1 data
 * (ultra-fine resonance treatment, S(a,b) thermal scattering for graphite
 * and ZrH), see docs/PHYSICS.md. Fuel-channel constants depend on the local
 * state through branch tables:
 *
 *   fuel temperature        (Doppler; interpolated in sqrt(T))
 *   sodium density          (relative to liquid at 733 K; covers heating and voiding)
 *   graphite temperature
 *   ZrH temperature         (hydrogen lattice vibrations harden the spectrum)
 *   ZrH hydrogen content    (H/Zr, drops if the pins dehydride)
 *
 * Effects are superposed additively on the reference state.
 */
#ifndef RM_XS_H
#define RM_XS_H

typedef struct {
    double D[2];    /* cm */
    double sa[2];   /* absorption, 1/cm */
    double sf[2];   /* fission, 1/cm */
    double nsf[2];  /* nu-fission, 1/cm */
    double ksf[2];  /* kappa-fission, J/cm */
    double s12, s21;
} rm_xs2;

typedef struct {
    double Tf;      /* fuel temperature, K */
    double na_rel;  /* sodium density relative to reference */
    double Tg;      /* graphite, K */
    double Tz;      /* ZrH, K */
    double zrh_x;   /* H/Zr */
} rm_xs_state;

enum { RM_XS_REFL, RM_XS_AX_LOW, RM_XS_AX_UP, RM_XS_CR_ABS, RM_XS_CR_DISP, RM_XS_CR_NA, RM_XS_NFIXED };

enum { RM_NUC_U235, RM_NUC_U238, RM_NUC_TH232, RM_NUC_U233, RM_NUC_PA233,
       RM_NUC_XE135, RM_NUC_SM149, RM_NNUC };

typedef struct {
    double f[2], c[2], nf[2];   /* barns, per homogenised atom, relative to cell flux */
} rm_micro2;

void rm_xs_fuel(const rm_xs_state *st, rm_xs2 *out);
const rm_xs2 *rm_xs_fixed(int type);
const rm_micro2 *rm_xs_micro(int nuc);

/* delayed neutron data and prompt generation time for the reference core */
extern const double rm_xs_beta[6];
extern const double rm_xs_lambda[6];
extern const double rm_xs_Lambda;
/* reference homogenised number densities in the fuel channel (atoms/b-cm) */
extern const double rm_xs_ref_density[RM_NNUC];
/* reference sodium density used for na_rel (kg/m3) */
extern const double rm_xs_na_ref_rho;

#endif
