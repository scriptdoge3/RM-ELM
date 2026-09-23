/* Game layer: dispatcher orders, scoring, random failures (fixed seed). */
#include "rm_game.h"
#include "rm_test.h"

#include <stdlib.h>
#include <string.h>

int main(void)
{
    rm_plant *p = malloc(sizeof *p);
    rm_plant_init(p);
    rm_plant_steady(p);
    rm_game g;
    rm_game_init(&g, p, 7, 1);
    g.next_failure = p->t + 400.0;   /* make sure one happens in the test window */
    unsigned seen = 0;
    int orders = 0;
    for (int i = 0; i < 7200; i++) {
        rm_plant_step(p, 0.25);
        rm_game_step(&g, p, 0.25);
        /* a simple operator: follow the dispatcher with the power demand */
        p->power_set = fmin(1.0, g.demand / RM_MWE_RATED * 1.0);
        while (seen < p->nmsg) {
            printf("  t=%5.0f  %s\n", p->t, p->msg[seen % 16]);
            if (strstr(p->msg[seen % 16], "LOAD DISPATCH: GO TO")) orders++;
            seen++;
        }
        if (i % 1200 == 0)
            printf("  t=%5.0f  demand %5.0f -> %5.0f MWe  net %5.0f MWe  score %7.1f  on target %4.0f s\n", p->t,
                   g.demand, g.demand_target, p->P_net / 1e6, g.score, g.on_target);
    }
    printf("  final: score %.1f, %.1f MWh, %.0f s on target, %d trips\n", g.score, g.mwh, g.on_target, g.trips);
    CHECK(orders >= 1, "dispatcher gave orders (%d)", orders);
    CHECK(g.mwh > 300, "energy sent out (%.0f MWh)", g.mwh);
    CHECK(g.on_target > 300, "tracked the demand for a while (%.0f s)", g.on_target);
    CHECK(p->nmsg > 1, "events reported");
    rm_plant_free(p);
    free(p);
    return TEST_REPORT();
}
