/* Two-group data tables generated from ENDF/B-VIII.1 lattice calculations
 * for the RM-ELM fuel channel. Do not edit. */
#ifndef RM_XSDATA_H
#define RM_XSDATA_H
#include "rm_xs.h"
#define RM_XS_NVALS 12
#define RM_XS_MAXPTS 5
typedef struct {
    int n;
    double x[RM_XS_MAXPTS];
    double delta[RM_XS_MAXPTS][RM_XS_NVALS];
} rm_xs_branch;
extern const rm_xs2 rm_xs_fuel_ref;
extern const rm_xs_branch rm_xs_br_tf, rm_xs_br_na, rm_xs_br_tg, rm_xs_br_tz, rm_xs_br_x;
extern const rm_xs2 rm_xs_fixed_tab[RM_XS_NFIXED];
extern const rm_micro2 rm_xs_micro_tab[RM_NNUC];
#endif
