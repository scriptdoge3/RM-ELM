/* Startup from hot shutdown: approach to criticality on the source, heat
 * up on nuclear power, feedwater in service, turbine latched, power raised
 * on automatic rod control. */
#include "rm_plant.h"
#include "rm_test.h"

#include <stdlib.h>

static void show(const rm_plant *p, const char *what)
{
    const rm_core *c = &p->core;
    printf("  t=%6.0f %-22s n=%9.2e  rho=%+7.1f pcm  per=%8.1f s  Pth %7.2f MW  Tin %5.1f  Tout %5.1f  "
           "SHIM %5.1f REG %5.1f  FW %5.1f kg/s  Tst %5.1f  p %5.2f  Pnet %6.1f\n",
           p->t, what, c->pks.n, 1e5 * c->rho, isfinite(p->period) ? p->period : 0.0, c->p_thermal / 1e6,
           p->T_core_in - 273.15, p->T_core_out - 273.15, rm_core_bank_pos(c, BANK_SHIM_A),
           rm_core_bank_pos(c, BANK_REG), p->loop[0].sg.W_fw * 4, p->loop[0].sg.T_steam - 273.15,
           p->p_header / 1e6, p->P_net / 1e6);
}

/* the operator keeps the IRM on scale, ranging up and down */
static void irm_ranging(rm_plant *p)
{
    for (int ch = 0; ch < 8; ch++) {
        int *r = &p->nms.irm_range[ch];
        while (*r < 10 && rm_plant_irm_ch(p, ch) > 90.0) (*r)++;
        while (*r > 1 && rm_plant_irm_ch(p, ch) < 15.0) (*r)--;
    }
}

static void run(rm_plant *p, double secs, double dt, const char *what, double every)
{
    double next = p->t;
    double end = p->t + secs;
    while (p->t < end - 1e-9) {
        if (p->t >= next) {
            show(p, what);
            next += every;
        }
        irm_ranging(p);
        rm_plant_step(p, dt);
    }
}

int main(void)
{
    rm_plant *p = malloc(sizeof *p);
    rm_plant_init(p);
    rm_plant_hot_standby(p);
    rm_core *c = &p->core;
    printf("hot standby:\n");
    run(p, 10, 0.1, "standby", 5);
    double n0 = c->pks.n;
    CHECK(c->rho < -0.02, "deeply subcritical with all rods in (%.0f pcm)", 1e5 * c->rho);
    CHECK(n0 > 1e-12 && n0 < 1e-7, "source-range level %g", n0);
    CHECK(fabs(p->T_core_in - 653.15) < 2, "isothermal 380 C (%.1f)", p->T_core_in - 273.15);

    char why[80];
    CHECK(p->mode == RM_MODE_SHUTDOWN, "starts with the mode switch in SHUTDOWN");
    CHECK(rm_plant_rod_block(p, 0, why, sizeof why), "rod withdrawal blocked in SHUTDOWN");
    CHECK(!rm_plant_set_mode(p, RM_MODE_RUN, why, sizeof why), "RUN refused at source level (%s)", why);
    rm_plant_set_mode(p, RM_MODE_STARTUP, why, sizeof why);
    CHECK(!c->scram, "STARTUP selected without a trip");
    printf("  SRM %.0f cps, IRM range %d reads %.2f\n", rm_plant_srm_cps(p), p->nms.irm_range[0], rm_plant_irm(p));
    printf("withdraw safety bank, then shims in steps until critical:\n");
    rm_core_bank_move(c, BANK_SAFETY, 0.0);
    run(p, 90, 0.1, "safety out", 30);
    CHECK(c->rho < 0, "still subcritical with the safety bank out");
    rm_core_bank_move(c, BANK_REG, 80.0);
    double shim = 160.0;
    int crit = 0;
    for (int step = 0; step < 40 && !crit; step++) {
        shim -= 5.0;
        for (int b = BANK_SHIM_A; b <= BANK_SHIM_D; b++) rm_core_bank_move(c, b, shim);
        run(p, 20, 0.1, "shims", 20);
        if (c->rho > 0.0) crit = 1;
    }
    CHECK(crit, "reached criticality (shims at %.0f cm)", shim);
    CHECK(!c->scram, "no trip on the approach (%s)", p->first_out);
    /* hold the shims and let power climb on a ~60 s period, then stop at ~3% */
    for (int b = BANK_SHIM_A; b <= BANK_SHIM_D; b++) rm_core_bank_move(c, b, rm_core_bank_pos(c, b) + 3.0);
    p->power_set = 0.03;
    p->auto_rod = 1;
    run(p, 1500, 0.2, "power ascension", 100);
    CHECK(!c->scram, "no trip heating up (%s)", p->first_out);
    CHECK(c->p_thermal > 0.01 * RM_P_RATED && c->p_thermal < 0.06 * RM_P_RATED, "holding ~3%% (%.1f MW)",
          c->p_thermal / 1e6);

    printf("feedwater in service, raise to 10%%, mode switch to RUN, latch turbine:\n");
    p->fw_on = 1;
    p->power_set = 0.10;
    run(p, 900, 0.2, "feed on, to 15%", 100);
    CHECK(!c->scram, "no trip with feed on (%s)", p->first_out);
    CHECK(rm_plant_set_mode(p, RM_MODE_RUN, why, sizeof why), "RUN accepted at %.1f%% APRM", rm_plant_aprm(p));
    p->tg.tdfp[0] = 1;   /* a turbine-driven feed pump for the power ascension */
    p->turbine_tripped = 0;
    p->generator_breaker = 1;
    p->tv_int = -2.0;
    p->power_set = 0.40;
    /* the operator pulls the shims so the regulating bank keeps some travel */
    for (int b = BANK_SHIM_A; b <= BANK_SHIM_D; b++) rm_core_bank_move(c, b, 60.0);
    run(p, 1200, 0.2, "on line, to 40%", 100);
    CHECK(fabs(c->p_thermal / RM_P_RATED - 0.40) < 0.03, "holding 40%% (%.1f %%)", 100 * c->p_thermal / RM_P_RATED);
    CHECK(!c->scram, "no trip on line (%s)", p->first_out);
    CHECK(p->P_net > 100e6, "generating (%.0f MWe net)", p->P_net / 1e6);
    CHECK(fabs(p->T_core_in - 653.15) < 20, "inlet held near 380 C (%.1f)", p->T_core_in - 273.15);
    rm_plant_free(p);
    free(p);
    return TEST_REPORT();
}
