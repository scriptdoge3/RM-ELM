/*
 * Sodium auxiliaries, containment and support systems.
 *
 * - Reactor vessel sodium level (thermal expansion and inventory) and the
 *   argon cover gas, held at 0.12 MPa by supply/vent valves.
 * - Failed fuel detection: delayed neutron detectors in the primary sodium
 *   and cover gas activity. A failed pin grows at high power.
 * - Sodium leaks: primary into the inerted cell (guard vessel), secondary
 *   into the steam generator building where the sodium burns in air.
 * - Cold traps (oxygen and hydrogen removal) and their plugging meters.
 * - Trace heating against pipe heat losses (sodium freezes at 98 C).
 * - Containment: primary cell nitrogen/oxygen, reactor building pressure.
 * - Radiation monitors, component cooling and service water (primary pump
 *   bearing cooling), instrument air (air-operated valves fail when it
 *   goes), control room HVAC, fire detection and suppression, and the
 *   control room evacuation that a control room fire forces.
 */
#include "rm_plant_int.h"
#include "rm_sodium.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define NA_PRIM_MASS 800000.0

void rm_aux_init(rm_plant *p, int hot)
{
    rm_aux *a = &p->aux;
    memset(a, 0, sizeof *a);
    a->gas_p = 0.12e6;
    a->gas_auto = 1;
    a->gas_act = 1.0;
    a->dnd = 20.0;
    a->cleanup = 1;
    a->prim_inventory = 1.0;
    a->prim_cold_trap = 1;
    a->prim_oxygen = 3.0;
    for (int i = 0; i < 6; i++) a->heat_auto[i] = 1;
    a->cell_o2 = 1.0;
    a->n2_supply = 1;
    a->cell_T = 45.0;
    a->cont_p = 101.3;
    a->cont_T = 30.0;
    a->ccw[0] = a->ccw[1] = 1;
    a->sw[0] = a->sw[1] = 1;
    a->ccw_T = 30.0;
    a->air_comp[0] = a->air_comp[1] = 1;
    a->air_p = 7.0;
    a->cr_T = 22.0;
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &p->loop[i];
        l->sec_inventory = 1.0;
        l->dumped = 0;
        l->refill_t = 0;
        l->na_leak = 0;
        l->exp_level = 50.0;
        l->exp_p = 0.3e6;
        l->cold_trap = 1;
        l->oxygen = 3.0;
        l->brg_T = hot ? 50.0 : 60.0;
    }
    (void)hot;
    rm_aux_pipe_heat(p);
}

/* pipe heat losses and trace heating; circuit 0 = primary, 1..4 = secondary loops */
static void pipe_heat(rm_plant *p, rm_pipe *pp, int circ)
{
    rm_aux *a = &p->aux;
    double T = rm_na_T(pp->h[RM_PIPE_N - 1]);
    double loss = 400.0 * (T - 293.15);                   /* W: insulated pipe */
    int on = a->heat_on[circ] || (a->heat_auto[circ] && T < 473.15);
    double heat = on && p->aux.fire[RM_FIRE_CABLE] == 0 && rm_plant_essential_power(p) ? 150.0e3 : 0.0;
    pp->q_ext = heat - loss;
    a->heat_kw[circ] += heat / 1e3;
}

void rm_aux_pipe_heat(rm_plant *p)
{
    rm_aux *a = &p->aux;
    for (int c = 0; c < 6; c++) a->heat_kw[c] = 0;
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &p->loop[i];
        pipe_heat(p, &l->hot, 0);
        pipe_heat(p, &l->cold, 0);
        pipe_heat(p, &l->shot, 1 + i);
        pipe_heat(p, &l->scold, 1 + i);
    }
    /* dump tanks: kept at 200 C when their heaters are on */
    a->heat_kw[5] = (a->heat_on[5] || a->heat_auto[5]) && rm_plant_essential_power(p) ? 120.0 : 0.0;
}

