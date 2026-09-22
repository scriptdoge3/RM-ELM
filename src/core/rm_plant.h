/*
 * The whole plant: the core plus four primary sodium loops, four
 * intermediate sodium loops, four once-through steam generators, the steam
 * header, turbine-generator, condenser and feedwater.
 *
 *   outlet plenum -> hot leg -> IHX shell -> pump -> check valve -> cold leg -> inlet plenum -> core
 *   IHX tubes -> secondary hot leg -> SG shell -> secondary cold leg -> secondary pump -> IHX tubes
 *   feedwater -> SG tubes (economiser/evaporator/superheater) -> steam header -> turbine / bypass
 *
 * Primary loop flows come from a momentum balance per loop with a shared
 * core pressure drop, pump head, loop friction and thermal buoyancy, so
 * natural circulation follows on its own after a pump trip. Sodium and
 * water/steam are tracked by enthalpy (sodium with freezing, water with
 * IAPWS-IF97).
 */
#ifndef RM_PLANT_H
#define RM_PLANT_H

#include "rm_core.h"

#define RM_NLOOPS 4
#define RM_PIPE_N 4
#define RM_HX_N 10

typedef struct {
    double h[RM_PIPE_N];      /* sodium enthalpy per node */
    double mass;              /* total sodium mass, kg */
    double wall_T[RM_PIPE_N];
    double wall_C;            /* wall heat capacity per node, J/K */
} rm_pipe;

typedef struct {
    double hp[RM_HX_N];       /* primary (shell) sodium enthalpy, in primary flow order */
    double hs[RM_HX_N];       /* secondary (tube) sodium enthalpy, in secondary flow order */
    double Tw[RM_HX_N];       /* tube wall, indexed like hp */
    double UA0;               /* design UA, W/K */
    double mp, ms, mw_C;      /* node sodium masses (kg), wall heat capacity per node */
    double Q;                 /* heat transferred, W */
} rm_ihx;

typedef struct {
    double hna[RM_HX_N];      /* sodium enthalpy, sodium flow order (top to bottom) */
    double hw[RM_HX_N];       /* water/steam enthalpy, water flow order (bottom to top) */
    double Tw[RM_HX_N];       /* tube wall, indexed like hna */
    double area;              /* heat transfer area, m2 */
    double mna, mw_C;
    double mwater[RM_HX_N];   /* water inventory per node */
    double W_fw, W_steam;     /* kg/s */
    double T_steam, h_steam;  /* outlet */
    double Q;
    double fw_valve;          /* 0..1 */
    int isolated;
} rm_sg;

typedef struct {
    double speed;             /* fraction of rated */
    double speed_set;
    int motor_on;             /* main motor */
    int pony_on;              /* 10% pony motor */
    int tripped;
} rm_pump;

typedef struct {
    /* primary side */
    rm_pipe hot, cold;
    rm_ihx ihx;
    rm_pump ppump;
    double W;                 /* primary flow, kg/s */
    int check_valve_stuck_open;
    /* secondary side */
    rm_pipe shot, scold;
    rm_pump spump;
    double Ws;
    rm_sg sg;
} rm_loop;

typedef struct {
    rm_core core;
    rm_loop loop[RM_NLOOPS];

    /* reactor vessel */
    double h_inplenum, m_inplenum;
    double h_outplenum, m_outplenum;
    double W_core;
    double T_core_in, T_core_out;
    double cover_gas_p;       /* Pa */

    /* steam side */
    double p_header;          /* Pa */
    double m_header, V_header;
    double h_header;
    double turbine_valve;     /* 0..1 */
    double bypass_valve;      /* 0..1 */
    int turbine_tripped;
    int generator_breaker;
    double W_turbine, W_bypass, W_relief;
    double P_mech, P_gen, P_house, P_net;   /* W */
    double p_cond, T_fw;
    double T_cw_in;

    /* automatic controls */
    int auto_fw;              /* feedwater holds steam temperature */
    int auto_turbine;         /* turbine valve holds header pressure */
    double p_set, T_steam_set;
    double fw_int[RM_NLOOPS], tv_int;

    double t;
} rm_plant;

int rm_plant_init(rm_plant *p);
void rm_plant_free(rm_plant *p);
/* Converge the whole plant to steady full power. */
void rm_plant_steady(rm_plant *p);
void rm_plant_step(rm_plant *p, double dt);

double rm_na_T_of_h(double h);

#endif
