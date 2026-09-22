/* Material property checks: IF97 against the iapws reference package,
 * sodium against Fink & Leibowitz tabulated values. */
#include "rm_if97.h"
#include "rm_materials.h"
#include "rm_sodium.h"
#include "rm_test.h"

#include "if97_reference.h"

#define N(a) (sizeof(a) / sizeof((a)[0]))

static void test_if97(void)
{
    rm_if97_pt o;
    for (size_t i = 0; i < N(ref_r1); i++) {
        rm_if97_region1(ref_r1[i][0], ref_r1[i][1], &o);
        CHECK_REL(o.v, ref_r1[i][2], 1e-9, "r1 v");
        CHECK_REL(o.h, ref_r1[i][3], 1e-9, "r1 h");
        CHECK_REL(o.s, ref_r1[i][4], 1e-9, "r1 s");
        CHECK_REL(o.cp, ref_r1[i][5], 1e-9, "r1 cp");
    }
    for (size_t i = 0; i < N(ref_r2); i++) {
        rm_if97_region2(ref_r2[i][0], ref_r2[i][1], &o);
        CHECK_REL(o.v, ref_r2[i][2], 1e-9, "r2 v");
        CHECK_REL(o.h, ref_r2[i][3], 1e-9, "r2 h");
        CHECK_REL(o.s, ref_r2[i][4], 1e-9, "r2 s");
        CHECK_REL(o.cp, ref_r2[i][5], 1e-9, "r2 cp");
    }
    for (size_t i = 0; i < N(ref_tsat); i++) {
        CHECK_REL(rm_if97_tsat(ref_tsat[i][0]), ref_tsat[i][1], 1e-10, "tsat");
        CHECK_REL(rm_if97_psat(ref_tsat[i][1]), ref_tsat[i][0], 1e-8, "psat");
    }
    for (size_t i = 0; i < N(ref_ph); i++) {
        rm_if97_state st;
        rm_if97_ph(ref_ph[i][0], ref_ph[i][1], &st);
        CHECK_ABS(st.T, ref_ph[i][2], 2e-6, "T(p,h)");
        double x = st.x < 0 ? 0 : st.x > 1 ? 1 : st.x;
        CHECK_ABS(x, ref_ph[i][3], 1e-8, "x(p,h)");
    }
    for (size_t i = 0; i < N(ref_ps); i++)
        CHECK_REL(rm_if97_h_ps(ref_ps[i][0], ref_ps[i][1]), ref_ps[i][2], 1e-8, "h(p,s)");
    for (size_t i = 0; i < N(ref_tr); i++) {
        CHECK_REL(rm_if97_mu(ref_tr[i][0], ref_tr[i][1]), ref_tr[i][2], 1e-8, "mu");
        CHECK_REL(rm_if97_k(ref_tr[i][0], ref_tr[i][1]), ref_tr[i][3], 1e-8, "k");
    }
    /* round trip h(p,T) -> T(p,h) */
    for (double p = 0.05e6; p < 16e6; p *= 1.7) {
        for (double T = 290.0; T < 1000.0; T += 37.0) {
            double h = rm_if97_h_pT(p, T);
            rm_if97_state st;
            rm_if97_ph(p, h, &st);
            if (st.region != 4) CHECK_ABS(st.T, T, 1e-6, "round trip T");
        }
    }
}

static void test_sodium(void)
{
    /* Fink & Leibowitz Table 2.1-1 / 2.2-1 spot values */
    CHECK_REL(rm_na_rho(400.0), 919.0, 2e-3, "rho 400K");
    CHECK_REL(rm_na_rho(800.0), 828.0, 2e-3, "rho 800K");
    CHECK_REL(rm_na_cp(800.0), 1262.0, 5e-3, "cp 800K");
    CHECK_REL(rm_na_k(800.0), 62.9, 1e-2, "k 800K");
    CHECK_REL(rm_na_tsat(101325.0), 1154.6, 2e-3, "normal boiling point");
    CHECK_REL(rm_na_hfg(1154.6), 3.87e6, 1e-2, "hfg at nbp");
    /* freezing round trip */
    for (double T = 300.0; T < 1100.0; T += 13.0) {
        double h = rm_na_h(T);
        if (fabs(T - RM_NA_TMELT) > 0.01) CHECK_ABS(rm_na_T(h), T, 1e-6, "Na T(h)");
    }
    double hs = rm_na_h_solidus(), hl = rm_na_h_liquidus();
    CHECK_REL(hl - hs, RM_NA_HFUS, 1e-12, "heat of fusion");
    CHECK_ABS(rm_na_T(0.5 * (hs + hl)), RM_NA_TMELT, 1e-12, "slush temperature");
    CHECK_ABS(rm_na_liquid_fraction(0.5 * (hs + hl)), 0.5, 1e-12, "slush fraction");
}

static void test_materials(void)
{
    CHECK(rm_fuel_k(800.0) > 15.0 && rm_fuel_k(800.0) < 30.0, "fuel k range %g", rm_fuel_k(800.0));
    CHECK_REL(rm_graphite_cp(700.0), 1518.0, 2e-2, "graphite cp");
    double p = rm_zrh_ph2(1073.15, 1.6);
    CHECK(p > 2e4 && p < 2e5, "ZrH1.6 H2 pressure at 800C %g Pa", p);
}

int main(void)
{
    test_if97();
    test_sodium();
    test_materials();
    return TEST_REPORT();
}
