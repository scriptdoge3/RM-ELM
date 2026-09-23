#include "rm_game.h"

#include <math.h>
#include <string.h>

static double urand(rm_game *g)
{
    /* xorshift32 */
    unsigned x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x ? x : 0x9e3779b9u;
    return (g->rng & 0xffffff) / (double)0x1000000;
}

static double expo(rm_game *g, double mean) { return -mean * log(1.0 - urand(g) * 0.999); }

void rm_game_init(rm_game *g, const rm_plant *p, unsigned seed, int failures)
{
    memset(g, 0, sizeof *g);
    g->rng = seed ? seed : 12345u;
    g->failures = failures;
    g->next_failure = p->t + 300.0 + expo(g, 1200.0);
    g->was_scram = p->core.scram;
    g->was_turb_trip = p->turbine_tripped;
    if (p->generator_breaker) {
        g->on_line = 1;
        g->demand = g->demand_target = round(p->P_net / 1e6 / 10.0) * 10.0;
        g->next_order = p->t + 120.0;
    }
}

double rm_game_deviation(const rm_game *g, const rm_plant *p)
{
    if (!g->on_line) return 0.0;
    return (p->P_net / 1e6 - g->demand) / RM_MWE_RATED;
}

static void dispatch(rm_game *g, rm_plant *p)
{
    double t = 25.0 * round((450.0 + 450.0 * urand(g)) / 25.0);
    if (fabs(t - g->demand_target) < 50.0) t = t > 700 ? t - 150.0 : t + 150.0;
    double rate = 5.0 * round((5.0 + 15.0 * urand(g)) / 5.0);     /* MWe/min */
    g->demand_target = t;
    g->ramp = rate / 60.0;
    rm_plant_msg(p, "LOAD DISPATCH: GO TO %.0f MWE AT %.0f MWE/MIN", t, rate);
    g->next_order = p->t + 600.0 + 600.0 * urand(g);
}

static int pick_loop(rm_game *g) { return (int)(urand(g) * RM_NLOOPS) % RM_NLOOPS; }

/* One random equipment failure. Weights are relative; a pick that doesn't
 * apply right now (the pump is already off...) just does nothing. */
