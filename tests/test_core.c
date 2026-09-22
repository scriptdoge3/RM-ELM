/* Full-core checks: criticality search, rod worths, feedback, SCRAM. */
#include "rm_core.h"
#include "rm_test.h"

#include <time.h>

static double k_now(rm_core *c, int outers)
{
    rm_core_update_xs(c);
    double k = 0;
    for (int i = 0; i < outers; i++) k = rm_diff_forward(&c->dif, 1, 2, 1.4);
    return k;
}

int main(void)
{
    rm_core c;
    rm_core_init(&c);
    printf("core: %d fuel channels, %d control rods, %d diffusion nodes\n", c.nchan, c.nctrl, c.dif.nn);
    int nb[RM_NBANKS] = {0};
    for (int k = 0; k < c.nctrl; k++) nb[c.ctrl_bank[k]]++;
    printf("banks: REG %d  SHIM A-D %d %d %d %d  SAFETY %d\n", nb[0], nb[1], nb[2], nb[3], nb[4], nb[5]);
    CHECK(c.nchan + c.nctrl == 397, "layout");

    clock_t t0 = clock();
    /* cold isothermal, all rods out / in */
    for (int b = 0; b < RM_NBANKS; b++) rm_core_bank_move(&c, b, 0), c.rod_ins[0] = c.rod_ins[0];
    for (int k = 0; k < c.nctrl; k++) c.rod_ins[k] = 0.0;
    double k_out = k_now(&c, 600);
    for (int k = 0; k < c.nctrl; k++) c.rod_ins[k] = RM_ACTIVE_H;
    double k_in = k_now(&c, 400);
    printf("hot-standby 653 K: k(all rods out) = %.5f  k(all rods in) = %.5f  total rod worth %.0f pcm\n",
           k_out, k_in, 1e5 * (1 / k_in - 1 / k_out));
    CHECK(k_out > 1.02 && k_in < 0.95, "rods can control the core");
    for (int b = 0; b < RM_NBANKS; b++) {
        for (int k = 0; k < c.nctrl; k++) c.rod_ins[k] = 0.0;
        for (int k = 0; k < c.nctrl; k++)
            if (c.ctrl_bank[k] == b) c.rod_ins[k] = RM_ACTIVE_H;
        double kb = k_now(&c, 150);
        printf("  bank %d worth %.0f pcm\n", b, 1e5 * (1 / kb - 1 / k_out));
    }

    double pos = rm_core_steady(&c, 1.0, 1, 1);
    printf("full power steady state: shims at %.1f cm, k = %.6f, peaking %.2f, rho %.1f pcm (%.1f s)\n",
           pos, c.dif.k, c.peak_factor, 1e5 * c.rho, (double)(clock() - t0) / CLOCKS_PER_SEC);
    printf("  fuel mean %.0f K, fuel centre max %.0f K, clad max %.0f K\n",
           rm_core_mean_fuel_T(&c), rm_core_max_fuel_T(&c), rm_core_max_clad_T(&c));
    double tout = 0;
    for (int ch = 0; ch < c.nchan; ch++) tout += c.th.T_mixed_out[ch];
    printf("  mean channel outlet %.1f C\n", tout / c.nchan - 273.15);

    /* run at power for a few seconds: should stay put */
    for (int i = 0; i < 100; i++) rm_core_step(&c, 0.05);
    printf("after 5 s: P = %.1f MW, rho = %.1f pcm, Doppler %.2f pcm/K\n", c.p_thermal / 1e6, 1e5 * c.rho,
           1e5 * c.rho_doppler_coef);
    CHECK(fabs(c.p_thermal / RM_P_RATED - 1.0) < 0.03, "steady power drift %.3f", c.p_thermal / RM_P_RATED);
    CHECK(c.rho_doppler_coef < 0, "Doppler negative");

    /* SCRAM */
    rm_core_scram(&c);
    printf("SCRAM:\n   t(s)   P(MW)   fission   rho(pcm)\n");
    for (int i = 0; i <= 200; i++) {
        if (i % 20 == 0)
            printf("  %5.1f  %7.1f  %7.4f  %8.0f\n", i * 0.05, c.p_thermal / 1e6, c.pks.n, 1e5 * c.rho);
        rm_core_step(&c, 0.05);
    }
    CHECK(c.pks.n < 0.02, "fission power after scram %g", c.pks.n);
    CHECK(c.p_decay / RM_P_RATED > 0.03, "decay heat present");
    rm_core_free(&c);
    return TEST_REPORT();
}
