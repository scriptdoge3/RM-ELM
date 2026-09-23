/* Internal to the plant model: design constants and the subsystem entry
 * points shared by rm_plant.c, rm_rps.c, rm_tg.c and rm_aux.c. */
#ifndef RM_PLANT_INT_H
#define RM_PLANT_INT_H

#include "rm_plant.h"

/* ---- design point ------------------------------------------------------ */
#define W_P0 (RM_P_RATED / (1270.0 * 160.0) / RM_NLOOPS)   /* primary kg/s per loop */
#define W_S0 2744.0                                         /* secondary kg/s per loop */
#define T_COLD0 653.15
#define T_HOT0 813.15
#define TS_COLD0 618.15
#define TS_HOT0 783.15
#define DP_CORE0 0.45e6
#define DP_LOOP0 0.15e6
#define H_THERMAL 8.0        /* m, core centre to IHX centre */
#define LOOP_INERTIA 150.0   /* sum of L/A per loop, 1/m */
#define P_STEAM0 14.0e6
#define T_STEAM0 753.15      /* 480 C */
#define T_FW0 513.15         /* 240 C */
#define V_HEADER 200.0
#define ETA_TURB 0.71
#define ETA_GEN 0.985
#define W_CW 40000.0
#define P_HOUSE0 45.0e6
#define SG_TUBE_VOL 10.0     /* m3 water side per SG */
#define G 9.80665
#define NA_SEC_MASS 68600.0  /* kg of sodium in one secondary loop */
#define SWR_HEAT 9.0e6       /* J per kg of water reacting with sodium */
#define DISC_BURST_LEAK 2.0  /* kg/s: the pressure pulse bursts the rupture disc */


extern double rm_W_T0;       /* nominal total steam flow, kg/s */

/* full reactor trip through both RPS divisions, first out = why */
void rm_plant_trip(rm_plant *p, const char *why);

void rm_rps_init(rm_plant *p, int hot);
void rm_rps_step(rm_plant *p, double dt);
void rm_rps_protection(rm_plant *p);

void rm_tg_init(rm_plant *p, int hot);
void rm_tg_step(rm_plant *p, double dt);

void rm_aux_init(rm_plant *p, int hot);
void rm_aux_step(rm_plant *p, double dt);
void rm_aux_pipe_heat(rm_plant *p);

#endif