static void failure(rm_game *g, rm_plant *p)
{
    enum { F_SG_LEAK, F_SPUMP, F_PPUMP, F_TURB, F_DG, F_GRID, F_FREQ, F_ROD_DRIFT, F_TDFP, F_CW, F_VAC, F_CCW,
           F_AIR, F_FUEL, F_NA_LEAK, F_PRIM_LEAK, F_TRANSFER, F_CABLE_FIRE, F_TURB_FIRE, F_CR_FIRE, F_COLD_TRAP,
           F_VIB, F_N };
    static const double w[F_N] = {12, 8, 4, 5, 6, 3, 5, 6, 6, 5, 4, 4, 4, 5, 4, 1, 3, 2, 2, 0.7, 3, 3};
    double tot = 0;
    for (int k = 0; k < F_N; k++) tot += w[k];
    double r = urand(g) * tot;
    int f = 0;
    while (f < F_N - 1 && r >= w[f]) r -= w[f++];
    int i = pick_loop(g);
    rm_loop *l = &p->loop[i];
    rm_core *c = &p->core;
    switch (f) {
    case F_SG_LEAK:
        if (!l->sg.isolated && l->sg.leak == 0.0 && !l->dumped) l->sg.leak = 1e-4 * (0.5 + urand(g));
        break;   /* silent: the hydrogen meter is how you find out */
    case F_SPUMP:
        if (l->spump.motor_on && !l->spump.tripped) {
            l->spump.tripped = 1;
            rm_plant_msg(p, "SECONDARY PUMP %d TRIPPED - MOTOR OVERCURRENT", i + 1);
        }
        break;
    case F_PPUMP:
        if (l->ppump.motor_on && !l->ppump.tripped) {
            l->ppump.tripped = 1;
            rm_plant_msg(p, "PRIMARY PUMP %d TRIPPED - MOTOR OVERCURRENT", i + 1);
        }
        break;
    case F_TURB:
        if (!p->turbine_tripped) rm_plant_turbine_trip(p, "THRUST BEARING WEAR DETECTOR");
        break;
    case F_DG: {
        int d = (int)(urand(g) * 3) % 3;
        if (p->diesel_avail[d]) {
            p->diesel_avail[d] = 0;
            g->dg_back_at[d] = p->t + 1800.0 + 1800.0 * urand(g);
            rm_plant_msg(p, "DIESEL GENERATOR %d OUT OF SERVICE - FUEL RACK FAULT", d + 1);
        }
        break;
    }
    case F_GRID:
        if (p->grid_ok) {
            p->grid_ok = 0;
            g->grid_back_at = p->t + 600.0 + 1200.0 * urand(g);
            rm_plant_msg(p, "GRID FAULT - OFFSITE POWER LOST");
        }
        break;
    case F_FREQ:
        if (g->on_line && p->generator_breaker) {
            g->demand_target = fmin(RM_MWE_RATED, g->demand + 100.0);
            g->ramp = 30.0 / 60.0;
            rm_plant_msg(p, "LOAD DISPATCH: FREQUENCY LOW - RAISE TO %.0f MWE AT ONCE", g->demand_target);
        }
        break;
    case F_ROD_DRIFT: {
        int k = (int)(urand(g) * c->nctrl) % c->nctrl;
        if (c->rod_ins[k] < RM_ACTIVE_H - 1 && c->ctrl_bank[k] != BANK_SAFETY) c->rod_drift[k] = -0.15;
        break;   /* found by the ROD DRIFT alarm */
    }
    case F_TDFP: {
        int k = (int)(urand(g) * 2) % 2;
        if (p->tg.tdfp[k]) {
            p->tg.tdfp[k] = 0;
            rm_plant_msg(p, "FEED PUMP %c TRIPPED - LOW SUCTION PRESSURE", 'A' + k);
        }
        break;
    }
    case F_CW: {
        int k = (int)(urand(g) * 3) % 3;
        if (p->tg.cw[k]) {
            p->tg.cw[k] = 0;
            rm_plant_msg(p, "CIRCULATING WATER PUMP %c TRIPPED", 'A' + k);
        }
        break;
    }
    case F_VAC: {
        int k = (int)(urand(g) * 2) % 2;
        if (p->tg.vac_pump[k]) {
            p->tg.vac_pump[k] = 0;
            rm_plant_msg(p, "CONDENSER VACUUM PUMP %c TRIPPED", 'A' + k);
        }
        break;
    }
    case F_CCW: {
        int k = (int)(urand(g) * 2) % 2;
        if (p->aux.ccw[k]) {
            p->aux.ccw[k] = 0;
            rm_plant_msg(p, "COMPONENT COOLING WATER PUMP %c TRIPPED", 'A' + k);
        }
        break;
    }
    case F_AIR: {
        int k = (int)(urand(g) * 2) % 2;
        if (p->aux.air_comp[k]) {
            p->aux.air_comp[k] = 0;
            rm_plant_msg(p, "INSTRUMENT AIR COMPRESSOR %c TRIPPED", 'A' + k);
        }
        break;
    }
    case F_FUEL:
        if (p->aux.fuel_fail == 0) p->aux.fuel_fail = 0.03 + 0.05 * urand(g);
        break;   /* found by the delayed neutron detectors */
    case F_NA_LEAK:
        if (!l->dumped && l->na_leak == 0) l->na_leak = 0.01 + 0.04 * urand(g);
        break;   /* found by the leak and fire detectors */
    case F_PRIM_LEAK:
        if (p->aux.prim_leak == 0) p->aux.prim_leak = 0.5 + urand(g);
        break;
    case F_TRANSFER:
        p->tg.transfer_fail = 1;
        break;   /* latent: shows up at the next generator trip */
    case F_CABLE_FIRE:
        if (!p->aux.fire[RM_FIRE_CABLE]) {
            p->aux.fire[RM_FIRE_CABLE] = 1;
            p->aux.fire_t[RM_FIRE_CABLE] = 0;
            rm_plant_msg(p, "FIRE ALARM - CABLE SPREADING ROOM");
        }
        break;
    case F_TURB_FIRE:
        if (!p->aux.fire[RM_FIRE_TURB]) {
            p->aux.fire[RM_FIRE_TURB] = 1;
            p->aux.fire_t[RM_FIRE_TURB] = 0;
            rm_plant_msg(p, "FIRE ALARM - TURBINE HALL (LUBE OIL)");
        }
        break;
    case F_CR_FIRE:
        if (!p->aux.fire[RM_FIRE_CR]) {
            p->aux.fire[RM_FIRE_CR] = 1;
            p->aux.fire_t[RM_FIRE_CR] = 0;
            rm_plant_msg(p, "FIRE IN THE CONTROL ROOM - SMOKE IN THE MAIN CONTROL BOARD");
        }
        break;
    case F_COLD_TRAP:
        if (l->cold_trap) {
            l->cold_trap = 0;
            rm_plant_msg(p, "LOOP %d COLD TRAP OUT OF SERVICE - FLOW LOW", i + 1);
        }
        break;
    default:
        if (p->generator_breaker) {
            p->tg.ecc += 2.5;
            rm_plant_msg(p, "TURBINE BEARING 4 VIBRATION RISING");
        }
        break;
    }
}

