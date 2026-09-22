/* Kernel checks: physics of the point-kinetics integrator, and bit-for-bit
 * agreement between the C reference kernels and the assembly kernels. */
#include "rm_kernels.h"
#include "rm_test.h"

#include <stdlib.h>
#include <string.h>

/* Keepin U-235 fast-fission group data scaled to beta_eff = 0.0070 */
static rm_pk_params params(void)
{
    rm_pk_params p;
    static const double frac[6] = {0.038, 0.213, 0.188, 0.407, 0.128, 0.026};
    static const double lam[6] = {0.0127, 0.0317, 0.115, 0.311, 1.40, 3.87};
    for (int i = 0; i < 6; i++) {
        p.beta[i] = 0.0070 * frac[i];
        p.lambda[i] = lam[i];
    }
    p.Lambda = 8e-6;
    return p;
}

static void equilibrium(const rm_pk_params *p, rm_pk_state *s, double n)
{
    s->n = n;
    for (int i = 0; i < 6; i++) s->y[i] = p->beta[i] * n / p->lambda[i];
}

static double beta_tot(const rm_pk_params *p)
{
    double b = 0;
    for (int i = 0; i < 6; i++) b += p->beta[i];
    return b;
}

static void run(const rm_pk_params *p, rm_pk_state *s, double rho, double dE, double tend,
                double dt, int (*fn)(const rm_pk_params *, rm_pk_state *, const rm_pk_step_in *,
                                     rm_pk_step_out *), double *energy)
{
    rm_pk_step_in in = {rho, 0.0, dE, 0.0, dt, 0.01, 1e-9};
    rm_pk_step_out out;
    double E = 0;
    for (double t = 0; t < tend - 1e-12; t += dt) {
        fn(p, s, &in, &out);
        E += out.energy;
    }
    if (energy) *energy = E;
}

static void test_pk_physics(void)
{
    rm_pk_params p = params();
    double b = beta_tot(&p);
    rm_pk_state s;

    /* critical reactor stays put */
    equilibrium(&p, &s, 1.0);
    run(&p, &s, 0.0, 0.0, 10.0, 0.02, rm_pk_advance_c, NULL);
    CHECK_ABS(s.n, 1.0, 1e-9, "critical steady state");

    /* prompt jump: n after a -$1 step ~ beta/(beta - rho) = 0.5 */
    equilibrium(&p, &s, 1.0);
    run(&p, &s, -b, 0.0, 0.01, 0.01, rm_pk_advance_c, NULL);
    CHECK_REL(s.n, 0.5, 0.02, "prompt drop for -$1");

    /* +10 cents: stable period from the inhour equation.
     * rho = omega*Lambda + sum beta_i*omega/(omega+lambda_i); solve for omega */
    double rho = 0.1 * b, lo = 1e-6, hi = 1.0;
    for (int it = 0; it < 200; it++) {
        double w = 0.5 * (lo + hi), r = w * p.Lambda;
        for (int i = 0; i < 6; i++) r += p.beta[i] * w / (w + p.lambda[i]);
        if (r > rho) hi = w; else lo = w;
    }
    double omega = 0.5 * (lo + hi);
    equilibrium(&p, &s, 1.0);
    run(&p, &s, rho, 0.0, 200.0, 0.05, rm_pk_advance_c, NULL);
    double n1 = s.n;
    run(&p, &s, rho, 0.0, 10.0, 0.05, rm_pk_advance_c, NULL);
    double measured = log(s.n / n1) / 10.0;
    CHECK_REL(measured, omega, 0.01, "+10 cent asymptotic period");
    printf("  +10 cents: period %.1f s (inhour %.1f s)\n", 1.0 / measured, 1.0 / omega);

    /* $1.5 prompt-critical step with adiabatic Doppler-like feedback:
     * the burst must turn over and the energy must be finite */
    equilibrium(&p, &s, 1e-3);
    double E;
    run(&p, &s, 1.5 * b, -0.02, 0.2, 0.01, rm_pk_advance_c, &E);
    CHECK(s.n < 1e3 && E > 0.0 && E < 50.0, "self-limited burst n=%g E=%g", s.n, E);
    printf("  $1.5 burst with feedback: E = %.3f full-power-seconds, n_end = %.3g\n", E, s.n);

    /* subcritical source multiplication: n = S*Lambda/(-rho) */
    equilibrium(&p, &s, 1e-12);
    rm_pk_step_in in = {-0.05, 0.0, 0.0, 1e-5, 1.0, 0.01, 1e-9};
    rm_pk_step_out out;
    for (int i = 0; i < 2000; i++) rm_pk_advance_c(&p, &s, &in, &out);
    CHECK_REL(s.n, 1e-5 * p.Lambda / 0.05, 1e-3, "source multiplication");
}

