/*
 * The RM-ELM core: 3D neutronics coupled to channel thermal-hydraulics.
 *
 * Layout: hexagonal lattice, 16 cm pitch. Rings 0-11 hold 397 channel
 * positions (fuel channels and control channels), rings 12-13 are canned
 * graphite reflector blocks. Axially: 3 lower zone nodes (shield, grid),
 * 20 active nodes over 1.60 m, 3 upper zone nodes (fission gas plenum).
 *
 * Time stepping (quasi-static): cross sections are rebuilt from the local
 * thermal and isotopic state every step; the importance-weighted
 * reactivity of the current flux shape drives the point-kinetics amplitude
 * (assembly integrator, with in-step Doppler feedback); the flux shape gets
 * one warm-started power iteration per step and the adjoint is refreshed
 * every couple of seconds.
 *
 * Control rods follow the post-1986 RBMK arrangement: a B4C absorber section
 * above a graphite displacer as long as the core, so a withdrawn rod leaves
 * graphite (not sodium) filling the channel over the whole core height.
 */
#ifndef RM_CORE_H
#define RM_CORE_H

#include "rm_coreth.h"
#include "rm_diffusion.h"
#include "rm_kernels.h"
#include "rm_xs.h"

#define RM_CORE_RINGS 11
#define RM_REFL_RINGS 2
#define RM_NZ_BOT 3
#define RM_NZ_ACT 20
#define RM_NZ_TOP 3
#define RM_NZ (RM_NZ_BOT + RM_NZ_ACT + RM_NZ_TOP)
#define RM_ACTIVE_H 160.0          /* cm */
#define RM_PITCH 16.0              /* cm */
#define RM_P_RATED 2300.0e6        /* W thermal */
#define RM_NDH 11                  /* decay heat groups */
/* burnable absorber in fresh fuel: a thin enriched-B4C wire in the bundle
 * (~0.2 cm2 per channel at the centre), homogenised B-10 density in
 * atoms/b-cm, zoned from centre to edge to flatten the radial power */
#define RM_BP_CENTRE 1.10e-4
#define RM_BP_EDGE 0.80e-5

enum { COL_FUEL, COL_CTRL, COL_REFL };
enum { BANK_REG, BANK_SHIM_A, BANK_SHIM_B, BANK_SHIM_C, BANK_SHIM_D, BANK_SAFETY, RM_NBANKS };
enum { ISO_I135, ISO_XE135, ISO_PM149, ISO_SM149, ISO_PA233, ISO_U233, ISO_U235, ISO_B10, RM_NISO };

typedef struct {
    rm_diff dif;
    rm_coreth th;

    int ncol;
    int *coltype;              /* per grid column */
    int *chan_of_col;          /* grid column -> fuel channel or -1 */
    int *col_of_chan;          /* fuel channel -> grid column */
    int *ctrl_of_col;          /* grid column -> control rod or -1 */
    int nchan, nctrl;
    int *ctrl_col, *ctrl_bank;
    double *rod_ins;           /* insertion depth of each rod, cm from core top (0 = out) */
    double *rod_vel;           /* cm/s, positive inwards */
    double bank_speed[RM_NBANKS];  /* cm/s for normal drive */
    double *rod_target;        /* cm, drive target */
    int scram;
    double scram_time;

    /* isotopes per fuel node [RM_NISO][nchan*RM_NZ_ACT] (atoms/b-cm, homogenised) */
    double *iso[RM_NISO];
    double *zrh_x;             /* H/Zr per fuel node */

    /* kinetics */
    rm_pk_params pk;
    rm_pk_state pks;
    double src;                /* neutron source in n/s units */
    double rho;                /* last reactivity */
    double rho_doppler_coef;   /* d rho / d T_fuel (1/K), refreshed periodically */
    double c_fuel;             /* total fuel heat capacity, J/K */
    double t_adjoint, t_doppler;
    int pk_substeps;

    /* decay heat */
    double dh[RM_NDH];

    /* powers (W) */
    double p_fission, p_decay, p_thermal;
    double *node_power;        /* [nn] fission+decay deposited, W */
    double *phi_abs[2];        /* [nchan*RM_NZ_ACT] absolute flux, n/cm2/s */
    double peak_factor;        /* max node power / mean fuel node power */

    double t;
} rm_core;

int rm_core_init(rm_core *c);
void rm_core_free(rm_core *c);

/* Build cross sections for all nodes from the current state. */
void rm_core_update_xs(rm_core *c);

/* Converge flux, adjoint and a critical rod configuration for a steady
 * state at the given fraction of rated power (thermal state held fixed
 * unless iterate_th). Returns the regulating-bank position found. */
double rm_core_steady(rm_core *c, double power_frac, int iterate_th, int with_xenon);

void rm_core_step(rm_core *c, double dt);

/* Rod control helpers */
void rm_core_bank_move(rm_core *c, int bank, double target_cm);
/* shift every rod of a bank by d cm, keeping individual offsets */
void rm_core_bank_shift(rm_core *c, int bank, double d_cm);
/* drive a single rod */
void rm_core_rod_move(rm_core *c, int rod, double target_cm);
double rm_core_bank_pos(const rm_core *c, int bank);
void rm_core_scram(rm_core *c);
void rm_core_reset_scram(rm_core *c);

/* mean temperatures for display/feedback bookkeeping */
double rm_core_mean_fuel_T(const rm_core *c);
double rm_core_max_fuel_T(const rm_core *c);
double rm_core_max_clad_T(const rm_core *c);

static inline int core_fnode(const rm_core *c, int ch, int zact) { return zact * c->nchan + ch; }

#endif
