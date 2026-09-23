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
    double q_ext;             /* heat added from outside (trace heaters minus losses), W */
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
    int fiv_open, msiv_open;  /* feedwater and main steam isolation valves */
    /* sodium-water reaction */
    double leak;              /* water/steam leaking into the sodium, kg/s */
    double h2;                /* hydrogen in the secondary sodium, ppm (hydrogen meter) */
    int disc_burst;           /* sodium-side rupture disc has gone */
    int n2_purge;             /* nitrogen purge of the blown-down water side */
    int fw_manual;            /* this SG's feed regulating valve station in MANUAL */
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
    /* secondary sodium system */
    double sec_inventory;     /* fraction of the loop's sodium present */
    int dumped;               /* drained to the dump tank */
    double refill_t;          /* s left of a refill, 0 = none */
    double na_leak;           /* sodium leaking out of the secondary pipework, kg/s */
    double exp_level;         /* expansion tank level, % */
    double exp_p;             /* expansion tank argon pressure, Pa */
    int cold_trap;            /* secondary cold trap in service */
    double oxygen;            /* ppm */
    double brg_T;             /* primary pump bearing temperature, C */
} rm_loop;

/* ---- reactor protection system and neutron monitoring ----------------------- */
enum { RPS_A1, RPS_A2, RPS_B1, RPS_B2 };
typedef struct {
    int trip[4];              /* channel trip relays de-energised */
    int bypass[4];            /* channel bypass keys (one per division) */
    int div_trip[2];          /* division A / B tripped (a half scram if only one) */
    char div_why[2][48];
    int irm_range[8];         /* IRM A..H range switches, 1..10 */
    int irm_bypass[2];        /* bypassed IRM channel per division, -1 = none */
    int aprm_bypass[2];       /* bypassed APRM channel per division, -1 = none */
    double srm_pos, irm_pos;  /* detector position, 0 = withdrawn .. 1 = fully in */
    int srm_drive, irm_drive; /* -1 withdrawing, 0 stopped, +1 inserting */
    double quad[4];           /* quadrant power, mean = 1 */
    int rwm_bypass;           /* rod worth minimizer bypass key */
    int sel_rod;              /* rod selected on the rod select matrix, -1 none */
    double rbm, rbm_ref;      /* rod block monitor: local power around the selected rod */
    int rbm_bypass;
} rm_nms;

/* ---- turbine-generator, feed, condenser and electrical ---------------------- */
typedef struct {
    double speed;             /* rpm, 1800 synchronous */
    double speed_target;      /* rpm */
    double speed_ref;         /* ramping governor reference, rpm */
    double accel;             /* rpm per minute */
    int auto_sync;            /* roll to speed and close the breaker in sync */
    int turning_gear;
    double ecc;               /* rotor eccentricity, mils */
    double vib;               /* shaft vibration, mils */
    double brg_T;             /* hottest bearing metal, C */
    double lube_p;            /* bearing oil header, bar */
    int aux_oil_pump;
    int field_breaker;
    int avr_auto;
    double exc;               /* excitation, per unit */
    double gen_kv, gen_hz, phase, mvar;
    int tdfp[2];              /* turbine-driven feed pumps A, B (60% each) */
    int mdfp;                 /* motor-driven startup feed pump (25%) */
    double fw_cap;            /* running feed pump capacity, fraction of rated flow */
    int cond_pump[2];
    int heaters_in;           /* high-pressure feed heaters in service */
    double hotwell, da_level; /* m */
    int cw[3];                /* circulating water pumps */
    int vac_pump[2];
    double cond_air;          /* air in the condenser, kg */
    int aux_on_uat;           /* house buses on the unit auxiliary transformer (else startup transformer) */
    int transfer_fail;        /* latent fault: the fast bus transfer won't work */
    double batt[2];           /* battery state of charge 0..1, divisions A and B */
    int charger[2], inverter[2];
    double dg_mw[3];
} rm_tg;

/* ---- sodium auxiliaries, containment and support systems -------------------- */
typedef struct {
    double na_level;          /* reactor vessel sodium level above nominal, mm */
    double gas_p;             /* argon cover gas pressure, Pa */
    int gas_auto;             /* argon supply/vent hold 0.12 MPa */
    int gas_supply, gas_vent; /* manual valves */
    double gas_act;           /* cover gas activity, MBq/m3 */
    double fuel_fail;         /* failed fuel (fraction of a pin-equivalent source) */
    double dnd;               /* delayed neutron detectors, cps */
    int cleanup;              /* cover gas cleanup (charcoal delay beds) */
    double prim_leak;         /* primary sodium into the guard vessel / cell, kg/s */
    double prim_inventory;    /* fraction */
    int prim_cold_trap;
    double prim_oxygen;       /* ppm */
    int heat_auto[6], heat_on[6];   /* trace heating circuits: primary, secondary 1-4, dump tanks */
    double heat_kw[6];
    double cell_o2;           /* primary cell oxygen, % */
    int n2_supply;
    double cell_T;            /* primary cell, C */
    double cont_p, cont_T;    /* reactor building, kPa, C */
    double rad[8];            /* radiation monitors (see RM_RAD_*) */
    int ccw[2], sw[2];        /* component cooling / service water pumps */
    double ccw_T;             /* C */
    int air_comp[2];
    double air_p;             /* instrument air, bar */
    int hvac_emerg;           /* control room HVAC on emergency filtration */
    double cr_T;
    int fire[8];              /* fire zones burning (see RM_FIRE_*) */
    double fire_t[8];         /* s the fire has burned */
    int fire_pump[2];
    int evacuated;            /* control room abandoned */
    int rsp_control;          /* remote shutdown panel has control */
} rm_aux;

