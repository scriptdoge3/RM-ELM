/*
 * Game layer on top of the plant: a load dispatcher that orders net
 * electrical output, a score, and random equipment failures. Everything
 * reaches the operator through the plant's message queue.
 */
#ifndef RM_GAME_H
#define RM_GAME_H

#include "rm_plant.h"

typedef struct {
    unsigned rng;
    int failures;             /* random equipment failures enabled */
    int on_line;              /* generator has been synchronised at least once */
    double demand;            /* MWe net the dispatcher currently wants (ramps) */
    double demand_target;     /* MWe it is heading for */
    double ramp;              /* MWe per second */
    double next_order;        /* sim time of the next dispatch order */
    double next_failure;
    double grid_back_at;      /* offsite power returns (after a random grid loss) */
    double dg_back_at[3];
    double score;
    double mwh;               /* energy sent out while on line */
    double on_target;         /* seconds within 3% of demand */
    int trips;
    int was_scram, was_turb_trip, was_burst[RM_NLOOPS], leak_seen[RM_NLOOPS];
} rm_game;

void rm_game_init(rm_game *g, const rm_plant *p, unsigned seed, int failures);
void rm_game_step(rm_game *g, rm_plant *p, double dt);
/* demand deviation as a fraction of rated output (0 when not dispatched) */
double rm_game_deviation(const rm_game *g, const rm_plant *p);

#define RM_MWE_RATED 900.0

#endif
