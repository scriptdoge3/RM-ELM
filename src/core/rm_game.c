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

static void failure(rm_game *g, rm_plant *p)
{
    double r = urand(g);
    int i = pick_loop(g);
    rm_loop *l = &p->loop[i];
    if (r < 0.22) {
        if (!l->sg.isolated && l->sg.leak == 0.0) {
            l->sg.leak = 1e-4 * (0.5 + urand(g));
            return;   /* silent: the hydrogen meter is how you find out */
        }
    } else if (r < 0.40) {
        if (l->spump.motor_on && !l->spump.tripped) {
            l->spump.tripped = 1;
            rm_plant_msg(p, "SECONDARY PUMP %d TRIPPED - MOTOR OVERCURRENT", i + 1);
            return;
        }
    } else if (r < 0.50) {
        if (l->ppump.motor_on && !l->ppump.tripped) {
            l->ppump.tripped = 1;
            rm_plant_msg(p, "PRIMARY PUMP %d TRIPPED - BEARING TEMPERATURE HIGH", i + 1);
            return;
        }
    } else if (r < 0.62) {
        if (!p->turbine_tripped) {
            p->turbine_tripped = 1;
            p->generator_breaker = 0;
            rm_plant_msg(p, "TURBINE TRIP - LOW CONDENSER VACUUM");
            return;
        }
    } else if (r < 0.80) {
        int d = (int)(urand(g) * 3) % 3;
        if (p->diesel_avail[d]) {
            p->diesel_avail[d] = 0;
            g->dg_back_at[d] = p->t + 1800.0 + 1800.0 * urand(g);
            rm_plant_msg(p, "DIESEL GENERATOR %d OUT OF SERVICE - FUEL RACK FAULT", d + 1);
            return;
        }
    } else if (r < 0.88) {
        if (p->offsite_power) {
            p->offsite_power = 0;
            g->grid_back_at = p->t + 600.0 + 1200.0 * urand(g);
            rm_plant_msg(p, "GRID FAULT - OFFSITE POWER LOST");
            return;
        }
    } else {
        /* grid frequency dip: the dispatcher wants more, now */
        if (g->on_line && p->generator_breaker) {
            g->demand_target = fmin(RM_MWE_RATED, g->demand + 100.0);
            g->ramp = 30.0 / 60.0;
            rm_plant_msg(p, "LOAD DISPATCH: FREQUENCY LOW - RAISE TO %.0f MWE AT ONCE", g->demand_target);
            return;
        }
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

    /* repairs */
    if (g->grid_back_at > 0 && t >= g->grid_back_at) {
        g->grid_back_at = 0;
        p->offsite_power = 1;
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
