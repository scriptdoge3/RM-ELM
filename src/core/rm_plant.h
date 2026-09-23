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
    int isolated;             /* feed and steam valves shut, water side blown down */
    /* sodium-water reaction */
    double leak;              /* water/steam leaking into the sodium, kg/s */
    double h2;                /* hydrogen in the secondary sodium, ppm (hydrogen meter) */
    int disc_burst;           /* sodium-side rupture disc has gone */
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
    int auto_rod;             /* regulating bank holds reactor power at power_set */
    double power_set;         /* fraction of rated */

    /* electrical */
    int offsite_power;        /* grid connection available */
    int diesel_avail[3];      /* diesel generator not failed */
    int diesel_running[3];
    double diesel_timer;      /* s since loss of offsite power */
    double fw_pump;           /* feedwater pump speed 0..1 (motor driven) */
    int fw_on;                /* feedwater system in service */
    double cw_pump;           /* circulating water pump speed 0..1 */

    /* decay heat removal: three natural-draft sodium-to-air coolers */
    int dracs_auto;           /* dampers open automatically on reactor trip */
    double dracs_damper[3];   /* 0..1 */
    double dracs_damper_set[3];
    double Q_dracs;           /* W removed */
    double W_dracs;           /* in-vessel natural circulation through the DRACS coolers, kg/s */
    double h_dracs_out;       /* sodium enthalpy returning from the coolers to the inlet plenum */
    double T_air;             /* K */

    /* reactor protection system */
    int rps_bypass;           /* 1 = trips disabled (for training/accident scenarios) */
    char first_out[48];       /* first trip signal to actuate */
    double period;            /* reactor period estimate, s */
    double n_last;

    /* event messages for the operator (ring buffer, nmsg counts all ever posted) */
    char msg[16][72];
    unsigned nmsg;

    double t;
} rm_plant;

/* Nominal values used by displays and protection. */
double rm_plant_nominal_flow(void);   /* total primary, kg/s */
int rm_plant_essential_power(const rm_plant *p);

int rm_plant_init(rm_plant *p);
void rm_plant_free(rm_plant *p);
/* Converge the whole plant to steady full power. */
void rm_plant_steady(rm_plant *p);
void rm_plant_step(rm_plant *p, double dt);
void rm_plant_manual_scram(rm_plant *p);
/* Start from hot shutdown instead: every rod in, sodium isothermal at 380 C
 * with the pumps running, feedwater off, turbine tripped. */
void rm_plant_hot_standby(rm_plant *p);
/* Shut an SG's feed and steam isolation valves and blow its water side down. */
void rm_plant_isolate_sg(rm_plant *p, int loop);
/* Post an operator message. */
void rm_plant_msg(rm_plant *p, const char *fmt, ...);
/* Thresholds shown on the panels */
#define RM_H2_ALARM 0.30      /* ppm, hydrogen-in-sodium high */
#define RM_H2_BACKGROUND 0.08

double rm_na_T_of_h(double h);

#endif