void rm_game_step(rm_game *g, rm_plant *p, double dt)
{
    rm_core *c = &p->core;
    double t = p->t;

    /* dispatcher */
    if (!g->on_line && p->generator_breaker && p->P_gen > 1e6) {
        g->on_line = 1;
        g->demand = g->demand_target = fmax(50.0, round(p->P_net / 1e6 / 10.0) * 10.0);
        g->score += 200.0;
        rm_plant_msg(p, "LOAD DISPATCH: UNIT 1 SYNCHRONISED - WELCOME ON LINE");
        g->next_order = t + 60.0;
    }
    if (g->on_line) {
        if (t >= g->next_order) dispatch(g, p);
        double d = g->demand_target - g->demand;
        double s = g->ramp * dt;
        g->demand += fabs(d) <= s ? d : (d > 0 ? s : -s);
    }

    /* score */
    if (g->on_line && p->generator_breaker && p->P_net > 0) {
        double mwh = p->P_net / 1e6 * dt / 3600.0;
        g->mwh += mwh;
        double dev = fabs(rm_game_deviation(g, p));
        if (dev < 0.03) {
            g->score += mwh;
            g->on_target += dt;
        } else if (dev < 0.10) {
            g->score += 0.5 * mwh;
        }
    }
    if (c->scram && !g->was_scram) {
        g->trips++;
        g->score -= 500.0;
        rm_plant_msg(p, "SHIFT SUPERVISOR: REACTOR TRIP LOGGED (-500)");
    }
    g->was_scram = c->scram;
    if (p->turbine_tripped && !g->was_turb_trip && !c->scram && g->on_line) g->score -= 100.0;
    g->was_turb_trip = p->turbine_tripped;
    if (p->rps_bypass) g->score -= 1.0 * dt;
    if (rm_core_max_clad_T(c) > 923.15) g->score -= 2.0 * dt;
    for (int i = 0; i < RM_NLOOPS; i++) {
        rm_sg *s = &p->loop[i].sg;
        if (s->disc_burst && !g->was_burst[i]) {
            g->score -= 1000.0;
            rm_plant_msg(p, "SHIFT SUPERVISOR: SG %d LOST TO SODIUM-WATER REACTION (-1000)", i + 1);
        }
        g->was_burst[i] = s->disc_burst;
        if (s->leak > 0 && s->isolated && !s->disc_burst && !g->leak_seen[i]) {
            g->leak_seen[i] = 1;
            g->score += 300.0;
            rm_plant_msg(p, "SHIFT SUPERVISOR: LEAKING SG %d ISOLATED IN TIME (+300)", i + 1);
        }
    }

    /* fires and the control room evacuation */
    for (int f = 0; f < 8; f++) {
        if (p->aux.fire[f] && !g->was_fire[f] && f <= RM_FIRE_SG4) {
            g->score -= 200.0;
            rm_plant_msg(p, "SHIFT SUPERVISOR: SODIUM FIRE (-200)");
        }
        g->was_fire[f] = p->aux.fire[f];
    }
    if (p->aux.evacuated && !g->was_evac) {
        g->evac_t = t;
        g->evac_scored = 0;
    }
    g->was_evac = p->aux.evacuated;
    if (p->aux.evacuated && !g->evac_scored && p->aux.rsp_control && c->scram && p->T_core_out < 873.15) {
        g->evac_scored = 1;
        if (t - g->evac_t < 600.0) {
            g->score += 300.0;
            rm_plant_msg(p, "SHIFT SUPERVISOR: SAFE SHUTDOWN FROM THE REMOTE SHUTDOWN PANEL (+300)");
        }
    }
    /* running on failed fuel spreads contamination */
    if (p->aux.dnd > 500.0 && c->p_thermal > 0.6 * RM_P_RATED) g->score -= 0.5 * dt;

    /* repairs */
    if (g->grid_back_at > 0 && t >= g->grid_back_at) {
        g->grid_back_at = 0;
        p->grid_ok = 1;
        rm_plant_msg(p, "GRID RESTORED - OFFSITE POWER AVAILABLE");
    }
    for (int d = 0; d < 3; d++)
        if (g->dg_back_at[d] > 0 && t >= g->dg_back_at[d]) {
            g->dg_back_at[d] = 0;
            p->diesel_avail[d] = 1;
            rm_plant_msg(p, "DIESEL GENERATOR %d RETURNED TO SERVICE", d + 1);
        }

    /* random failures, only once the unit is making real power */
    if (g->failures && t >= g->next_failure) {
        if (c->p_thermal > 0.2 * RM_P_RATED) failure(g, p);
        g->next_failure = t + 300.0 + expo(g, 1200.0);
    }
}
