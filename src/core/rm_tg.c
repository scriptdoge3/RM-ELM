/*
 * Turbine-generator, feedwater and condensate, condenser and the electrical
 * system.
 *
 * Turbine: a rotor energy balance (about 4 GJ stored at 1800 rpm) with a
 * speed governor that ramps its reference at the selected acceleration
 * while rolling off line, overspeed trip at 110%, lube oil from the shaft
 * pump (or the AC auxiliary / DC emergency pumps at low speed), bearing
 * metal temperatures, rotor eccentricity (a bow that grows while the rotor
 * sits still off the turning gear) and vibration that peaks through the
 * critical speed. On line the grid holds the speed.
 *
 * Generator: field breaker and excitation with an automatic voltage
 * regulator, a synchroscope (phase slipping at the frequency difference)
 * and a sync check on closing the breaker; out-of-phase closure trips the
 * unit.
 *
 * Feed: two 60% turbine-driven feed pumps (need steam) and a 25% motor-
 * driven startup pump, fed by two condensate pumps. Condenser: three
 * circulating water pumps and two vacuum pumps against air in-leakage.
 *
 * Electrical: the house buses run from the unit auxiliary transformer when
 * the generator is on line and fast-transfer to the startup transformer
 * when it trips; batteries (two divisions) with chargers and vital
 * inverters; the diesels need DC to start.
 */
#include "rm_plant_int.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define RPM0 1800.0
#define E_ROTOR 4.0e9          /* J stored at synchronous speed */
#define GRID_KV 22.0

void rm_tg_init(rm_plant *p, int hot)
{
    rm_tg *t = &p->tg;
    memset(t, 0, sizeof *t);
    t->accel = 120.0;
    t->avr_auto = 1;
    t->heaters_in = !hot;
    t->hotwell = 1.0;
    t->da_level = 2.0;
    t->cond_pump[0] = 1;
    t->cond_pump[1] = !hot;
    t->cw[0] = t->cw[1] = t->cw[2] = 1;
    t->vac_pump[0] = 1;
    t->cond_air = 0.13;
    for (int d = 0; d < 2; d++) {
        t->batt[d] = 1.0;
        t->charger[d] = t->inverter[d] = 1;
    }
    t->ecc = 0.6;
    t->vib = 1.0;
    t->lube_p = 2.2;
    if (hot) {
        t->speed = 3.0;
        t->turning_gear = 1;
        t->aux_oil_pump = 1;
        t->mdfp = 1;
        t->brg_T = 40.0;
        t->aux_on_uat = 0;
    } else {
        t->speed = t->speed_target = t->speed_ref = RPM0;
        t->field_breaker = 1;
        t->exc = 1.02;
        t->tdfp[0] = t->tdfp[1] = 1;
        t->brg_T = 75.0;
        t->aux_on_uat = 1;
    }
    t->gen_hz = 60.0 * t->speed / RPM0;
}

void rm_plant_turbine_trip(rm_plant *p, const char *why)
{
    if (p->turbine_tripped) return;
    p->turbine_tripped = 1;
    p->generator_breaker = 0;
    p->tg.auto_sync = 0;
    rm_plant_msg(p, "TURBINE TRIP - %s", why);
}

int rm_plant_turbine_latch(rm_plant *p, char *why, int nwhy)
{
    rm_tg *t = &p->tg;
    const char *r = NULL;
    if (!p->turbine_tripped) r = "TURBINE ALREADY LATCHED";
    else if (p->core.scram) r = "LATCH BLOCKED: REACTOR TRIPPED";
    else if (t->lube_p < 1.0) r = "LATCH BLOCKED: BEARING OIL PRESSURE LOW - START AUX OIL PUMP";
    else if (t->ecc > 2.0) r = "LATCH BLOCKED: ECCENTRICITY HIGH - TURNING GEAR FIRST";
    else if (p->p_header < 5.0e6) r = "LATCH BLOCKED: STEAM PRESSURE BELOW 5 MPA";
    else if (p->p_cond > 15.0e3) r = "LATCH BLOCKED: CONDENSER VACUUM LOW";
    if (r) {
        snprintf(why, nwhy, "%s", r);
        return 0;
    }
    p->turbine_tripped = 0;
    p->generator_breaker = 0;
    t->speed_ref = t->speed;
    if (t->speed_target < t->speed) t->speed_target = t->speed;
    rm_plant_msg(p, "TURBINE LATCHED - STOP VALVES OPEN, ROLLING ON SPEED CONTROL");
    return 1;
}

static int in_sync(const rm_tg *t)
{
    return t->field_breaker && fabs(t->gen_hz - 60.0) < 0.2 && fabs(t->phase) < 10.0 && fabs(t->gen_kv - GRID_KV) < 1.5;
}

