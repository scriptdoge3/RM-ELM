/* Whole-plant checks: steady full power heat balance and three transients. */
#include "rm_plant.h"
#include "rm_sodium.h"
#include "rm_test.h"

#include <stdlib.h>

#include <string.h>

static void show(const rm_plant *p)
{
    printf("  t=%6.1f  Pth %7.1f MW  Pnet %6.1f MWe  Wcore %5.0f kg/s  Tin %5.1f C  Tout %5.1f C  "
           "fuelmax %5.0f C  cladmax %5.0f C  p %5.2f MPa  Tst %5.1f C\n",
           p->t, p->core.p_thermal / 1e6, p->P_net / 1e6, p->W_core, p->T_core_in - 273.15,
           p->T_core_out - 273.15, rm_core_max_fuel_T(&p->core) - 273.15,
           rm_core_max_clad_T(&p->core) - 273.15, p->p_header / 1e6, p->loop[0].sg.T_steam - 273.15);
}

static rm_plant *fresh(void)
{
    static rm_plant base;
    static int ready = 0;
    rm_plant *p = malloc(sizeof *p);
    if (!ready) {
        rm_plant_init(&base);
        rm_plant_steady(&base);
        ready = 1;
    }
    /* deep copy is complicated by heap arrays: just rebuild each time */
    rm_plant_init(p);
    rm_plant_steady(p);
    return p;
}

int main(void)
{
    rm_plant *p = malloc(sizeof *p);
    rm_plant_init(p);
    rm_plant_steady(p);
    printf("steady full power:\n");
    for (int i = 0; i < 100; i++) rm_plant_step(p, 0.05);
    show(p);
    rm_loop *l = &p->loop[0];
    printf("  loop 1: primary %.0f kg/s, IHX %.1f MW, secondary %.1f->%.1f C, SG Q %.1f MW, FW %.1f kg/s at %.1f C\n",
           l->W, l->ihx.Q / 1e6, rm_na_T(l->shot.h[RM_PIPE_N - 1]) - 273.15, rm_na_T(l->scold.h[RM_PIPE_N - 1]) - 273.15,
           l->sg.Q / 1e6, l->sg.W_fw, p->T_fw - 273.15);
    printf("  turbine valve %.2f, bypass %.2f, gross %.1f MWe, efficiency %.1f %%\n", p->turbine_valve,
           p->bypass_valve, p->P_gen / 1e6, 100 * p->P_net / p->core.p_thermal);
    CHECK(fabs(p->core.p_thermal / RM_P_RATED - 1) < 0.03, "steady power");
    CHECK(fabs(p->T_core_in - 653.15) < 25, "inlet temp %g", p->T_core_in);
    CHECK(p->P_net > 700e6 && p->P_net < 1100e6, "net power %g", p->P_net);

    printf("\nturbine trip (reactor stays at power, bypass takes the steam):\n");
    p->turbine_tripped = 1;
    for (int i = 0; i <= 600; i++) {
        if (i % 100 == 0) show(p);
        rm_plant_step(p, 0.05);
    }
    CHECK(p->p_header < 16.5e6, "header pressure contained %g", p->p_header);
    rm_plant_free(p);

    printf("\nunprotected loss of flow (all primary pumps trip, NO scram):\n");
    rm_plant_init(p);
    rm_plant_steady(p);
    p->rps_bypass = 1;
    for (int i = 0; i < RM_NLOOPS; i++) p->loop[i].ppump.tripped = 1, p->loop[i].ppump.pony_on = 0;
    double peak_clad = 0;
    for (int i = 0; i <= 3000; i++) {
        if (i % 300 == 0) show(p);
        rm_plant_step(p, 0.05);
        double cm = rm_core_max_clad_T(&p->core);
        if (cm > peak_clad) peak_clad = cm;
    }
    printf("  peak clad %.0f C, final power %.1f %%, natural circulation %.1f %% flow\n", peak_clad - 273.15,
           100 * p->core.p_thermal / RM_P_RATED, 100 * p->W_core / (4 * 2830.0));
    rm_plant_free(p);

    printf("\nprotected loss of flow (pumps trip, pony motors on, protection active):\n");
    rm_plant_init(p);
    rm_plant_steady(p);
    for (int i = 0; i < RM_NLOOPS; i++) p->loop[i].ppump.tripped = 1;
    peak_clad = 0;
    for (int i = 0; i <= 3000; i++) {
        if (i % 600 == 0) show(p);
        rm_plant_step(p, 0.05);
        double cm = rm_core_max_clad_T(&p->core);
        if (cm > peak_clad) peak_clad = cm;
    }
    printf("  first out: %s, peak clad %.0f C\n", p->first_out, peak_clad - 273.15);
    CHECK(p->core.scram, "protection tripped the reactor");
    CHECK(peak_clad < 973.15, "clad stayed below 700 C (%.0f C)", peak_clad - 273.15);
    rm_plant_free(p);
    free(p);
    (void)fresh;
    return TEST_REPORT();
}
