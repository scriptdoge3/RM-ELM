/*
 * Core thermal-hydraulics: every fuel channel, every axial node.
 *
 * Per node the channel is represented by a chain of lumped temperatures
 *
 *   fuel ring 1 - fuel ring 2 - fuel ring 3 - clad - bundle Na - duct - bypass Na - graphite
 *                                                        |
 *                                                    ZrH pins
 *
 * advanced with backward Euler. The ZrH node hangs off the bundle coolant
 * and is eliminated before the solve, which leaves a tridiagonal system per
 * channel. All channels at one axial level are solved together with the
 * batched tridiagonal kernel; coolant enters each level from the one below
 * (upwind, implicit), so a single sweep from inlet to outlet is exact.
 *
 * Sodium boiling is handled at the coolant nodes by holding the node at the
 * local saturation temperature and converting surplus enthalpy into quality.
 */
#ifndef RM_CORETH_H
#define RM_CORETH_H

#define RM_TH_NCHAIN 8
enum { TH_F1, TH_F2, TH_F3, TH_CL, TH_C, TH_D, TH_B, TH_G };

typedef struct {
    int n_fuel_pins, n_zrh_pins;
    double pellet_r, clad_ir, clad_or, zrh_r, pin_pitch;   /* m */
    double flow_area, bypass_area;                         /* m2 per channel */
    double dh_bundle, dh_bypass;                           /* m */
    double duct_perim_in, duct_perim_out, duct_area;       /* m, m, m2 */
    double graphite_area, graphite_perim;                  /* m2, m */
    double graphite_gap_h;                                 /* W/m2/K across the He gap in the can */
    double frac_fuel, frac_zrh, frac_graphite, frac_coolant, frac_duct; /* power deposition */
} rm_channel_geom;

typedef struct {
    int nch, nz;
    double dz;                     /* m */
    rm_channel_geom geo;

    /* state, [nz][nch] */
    double *T[RM_TH_NCHAIN];
    double *T_zrh;
    double *h_cool, *h_byp;        /* sodium enthalpy (J/kg) */
    double *void_frac;             /* bundle void fraction */
    double *q_node;                /* power deposited in node (W), input */
    double *T_center;              /* fuel centreline estimate (K) */

    /* per channel */
    double *W, *W_byp;             /* kg/s */
    double *T_in;                  /* inlet temperature (K) */
    double *T_out, *T_out_byp;     /* outlet temperatures (K) */
    double *T_mixed_out;           /* bundle+bypass mixed */
    double p_out;                  /* outlet plenum pressure (Pa) */
    double p_in;

    /* scratch */
    double *a, *b, *c, *d, *x, *w;
    double *s_cpc, *s_cpb, *s_zfac, *s_zrhs;   /* [nch] */
} rm_coreth;

int rm_coreth_init(rm_coreth *th, int nch, int nz, double height, const rm_channel_geom *geo);
void rm_coreth_free(rm_coreth *th);
/* Set every node to an isothermal state. */
void rm_coreth_isothermal(rm_coreth *th, double T);
/* Advance by dt with node powers q_node. */
void rm_coreth_step(rm_coreth *th, double dt);
/* Channel pressure drop (Pa) for a given channel flow, used by the loop
 * hydraulics. Includes friction, orifice/grid form losses, gravity. */
double rm_coreth_dp(const rm_coreth *th, int ch, double W);

static inline int th_idx(const rm_coreth *th, int ch, int z) { return z * th->nch + ch; }

#endif