int rm_plant_gen_breaker_close(rm_plant *p, char *why, int nwhy)
{
    rm_tg *t = &p->tg;
    if (p->generator_breaker) return 1;
    if (p->turbine_tripped || !p->grid_ok || !p->grid_breaker) {
        snprintf(why, nwhy, "BREAKER CLOSE BLOCKED: %s", p->turbine_tripped ? "TURBINE TRIPPED" : "NO GRID");
        return 0;
    }
    if (!t->field_breaker || t->speed < 1700.0) {
        snprintf(why, nwhy, "BREAKER CLOSE BLOCKED: GENERATOR NOT UP TO SPEED AND VOLTAGE");
        return 0;
    }
    if (!in_sync(t)) {
        /* closing out of phase slams the rotor into step with the grid */
        rm_plant_msg(p, "GENERATOR CLOSED OUT OF PHASE (%.0f DEG) - GENERATOR PROTECTION TRIP", t->phase);
        rm_plant_turbine_trip(p, "GENERATOR DIFFERENTIAL");
        t->vib += 6.0;
        snprintf(why, nwhy, "OUT OF PHASE CLOSURE");
        return 0;
    }
    p->generator_breaker = 1;
    p->tv_int = -2.0;
    t->auto_sync = 0;
    rm_plant_msg(p, "GENERATOR BREAKER CLOSED - UNIT SYNCHRONISED");
    return 1;
}

static void turbine(rm_plant *p, double dt)
{
    rm_tg *t = &p->tg;
    int latched = !p->turbine_tripped;
    double s = t->speed / RPM0;
    if (p->generator_breaker) {
        t->speed = RPM0;
        t->speed_ref = RPM0;
    } else {
        /* rotor energy balance */
        double P_in = latched ? p->P_mech : 0.0;
        double loss = 2.0e6 * s * s * s + 0.3e6 * s + (t->speed > 1 ? 0.05e6 : 0.0);
        double E = E_ROTOR * s * s + (P_in - loss) * dt;
        t->speed = RPM0 * sqrt(fmax(E, 0.0) / E_ROTOR);
        if (t->turning_gear && t->speed < 3.0) t->speed = 3.0;
        if (t->speed > 10.0 && t->turning_gear) {
            t->turning_gear = 0;
            rm_plant_msg(p, "TURNING GEAR DISENGAGED");
        }
        if (latched) {
            /* governor: ramp the reference, valves follow the speed error */
            double target = t->auto_sync ? RPM0 + 1.5 : t->speed_target;
            double d = target - t->speed_ref, r = t->accel / 60.0 * dt;
            t->speed_ref += fabs(d) <= r ? d : (d > 0 ? r : -r);
            double err = (t->speed_ref - t->speed) / RPM0;
            double v = 0.004 * s * s * s + 0.02 * s + 1.5 * err;
            p->turbine_valve = fmin(0.25, fmax(0.0, v));
        }
    }
    s = t->speed / RPM0;
    if (latched && t->speed > 1.10 * RPM0) rm_plant_turbine_trip(p, "OVERSPEED 110%");

    /* lube oil: shaft pump at speed, AC auxiliary pump, DC emergency pump as backup */
    double shaft = 2.4 * s * s;
    double aux = t->aux_oil_pump && p->offsite_power ? 2.0 : 0.0;
    double emerg = (shaft < 0.9 && aux < 0.5 && t->batt[0] > 0.05) ? 1.3 : 0.0;
    t->lube_p = fmax(shaft, fmax(aux, emerg));
    double brg_target = 40.0 + 35.0 * s * s + (t->lube_p < 0.6 && t->speed > 30 ? 150.0 : 0.0);
    t->brg_T += (brg_target - t->brg_T) * fmin(1.0, dt / 60.0);

    /* rotor bow: grows at standstill off the turning gear, straightens on it */
    if (t->speed < 5.0 && !t->turning_gear) t->ecc = fmin(12.0, t->ecc + 0.3 / 60.0 * dt);
    else if (t->turning_gear) t->ecc += (0.5 - t->ecc) * fmin(1.0, dt / 1200.0);
    else if (t->speed > 600.0) t->ecc += (0.6 - t->ecc) * fmin(1.0, dt / 300.0);
    double res = exp(-pow((t->speed - 1100.0) / 150.0, 2.0));
    double vib = t->speed > 30 ? 0.5 + 0.6 * s * s + t->ecc * (0.4 + 4.0 * res) : 0.0;
    t->vib += (vib - t->vib) * fmin(1.0, dt / 5.0);

    if (latched) {
        if (t->vib > 7.0) rm_plant_turbine_trip(p, "HIGH VIBRATION");
        else if (t->brg_T > 107.0) rm_plant_turbine_trip(p, "BEARING TEMPERATURE HIGH");
        else if (t->lube_p < 0.6 && t->speed > 200.0) rm_plant_turbine_trip(p, "LOW BEARING OIL PRESSURE");
        else if (p->p_cond > 25.0e3) rm_plant_turbine_trip(p, "LOW CONDENSER VACUUM");
    }
    if (p->turbine_tripped) p->turbine_valve = 0.0;

    /* generator */
    if (!t->field_breaker) t->exc = 0.0;
    else if (p->generator_breaker) {
        if (t->avr_auto) t->exc += (1.02 - t->exc) * fmin(1.0, dt / 5.0);
    } else if (t->avr_auto) {
        double want = s > 0.3 ? 1.0 / s : 0.0;
        t->exc += (fmin(want, 1.3) - t->exc) * fmin(1.0, dt / 3.0);
    }
    if (p->generator_breaker) {
        t->gen_hz = 60.0;
        t->gen_kv = GRID_KV;
        t->phase = 0.0;
        t->mvar = 3000.0 * (t->exc - 1.0);
    } else {
        t->gen_hz = 60.0 * s;
        t->gen_kv = GRID_KV * s * fmax(t->exc, 0.02);
        t->phase += 360.0 * (t->gen_hz - 60.0) * dt;
        t->phase = fmod(t->phase + 540.0, 360.0) - 180.0;
        t->mvar = 0.0;
        if (t->auto_sync && latched && in_sync(t) && p->grid_ok && p->grid_breaker) {
            char why[80];
            rm_plant_gen_breaker_close(p, why, sizeof why);
        }
    }
}