enum { RM_RAD_CR, RM_RAD_HALL, RM_RAD_TURB, RM_RAD_SG, RM_RAD_GAS, RM_RAD_STACK, RM_RAD_SECNA, RM_RAD_STEAM };
enum { RM_FIRE_SG1, RM_FIRE_SG2, RM_FIRE_SG3, RM_FIRE_SG4, RM_FIRE_CELL, RM_FIRE_TURB, RM_FIRE_CABLE, RM_FIRE_CR };

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
    int offsite_power;        /* grid connected (grid_ok && grid_breaker), derived each step */
    int grid_ok;              /* the grid itself is up */
    int grid_breaker;         /* switchyard breaker closed */
    int diesel_manual[3];     /* operator start (test runs / pre-emptive start) */
    double diesel_t[3];       /* s since each diesel was called to start */
    int diesel_avail[3];      /* diesel generator not failed */
    int diesel_running[3];
    int diesel_ptl[3];        /* control switch in pull-to-lock: no automatic start */
    double diesel_timer;      /* s since loss of offsite power */
    double fw_pump;           /* feedwater pump speed 0..1 (motor driven) */
    int fw_on;                /* feedwater system in service */
    double cw_pump;           /* circulating water pump speed 0..1 */

    /* decay heat removal: three natural-draft sodium-to-air coolers */
    int dracs_auto;           /* dampers open automatically on reactor trip */
    double dracs_damper[3];   /* 0..1 */
    double dracs_damper_set[3];
    int dracs_man[3];         /* train's damper loading station in MANUAL: no auto-open on trip */
    double Q_dracs;           /* W removed */
    double Q_dracs_train[3];
    double W_dracs;           /* in-vessel natural circulation through the DRACS coolers, kg/s */
    double h_dracs_out;       /* sodium enthalpy returning from the coolers to the inlet plenum */
    double T_air;             /* K */

    int containment_isolated; /* penetrations shut, incl. the sodium purification (cold trap) lines */

    /* reactor mode switch and neutron monitoring */
    int mode;                 /* RM_MODE_* */
    rm_nms nms;
    rm_tg tg;
    rm_aux aux;

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
/* Reactor mode switch (BWR style). SHUTDOWN scrams and blocks withdrawal,
 * REFUEL allows single-rod moves only, STARTUP arms the APRM setdown (15%)
 * and IRM high trips, RUN restores the full-power trips and needs >5% APRM. */
enum { RM_MODE_SHUTDOWN, RM_MODE_REFUEL, RM_MODE_STARTUP, RM_MODE_RUN };
/* returns 0 and an explanation in why[] if the switch is refused */
int rm_plant_set_mode(rm_plant *p, int mode, char *why, int nwhy);
double rm_plant_srm_cps(const rm_plant *p);      /* SRM A, counts/s */
double rm_plant_srm_ch(const rm_plant *p, int ch);   /* SRM A..D */
double rm_plant_irm(const rm_plant *p);          /* IRM A reading on its range, 0..125 scale */
double rm_plant_irm_ch(const rm_plant *p, int ch);   /* IRM A..H */
double rm_plant_aprm(const rm_plant *p);         /* APRM average of channels in service, % */
double rm_plant_aprm_ch(const rm_plant *p, int ch);  /* APRM A..F */
/* Rod withdrawal block: returns 1 and the reason if withdrawal is blocked.
 * single = 1 when one rod is being moved from the rod select matrix. */
int rm_plant_rod_block(const rm_plant *p, int single, char *why, int nwhy);
/* Full withdrawal permissive for a bank (rod = -1) or one rod (bank = -1) to
 * target_cm: rod blocks, rod worth minimizer and rod block monitor. */
int rm_plant_rod_permit(const rm_plant *p, int bank, int rod, double target_cm, char *why, int nwhy);
void rm_plant_select_rod(rm_plant *p, int rod);
/* RPS: manual scram of one division (0 = A, 1 = B); reset a division */
void rm_plant_manual_scram_div(rm_plant *p, int div);
void rm_plant_rps_reset(rm_plant *p, int div);
/* Turbine: latch (start rolling) and close the generator breaker by hand.
 * Returns 0 with the reason if refused. */
int rm_plant_turbine_latch(rm_plant *p, char *why, int nwhy);
int rm_plant_gen_breaker_close(rm_plant *p, char *why, int nwhy);
void rm_plant_turbine_trip(rm_plant *p, const char *why);
/* Secondary loop: drain to the dump tank / refill */
void rm_plant_dump_loop(rm_plant *p, int loop);
void rm_plant_refill_loop(rm_plant *p, int loop);
double rm_plant_essential_load(const rm_plant *p);   /* W on the essential buses */
double rm_plant_plugging_T(const rm_plant *p, int loop);  /* plugging meter, C; loop -1 = primary */
#define RM_IRM_TRIP 120.0
#define RM_IRM_DOWNSCALE 5.0

/* Thresholds shown on the panels */
#define RM_H2_ALARM 0.30      /* ppm, hydrogen-in-sodium high */
#define RM_H2_BACKGROUND 0.08

double rm_na_T_of_h(double h);

#endif