static double plugging_temp(double oxygen) { return 105.0 + 25.0 * log2(fmax(oxygen, 0.5) / 2.0); }
double rm_plant_plugging_T(const rm_plant *p, int loop)
{
    return plugging_temp(loop < 0 ? p->aux.prim_oxygen : p->loop[loop].oxygen);
}

void rm_aux_step(rm_plant *p, double dt)
{
    rm_aux *a = &p->aux;
    rm_core *c = &p->core;
    double pw = c->p_fission / RM_P_RATED;
    int ess = rm_plant_essential_power(p);
    int live = p->offsite_power;

    rm_aux_pipe_heat(p);

    /* ---- reactor vessel level and cover gas ---- */
    double Tm = 0.5 * (p->T_core_in + p->T_core_out);
    a->prim_inventory = fmax(0.0, a->prim_inventory - a->prim_leak * dt / NA_PRIM_MASS);
    double lvl = 2.5 * (Tm - 653.15) + (a->prim_inventory - 1.0) * 8000.0;
    double dl = lvl - a->na_level;
    a->na_level = lvl;
    a->gas_p *= 1.0 + dl / 4000.0;                          /* level rise squeezes the gas */
    if (a->gas_auto) a->gas_p += (0.12e6 - a->gas_p) * fmin(1.0, dt / 60.0);
    else a->gas_p += (a->gas_supply ? 500.0 : 0.0) * dt - (a->gas_vent ? 800.0 : 0.0) * dt;
    if (a->gas_p < 0.101e6) a->gas_p = 0.101e6;

    /* ---- failed fuel ---- */
    if (a->fuel_fail > 0 && pw > 0.6) a->fuel_fail *= exp(dt * log(2.0) / 1200.0 * (pw - 0.6) / 0.4);
    a->dnd = 20.0 + 4000.0 * a->fuel_fail * fmax(pw, 0.0);
    double vent = a->gas_auto ? (a->gas_p > 0.121e6 ? 1.0 : 0.0) : a->gas_vent;
    double removal = (a->cleanup ? 1.0 / 1800.0 : 1.0 / 36000.0) + vent * 1.0 / 3600.0;
    a->gas_act += (2.0 * a->fuel_fail * fmax(pw, 0.0) + 0.01 - a->gas_act * removal) * dt;
    if (a->gas_act < 1.0) a->gas_act = 1.0;

    /* ---- sodium systems ---- */
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_loop *l = &p->loop[i];
        if (l->dumped) l->na_leak = 0;
        l->sec_inventory = fmax(0.0, l->sec_inventory - l->na_leak * dt / NA_SEC_MASS);
        double Ts = 0.5 * (rm_na_T(l->shot.h[RM_PIPE_N - 1]) + rm_na_T(l->scold.h[RM_PIPE_N - 1]));
        l->exp_level = l->dumped ? 0.0 : fmin(100.0, fmax(0.0, 50.0 + 0.2 * (Ts - 663.15) + (l->sec_inventory - 1.0) * 300.0));
        if (l->refill_t > 0) {
            l->refill_t -= dt;
            if (l->refill_t <= 0) {
                l->refill_t = 0;
                l->dumped = 0;
                l->sec_inventory = 1.0;
                l->sg.disc_burst = 0;
                l->sg.isolated = 0;
                l->sg.n2_purge = 0;
                l->sg.leak = 0;
                l->sg.h2 = RM_H2_BACKGROUND;
                l->spump.tripped = 0;
                l->spump.motor_on = 0;
                rm_plant_msg(p, "LOOP %d REFILLED - SG %d VALVES STILL SHUT", i + 1, i + 1);
            }
        }
        /* oxygen: in-leakage against the cold trap */
        int ct = l->cold_trap && !p->containment_isolated && !l->dumped;
        l->oxygen += (ct ? (2.0 - l->oxygen) / 10800.0 : 1.0 / 3600.0) * dt;
        /* primary pump bearings: cooled by component cooling water */
        int ccw_ok = (a->ccw[0] && ess) || (a->ccw[1] && live);
        double s = l->ppump.speed;
        double target = 50.0 + 12.0 * s * s + (ccw_ok ? 0.0 : 45.0 * s * s) + (a->ccw_T - 30.0) * 0.5;
        l->brg_T += (target - l->brg_T) * fmin(1.0, dt / 120.0);
        if (l->brg_T > 90.0 && l->ppump.motor_on && !l->ppump.tripped) {
            l->ppump.tripped = 1;
            rm_plant_msg(p, "PRIMARY PUMP %d TRIPPED - BEARING TEMPERATURE HIGH", i + 1);
        }
        /* a secondary sodium leak burns in the air of the SG building */
        if (l->na_leak > 0.005 && !a->fire[RM_FIRE_SG1 + i]) {
            a->fire[RM_FIRE_SG1 + i] = 1;
            a->fire_t[RM_FIRE_SG1 + i] = 0;
            rm_plant_msg(p, "SODIUM FIRE IN SG %d CELL", i + 1);
        }
    }
    int pct = a->prim_cold_trap && !p->containment_isolated;
    a->prim_oxygen += (pct ? (2.0 - a->prim_oxygen) / 10800.0 : 1.0 / 3600.0) * dt;

    /* ---- containment ---- */
    a->cell_o2 += (a->n2_supply ? (1.0 - a->cell_o2) / 7200.0 : 0.3 / 3600.0) * dt;
    double cell_heat = a->prim_leak * 2000.0 + (a->fire[RM_FIRE_CELL] ? 5.0 : 0.0);
    a->cell_T += (45.0 + cell_heat - a->cell_T) * fmin(1.0, dt / 600.0);
    if (a->prim_leak > 0.002 && a->cell_o2 > 5.0 && !a->fire[RM_FIRE_CELL]) {
        a->fire[RM_FIRE_CELL] = 1;
        a->fire_t[RM_FIRE_CELL] = 0;
        rm_plant_msg(p, "SODIUM FIRE IN PRIMARY CELL - OXYGEN ABOVE 5%%");
    }
    double fires = 0;
    for (int f = 0; f < 8; f++) fires += a->fire[f];
    a->cont_p += (101.3 + 2.0 * a->fire[RM_FIRE_CELL] + 0.5 * fires - a->cont_p) * fmin(1.0, dt / 300.0);
    a->cont_T += (30.0 + 3.0 * fires - a->cont_T) * fmin(1.0, dt / 600.0);

    /* ---- radiation (uSv/h, gas and stack in MBq/m3) ---- */
    int leak_path = !p->containment_isolated;
    a->rad[RM_RAD_GAS] = a->gas_act;
    a->rad[RM_RAD_STACK] = leak_path ? 0.02 + a->gas_act * vent * 0.05 : 0.02;
    a->rad[RM_RAD_HALL] = 0.5 + 3.0 * fmax(pw, 0) + 0.02 * a->gas_act + 20.0 * a->prim_leak;
    a->rad[RM_RAD_TURB] = 0.2;
    a->rad[RM_RAD_SG] = 0.3 + 0.1 * fmax(pw, 0);
    a->rad[RM_RAD_SECNA] = 0.1;
    a->rad[RM_RAD_STEAM] = 0.05;
    a->rad[RM_RAD_CR] = 0.1 + (a->hvac_emerg ? 0.0 : 0.05 * a->rad[RM_RAD_STACK]);
    /* high stack or hall activity: containment isolates, control room goes to filtration */
    if ((a->rad[RM_RAD_STACK] > 50.0 || a->rad[RM_RAD_HALL] > 100.0) && !p->containment_isolated) {
        p->containment_isolated = 1;
        a->hvac_emerg = 1;
        rm_plant_msg(p, "HIGH RADIATION - AUTOMATIC CONTAINMENT ISOLATION");
    }

    /* ---- cooling water ---- */
    int sw_ok = (a->sw[0] && ess) || (a->sw[1] && live);
    int ccw_ok = (a->ccw[0] && ess) || (a->ccw[1] && live);
    double ccw_target = ccw_ok ? (sw_ok ? 30.0 : 70.0) : a->ccw_T;
    a->ccw_T += (ccw_target - a->ccw_T) * fmin(1.0, dt / 300.0);

    /* ---- instrument air: A on the essential bus, B on the normal bus ---- */
    double make = ((a->air_comp[0] && ess) + (a->air_comp[1] && live)) * 0.06;
    if (a->air_p > 7.2) make = 0;
    double prev = a->air_p;
    a->air_p = fmax(0.0, a->air_p + (make - 0.025) * dt);
    if (prev >= 4.0 && a->air_p < 4.0) rm_plant_msg(p, "INSTRUMENT AIR LOW - AIR-OPERATED VALVES FAILING");
    if (prev >= 3.0 && a->air_p < 3.0) {
        for (int i = 0; i < RM_NLOOPS; i++) p->loop[i].sg.msiv_open = 0;
        rm_plant_msg(p, "INSTRUMENT AIR LOST - MSIVS DRIFTED SHUT, DRACS DAMPERS FAILED OPEN");
    }

    /* ---- control room HVAC ---- */
    a->cr_T += ((ess ? 22.0 : 32.0) + (a->fire[RM_FIRE_CR] ? 8.0 : 0.0) - a->cr_T) * fmin(1.0, dt / 900.0);

    /* ---- fires ---- */
    int pump = (a->fire_pump[0] && ess) || a->fire_pump[1];   /* B is diesel driven */
    for (int f = 0; f < 8; f++) {
        if (!a->fire[f]) continue;
        a->fire_t[f] += dt;
        int out = 0;
        if (f <= RM_FIRE_SG4) {
            rm_loop *l = &p->loop[f - RM_FIRE_SG1];
            /* a sodium pool burns out ten minutes after the leak stops */
            if (l->na_leak <= 0.005 && a->fire_t[f] > 600.0) out = 1;
        } else if (f == RM_FIRE_CELL) {
            if (a->cell_o2 < 4.0) out = 1;
        } else if (f == RM_FIRE_CR) {
            if (a->fire_t[f] > 900.0) out = 1;
        } else {
            if (pump && a->fire_t[f] > 120.0) out = 1;        /* sprinklers */
            if (f == RM_FIRE_CABLE && a->fire_t[f] > 300.0 && !out) {
                /* burning cables start knocking things out */
                if (a->ccw[1]) {
                    a->ccw[1] = 0;
                    rm_plant_msg(p, "CABLE FIRE DAMAGE - CCW PUMP B LOST");
                }
                if (p->tg.cw[2]) {
                    p->tg.cw[2] = 0;
                    rm_plant_msg(p, "CABLE FIRE DAMAGE - CIRCULATING WATER PUMP C LOST");
                }
            }
            if (f == RM_FIRE_TURB && !p->turbine_tripped) rm_plant_turbine_trip(p, "TURBINE HALL FIRE");
        }
        if (out) {
            a->fire[f] = 0;
            static const char *zone[8] = {"SG 1 CELL", "SG 2 CELL", "SG 3 CELL", "SG 4 CELL", "PRIMARY CELL",
                                          "TURBINE HALL", "CABLE SPREADING ROOM", "CONTROL ROOM"};
            rm_plant_msg(p, "FIRE OUT - %s", zone[f]);
        }
    }
    /* a control room fire drives the crew out after a minute of smoke */
    if (a->fire[RM_FIRE_CR] && a->fire_t[RM_FIRE_CR] > 60.0 && !a->evacuated) {
        a->evacuated = 1;
        rm_plant_msg(p, "CONTROL ROOM EVACUATED - GO TO THE REMOTE SHUTDOWN PANEL");
    }
}