static void electrical(rm_plant *p, double dt)
{
    rm_tg *t = &p->tg;
    int grid = p->grid_ok && p->grid_breaker;
    if (!grid && p->generator_breaker) {
        /* the generator can't hold an island here */
        rm_plant_turbine_trip(p, "LOSS OF GRID");
        p->generator_breaker = 0;
    }
    /* fast bus transfer off the unit auxiliary transformer when the generator goes */
    int uat_ok = p->generator_breaker && t->speed > 1700.0;
    if (t->aux_on_uat && !uat_ok) {
        if (grid && !t->transfer_fail) {
            t->aux_on_uat = 0;
            rm_plant_msg(p, "FAST BUS TRANSFER TO STARTUP TRANSFORMER");
        } else if (p->offsite_power) {
            rm_plant_msg(p, "BUS TRANSFER FAILED - HOUSE BUSES DEAD");
        }
    }
    int live = t->aux_on_uat ? uat_ok : grid;
    p->offsite_power = live;
    p->diesel_timer = live ? 0.0 : p->diesel_timer + dt;
    /* diesels start on bus undervoltage (or by hand); they need DC to crank */
    int running = 0;
    for (int i = 0; i < 3; i++) {
        int called = (!live || p->diesel_manual[i]) && !p->diesel_ptl[i];
        int dc = t->batt[i == 1 ? 1 : 0] > 0.05;
        p->diesel_t[i] = (called && p->diesel_avail[i] && (dc || p->diesel_t[i] > 10.0)) ? p->diesel_t[i] + dt : 0.0;
        p->diesel_running[i] = p->diesel_t[i] > 10.0;
        running += p->diesel_running[i];
    }
    double ess_load = rm_plant_essential_load(p);
    for (int i = 0; i < 3; i++)
        t->dg_mw[i] = p->diesel_running[i] ? (live ? 0.3 : ess_load / fmax(running, 1) / 1e6) : 0.0;
    /* batteries: chargers on the essential buses */
    int ess = rm_plant_essential_power(p);
    for (int d = 0; d < 2; d++) {
        if (t->charger[d] && ess) t->batt[d] = fmin(1.0, t->batt[d] + dt / 7200.0);
        else t->batt[d] = fmax(0.0, t->batt[d] - dt / (4.0 * 3600.0) * (t->inverter[d] ? 1.0 : 0.5));
    }

    /* feed pumps: condensate pumps must be running; the turbine-driven pumps need steam */
    int cond = live && (t->cond_pump[0] || t->cond_pump[1]);
    double cap = 0;
    if (cond) {
        for (int i = 0; i < 2; i++) cap += t->tdfp[i] && p->p_header > 4.0e6 ? 0.6 : 0.0;
        cap += t->mdfp && live ? 0.25 : 0.0;
    }
    t->fw_cap = cap;
    p->fw_pump += (cap - p->fw_pump) * fmin(1.0, dt / (cap > p->fw_pump ? 10.0 : 4.0));
    t->hotwell += ((cond ? 1.0 : 1.0 + fmin(1.0, p->W_turbine / rm_W_T0)) - t->hotwell) * fmin(1.0, dt / 120.0);
    t->da_level += ((cap > 0 || !cond ? 2.0 : 2.4) - t->da_level) * fmin(1.0, dt / 120.0);

    /* circulating water and vacuum */
    int ncw = 0, nvac = 0;
    for (int i = 0; i < 3; i++) ncw += t->cw[i];
    for (int i = 0; i < 2; i++) nvac += t->vac_pump[i];
    double cw_target = live ? ncw / 3.0 : 0.0;
    p->cw_pump += (cw_target - p->cw_pump) * fmin(1.0, dt / 20.0);
    double removal = live ? nvac * 0.03 * t->cond_air : 0.0;
    t->cond_air = fmax(0.0, t->cond_air + (0.004 - removal) * dt);
}

void rm_tg_step(rm_plant *p, double dt)
{
    electrical(p, dt);
    turbine(p, dt);
}
