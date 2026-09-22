/* Diffusion solver checks on a homogeneous bare hex-z prism, where the
 * two-group eigenvalue has a closed form via the geometric buckling. */
#include "rm_diffusion.h"
#include "rm_kernels.h"
#include "rm_test.h"

#include <math.h>
#include <stdio.h>
#include <time.h>

static void fill(rm_diff *s, double scale_nsf)
{
    for (int n = 0; n < s->nn; n++) {
        s->D[0][n] = 1.5;
        s->D[1][n] = 0.8;
        s->sr[0][n] = 0.012 + 0.010;   /* absorption + downscatter */
        s->sr[1][n] = 0.060;
        s->s12[n] = 0.010;
        s->s21[n] = 0.0;
        s->nsf[0][n] = 0.008 * scale_nsf;
        s->nsf[1][n] = 0.085 * scale_nsf;
        s->ksf[0][n] = s->nsf[0][n] / 2.45 * 3.2e-11;
        s->ksf[1][n] = s->nsf[1][n] / 2.45 * 3.2e-11;
    }
    rm_diff_couplings(s);
}

int main(void)
{
    const int rings = 8, nz = 20;
    const double pitch = 16.0, H = 200.0;
    double dz[20];
    for (int i = 0; i < nz; i++) dz[i] = H / nz;
    rm_diff s;
    rm_diff_init(&s, rings, nz, pitch, dz);
    fill(&s, 1.0);
    clock_t t0 = clock();
    double k = 0.0;
    for (int i = 0; i < 400; i++) k = rm_diff_forward(&s, 1, 2, 1.5);
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    double res = rm_diff_residual(&s);

    /* reference: equivalent-area cylinder with extrapolation distance 2.13*D1 */
    double area = s.area * s.ncol;
    double R = sqrt(area / RM_PI) + 2.13 * 1.5 * 0.7;
    double He = H + 2 * 2.13 * 1.5 * 0.7;
    double B2 = pow(2.405 / R, 2) + pow(RM_PI / He, 2);
    double r1 = 0.022 + 1.5 * B2, r2 = 0.060 + 0.8 * B2;
    double kref = (0.008 + 0.085 * 0.010 / r2) / r1;
    printf("k = %.5f (buckling estimate %.5f), residual %.2e, 400 outers in %.2f s [%s]\n",
           k, kref, res, secs, rm_kernels_backend_name());
    CHECK(fabs(k - kref) < 0.01, "k %.5f vs %.5f", k, kref);
    CHECK(res < 1e-5, "residual %g", res);

    /* the importance-weighted reactivity of a converged state is ~ 1 - 1/k */
    for (int i = 0; i < 400; i++) rm_diff_adjoint(&s, 1, 2, 1.5);
    CHECK(fabs(s.k_adj - k) < 1e-5, "adjoint k %.6f vs %.6f", s.k_adj, k);
    double rho = rm_diff_reactivity(&s);
    CHECK(fabs(rho - (1 - 1 / k)) < 1e-6, "rho %.7f vs %.7f", rho, 1 - 1 / k);

    /* first-order perturbation: +1% nu-Sigma_f everywhere -> drho ~ 0.01/k */
    fill(&s, 1.01);
    double rho2 = rm_diff_reactivity(&s);
    double exact = 1 - 1 / (k * 1.01);
    printf("perturbed rho %.6f, exact %.6f\n", rho2, exact);
    CHECK(fabs(rho2 - exact) < 2e-6, "perturbation rho %.7f vs %.7f", rho2, exact);

    /* C and assembly kernels give the same eigenvalue */
    rm_kernels_set_backend(RM_BACKEND_C);
    fill(&s, 1.0);
    for (int i = 0; i < 20; i++) rm_diff_forward(&s, 1, 2, 1.5);
    double kc = s.k;
    rm_kernels_set_backend(RM_BACKEND_AVX);
    CHECK(fabs(kc - k) < 1e-6, "C backend k %.7f vs %.7f", kc, k);
    rm_diff_free(&s);
    return TEST_REPORT();
}
