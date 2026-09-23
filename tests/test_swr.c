/* Sodium-water reaction: a small SG tube leak grows by self-wastage. With
 * the operator isolating the SG on the hydrogen alarm the unit stays on
 * line; left alone, the leak bursts the rupture disc and trips the reactor. */
#include "rm_plant.h"
#include "rm_test.h"

#include <stdlib.h>
#include <string.h>

static void show(const rm_plant *p)
{
    const rm_sg *s = &p->loop[1].sg;
    printf("  t=%6.0f  leak %8.4f kg/s  H2 %5.2f ppm  %s%s Pth %6.0f MW  Tin %5.1f  Tout %5.1f  Pnet %5.0f\n", p->t,
           s->leak, s->h2, s->isolated ? "ISOLATED " : "", s->disc_burst ? "DISC " : "", p->core.p_thermal / 1e6,
           p->T_core_in - 273.15, p->T_core_out - 273.15, p->P_net / 1e6);
}

static void messages(rm_plant *p, unsigned *seen)
{
    while (*seen < p->nmsg) {
        printf("  MSG %s\n", p->msg[*seen % 16]);
        (*seen)++;
    }
}

int main(void)
{
    rm_plant *p = malloc(sizeof *p);
    for (int act = 1; act >= 0; act--) {
        printf(act ? "\noperator isolates SG 2 on the hydrogen alarm and runs back to 75%%:\n"
                   : "\nno operator action:\n");
        rm_plant_init(p);
        rm_plant_steady(p);
        unsigned seen = 0;
        p->loop[1].sg.leak = 1e-4;   /* 0.1 g/s pinhole */
        double t_alarm = -1, t_iso = -1;
        for (int i = 0; i < 9000 && !p->core.scram; i++) {
            rm_plant_step(p, 0.2);
            messages(p, &seen);
            if (i % 500 == 0) show(p);
            if (t_alarm < 0 && p->loop[1].sg.h2 > RM_H2_ALARM) {
                t_alarm = p->t;
                printf("  hydrogen alarm at t=%.0f s, leak %.3f g/s\n", p->t, 1000 * p->loop[1].sg.leak);
            }
            if (act && t_alarm > 0 && t_iso < 0 && p->t > t_alarm + 60) {
                rm_plant_isolate_sg(p, 1);
                p->loop[1].spump.motor_on = 0;
                p->power_set = 0.75;
                t_iso = p->t;
            }
        }
        show(p);
        printf("  first out: %s\n", p->first_out[0] ? p->first_out : "(none)");
        CHECK(t_alarm > 0, "hydrogen alarm came in");
        if (act) {
            CHECK(!p->core.scram, "no reactor trip (%s)", p->first_out);
            CHECK(!p->loop[1].sg.disc_burst, "rupture disc intact");
            CHECK(p->P_net > 450e6, "still on line at reduced power (%.0f MWe)", p->P_net / 1e6);
            CHECK(p->T_core_out < 873.15, "core outlet within limits (%.0f C)", p->T_core_out - 273.15);
        } else {
            CHECK(p->loop[1].sg.disc_burst, "rupture disc burst");
            CHECK(p->core.scram && strstr(p->first_out, "SODIUM-WATER"), "reactor tripped on it (%s)", p->first_out);
            CHECK(p->loop[1].sg.isolated, "SG 2 isolated automatically");
        }
        rm_plant_free(p);
    }
    free(p);
    return TEST_REPORT();
}
