/* Plant systems behind the new panels: turbine roll and auto-sync, RPS
 * divisions, instrument air, component cooling, failed fuel, sodium leak and
 * fire, bus transfer and feed pumps. */
#include "rm_plant.h"
#include "rm_test.h"

#include <stdlib.h>
#include <string.h>

static unsigned seen;
static void run(rm_plant *p, double secs, double dt)
{
    double end = p->t + secs;
    while (p->t < end - 1e-9) {
        rm_plant_step(p, dt);
        while (seen < p->nmsg) {
            printf("    %6.0f  %s\n", p->t, p->msg[seen % 16]);
            seen++;
        }
    }
}

static rm_plant *fresh(rm_plant *p, const char *what)
{
    printf("\n%s:\n", what);
    rm_plant_init(p);
    rm_plant_steady(p);
    seen = p->nmsg;
    return p;
}

int main(void)
{
    rm_plant *p = malloc(sizeof *p);
    char why[80];

    fresh(p, "turbine trip with the reactor held on the bypass, re-latch and auto-sync");
    p->rps_bypass = 1;
    rm_plant_turbine_trip(p, "TEST");
    run(p, 60, 0.1);
    printf("  speed after 60 s coasting: %.0f rpm\n", p->tg.speed);
    CHECK(p->tg.speed < 1790 && p->tg.speed > 1000, "coasting down (%.0f rpm)", p->tg.speed);
    CHECK(rm_plant_turbine_latch(p, why, sizeof why), "latched (%s)", why);
    p->tg.auto_sync = 1;
    run(p, 240, 0.1);
    printf("  speed %.0f rpm, breaker %s, net %.0f MWe, vibration %.1f mils, bearing %.0f C\n", p->tg.speed,
           p->generator_breaker ? "closed" : "open", p->P_net / 1e6, p->tg.vib, p->tg.brg_T);
    CHECK(p->generator_breaker, "auto-synchroniser closed the breaker");
    run(p, 120, 0.1);
    CHECK(p->P_net > 500e6, "loaded back up (%.0f MWe)", p->P_net / 1e6);
    rm_plant_free(p);

    fresh(p, "RPS: manual scram division A only, then B");
    rm_plant_manual_scram_div(p, 0);
    run(p, 2, 0.05);
    CHECK(!p->core.scram && p->nms.div_trip[0], "half scram only");
    rm_plant_manual_scram_div(p, 1);
    run(p, 2, 0.05);
    CHECK(p->core.scram, "full scram with both divisions");
    rm_plant_rps_reset(p, 0);
    CHECK(p->core.scram, "still tripped with one division reset");
    rm_plant_rps_reset(p, 1);
    CHECK(!p->core.scram, "reset");
    rm_plant_free(p);

    fresh(p, "loss of instrument air");
    p->aux.air_comp[0] = p->aux.air_comp[1] = 0;
    run(p, 240, 0.1);
    printf("  air %.1f bar, MSIV 1 %s, DRACS damper set %.0f\n", p->aux.air_p, p->loop[0].sg.msiv_open ? "open" : "shut",
           p->dracs_damper_set[0]);
    CHECK(!p->loop[0].sg.msiv_open, "MSIVs drifted shut");
    CHECK(p->dracs_damper_set[0] > 0.5, "DRACS dampers failed open");
    rm_plant_free(p);

    fresh(p, "loss of component cooling water");
    p->aux.ccw[0] = p->aux.ccw[1] = 0;
    run(p, 400, 0.1);
    printf("  pump 1 bearing %.0f C, first out %s\n", p->loop[0].brg_T, p->first_out);
    CHECK(p->loop[0].ppump.tripped, "primary pumps tripped on bearing temperature");
    CHECK(p->core.scram, "reactor tripped");
    rm_plant_free(p);

    fresh(p, "failed fuel pin at full power");
    p->aux.fuel_fail = 0.1;
    run(p, 300, 0.1);
    printf("  DND %.0f cps, cover gas %.0f MBq/m3\n", p->aux.dnd, p->aux.gas_act);
    CHECK(p->aux.dnd > 200, "delayed neutron detectors see it");
    CHECK(p->aux.gas_act > 20, "cover gas activity rising");
    rm_plant_free(p);

    fresh(p, "secondary sodium leak in loop 2, operator dumps the loop");
    p->loop[1].na_leak = 0.05;
    run(p, 30, 0.1);
    CHECK(p->aux.fire[RM_FIRE_SG2], "sodium fire in SG 2 cell");
    p->power_set = 0.7;
    rm_plant_dump_loop(p, 1);
    run(p, 700, 0.2);
    CHECK(!p->aux.fire[RM_FIRE_SG2], "fire burned out after the dump");
    CHECK(p->loop[1].Ws == 0.0, "loop 2 out of service");
    printf("  first out: %s  power %.0f%%  Tin %.0f C\n", p->first_out[0] ? p->first_out : "(none)",
           100 * p->core.p_thermal / RM_P_RATED, p->T_core_in - 273.15);
    rm_plant_free(p);

    fresh(p, "turbine trip with a failed bus transfer");
    p->tg.transfer_fail = 1;
    rm_plant_turbine_trip(p, "TEST");
    run(p, 30, 0.05);
    CHECK(!p->offsite_power, "house buses dead");
    CHECK(p->diesel_running[0], "diesels picked up the essential buses");
    rm_plant_free(p);

    fresh(p, "turbine-driven feed pump A trips at full power");
    p->tg.tdfp[0] = 0;
    run(p, 60, 0.1);
    double tin = p->T_core_in - 273.15;
    printf("  feed capacity %.2f, inlet %.0f C\n", p->fw_pump, tin);
    CHECK(p->fw_pump < 0.7, "feed capacity down to one pump");
    p->tg.mdfp = 1;
    run(p, 60, 0.1);
    CHECK(p->fw_pump > 0.8, "startup pump adds capacity (%.2f)", p->fw_pump);
    rm_plant_free(p);

    free(p);
    return TEST_REPORT();
}
