#include "rm_kernels.h"

#include <math.h>

/* ---- point kinetics ---------------------------------------------------- */

int rm_pk_advance_c(const rm_pk_params *p, rm_pk_state *s, const rm_pk_step_in *in,
                    rm_pk_step_out *out)
{
    double n = s->n;
    const double n0 = s->n;
    double t = 0.0, E = 0.0, Ex = 0.0;
    double h = in->dt;
    double rho = in->rho0;
    double d[RM_PK_GROUPS];
    int count = 0;

    for (;;) {
        double rem = in->dt - t;
        if (rem <= 1e-12 * in->dt) break;
        if (h > rem) h = rem;
        for (;;) {
            double hL = h / p->Lambda;
            rho = in->rho0 + in->drho_dt * (t + h) + in->drho_dE * (Ex + h * (n - n0));
            double sb = 0.0, sy = 0.0;
            for (int i = 0; i < RM_PK_GROUPS; i++) {
                d[i] = 1.0 + h * p->lambda[i];
                sb += p->beta[i] / d[i];
                sy += p->lambda[i] * s->y[i] / d[i];
            }
            double denom = 1.0 - hL * (rho - sb);
            double nn = (n + hL * sy + h * in->source) / denom;
            double big = n > nn ? n : nn;
            double rel = fabs(nn - n) / big;
            if ((denom > 0.0 && rel <= in->max_rel) || h <= in->h_min) {
                for (int i = 0; i < RM_PK_GROUPS; i++)
                    s->y[i] = (s->y[i] + h * p->beta[i] * nn) / d[i];
                double nav = 0.5 * (n + nn);
                E += h * nav;
                Ex += h * (nav - n0);
                t += h;
                n = nn;
                if (rel < 0.25 * in->max_rel) h = h + h;
                break;
            }
            h = h * 0.5;
        }
        if (++count >= RM_PK_MAX_SUBSTEPS) break;
    }
    s->n = n;
    out->energy = E;
    out->rho_end = rho;
    out->substeps = count;
    return count;
}

/* ---- batched tridiagonal ------------------------------------------------ */

void rm_tridiag_batch_c(size_t nlines, size_t n, size_t stride,
                        const double *a, const double *b, const double *c,
                        const double *d, double *x, double *cp)
{
    if (n == 0) return;
    for (size_t j = 0; j < nlines; j++) {
        double cprev = c[j] / b[j];
        double dprev = d[j] / b[j];
        cp[j] = cprev;
        x[j] = dprev;
        for (size_t k = 1; k < n; k++) {
            size_t o = k * stride + j;
            double m = b[o] - a[o] * cprev;
            cprev = c[o] / m;
            dprev = (d[o] - a[o] * dprev) / m;
            cp[o] = cprev;
            x[o] = dprev;
        }
        double xn = dprev;
        for (size_t k = n - 1; k-- > 0;) {
            size_t o = k * stride + j;
            xn = x[o] - cp[o] * xn;
            x[o] = xn;
        }
    }
}

/* ---- dispatch ----------------------------------------------------------- */

static int g_backend = -1;
static int g_forced = -1;

static rm_backend detect(void)
{
#if defined(RMELM_HAVE_ASM)
#if defined(__GNUC__) || defined(__clang__)
    __builtin_cpu_init();
    if (__builtin_cpu_supports("avx")) return RM_BACKEND_AVX;
#endif
    return RM_BACKEND_SSE2; /* baseline for every x86-64 CPU */
#else
    return RM_BACKEND_C;
#endif
}

rm_backend rm_kernels_backend(void)
{
    if (g_backend < 0) g_backend = (int)detect();
    if (g_forced >= 0 && g_forced < g_backend) return (rm_backend)g_forced;
    return (rm_backend)g_backend;
}

void rm_kernels_set_backend(rm_backend b)
{
    g_forced = (int)b;
}

const char *rm_kernels_backend_name(void)
{
    switch (rm_kernels_backend()) {
    case RM_BACKEND_AVX: return "x86-64 asm (AVX)";
    case RM_BACKEND_SSE2: return "x86-64 asm (SSE2)";
    default: return "portable C";
    }
}

int rm_pk_advance(const rm_pk_params *p, rm_pk_state *s, const rm_pk_step_in *in,
                  rm_pk_step_out *out)
{
#if defined(RMELM_HAVE_ASM)
    if (rm_kernels_backend() != RM_BACKEND_C) return rm_pk_advance_asm(p, s, in, out);
#endif
    return rm_pk_advance_c(p, s, in, out);
}

void rm_tridiag_batch(size_t nlines, size_t n, size_t stride,
                      const double *a, const double *b, const double *c,
                      const double *d, double *x, double *cp_work)
{
#if defined(RMELM_HAVE_ASM)
    switch (rm_kernels_backend()) {
    case RM_BACKEND_AVX:
        rm_tridiag_batch_avx(nlines, n, stride, a, b, c, d, x, cp_work);
        return;
    case RM_BACKEND_SSE2:
        rm_tridiag_batch_sse2(nlines, n, stride, a, b, c, d, x, cp_work);
        return;
    default:
        break;
    }
#endif
    rm_tridiag_batch_c(nlines, n, stride, a, b, c, d, x, cp_work);
}