#if defined(RMELM_HAVE_ASM)
static void test_pk_asm_equivalence(void)
{
    rm_pk_params p = params();
    double b = beta_tot(&p);
    const double rhos[] = {0.0, 0.1 * b, -3.0 * b, 0.9 * b, 1.3 * b, -0.2 * b};
    const double dEs[] = {0.0, -0.001, 0.0, -0.01, -0.03, 0.0};
    for (size_t k = 0; k < sizeof(rhos) / sizeof(rhos[0]); k++) {
        rm_pk_state sc, sa;
        equilibrium(&p, &sc, 0.7);
        memcpy(&sa, &sc, sizeof sc);
        rm_pk_step_in in = {rhos[k], 1e-4, dEs[k], 1e-6, 0.02, 0.01, 1e-9};
        for (int step = 0; step < 300; step++) {
            rm_pk_step_out oc, oa;
            int nc = rm_pk_advance_c(&p, &sc, &in, &oc);
            int na = rm_pk_advance_asm(&p, &sa, &in, &oa);
            if (nc != na || memcmp(&sc, &sa, sizeof sc) != 0 || oc.energy != oa.energy ||
                oc.rho_end != oa.rho_end) {
                CHECK(0, "pk asm/C mismatch case %zu step %d: n %.17g vs %.17g, substeps %d vs %d",
                      k, step, sc.n, sa.n, nc, na);
                break;
            }
            in.rho0 = oc.rho_end;
        }
    }
    printf("  point kinetics: asm and C agree bit for bit\n");
}

typedef void (*tri_fn)(size_t, size_t, size_t, const double *, const double *, const double *,
                       const double *, double *, double *);

static void test_tridiag_equivalence(void)
{
    const size_t nl = 23, n = 17, stride = 25;
    size_t sz = n * stride;
    double *a = malloc(sz * sizeof(double)), *b = malloc(sz * sizeof(double));
    double *c = malloc(sz * sizeof(double)), *d = malloc(sz * sizeof(double));
    double *x0 = calloc(sz, sizeof(double)), *x1 = calloc(sz, sizeof(double));
    double *x2 = calloc(sz, sizeof(double)), *w = calloc(sz, sizeof(double));
    srand(12345);
    for (size_t i = 0; i < sz; i++) {
        a[i] = -1.0 - rand() / (double)RAND_MAX;
        c[i] = -1.0 - rand() / (double)RAND_MAX;
        b[i] = 4.5 + rand() / (double)RAND_MAX;
        d[i] = rand() / (double)RAND_MAX - 0.5;
    }
    rm_tridiag_batch_c(nl, n, stride, a, b, c, d, x0, w);
    rm_tridiag_batch_sse2(nl, n, stride, a, b, c, d, x1, w);
    int ok_sse = 1, ok_avx = 1;
    for (size_t k = 0; k < n; k++)
        for (size_t j = 0; j < nl; j++)
            if (x0[k * stride + j] != x1[k * stride + j]) ok_sse = 0;
    CHECK(ok_sse, "SSE2 tridiagonal differs from C");
    __builtin_cpu_init();
    if (__builtin_cpu_supports("avx")) {
        rm_tridiag_batch_avx(nl, n, stride, a, b, c, d, x2, w);
        for (size_t k = 0; k < n; k++)
            for (size_t j = 0; j < nl; j++)
                if (x0[k * stride + j] != x2[k * stride + j]) ok_avx = 0;
        CHECK(ok_avx, "AVX tridiagonal differs from C");
    }
    /* residual of the C solution */
    double worst = 0;
    for (size_t j = 0; j < nl; j++)
        for (size_t k = 0; k < n; k++) {
            size_t o = k * stride + j;
            double r = b[o] * x0[o] - d[o];
            if (k > 0) r += a[o] * x0[o - stride];
            if (k + 1 < n) r += c[o] * x0[o + stride];
            if (fabs(r) > worst) worst = fabs(r);
        }
    CHECK(worst < 1e-12, "tridiagonal residual %g", worst);
    /* x aliasing d */
    memcpy(x1, d, sz * sizeof(double));
    rm_tridiag_batch(nl, n, stride, a, b, c, x1, x1, w);
    int ok_alias = 1;
    for (size_t k = 0; k < n; k++)
        for (size_t j = 0; j < nl; j++)
            if (x0[k * stride + j] != x1[k * stride + j]) ok_alias = 0;
    CHECK(ok_alias, "in-place solve differs");
    printf("  tridiagonal: C, SSE2%s agree bit for bit (residual %.2g)\n",
           ok_avx ? " and AVX" : "", worst);
    free(a); free(b); free(c); free(d); free(x0); free(x1); free(x2); free(w);
}
#endif

int main(void)
{
    printf("kernel backend: %s\n", rm_kernels_backend_name());
    test_pk_physics();
#if defined(RMELM_HAVE_ASM)
    test_pk_asm_equivalence();
    test_tridiag_equivalence();
#endif
    return TEST_REPORT();
}
