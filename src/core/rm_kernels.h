/*
 * Numerical hot loops. Each kernel has a portable C reference version and,
 * on x86-64 with GCC/Clang, a hand-written assembly version in src/asm/.
 * The assembly performs exactly the same IEEE operations in the same order,
 * so both produce bit-identical results (checked by tests/test_kernels.c).
 */
#ifndef RM_KERNELS_H
#define RM_KERNELS_H

#include <stddef.h>

#if defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#define RM_ASM_ABI __attribute__((sysv_abi))
#else
#define RM_ASM_ABI
#endif

#define RM_PK_GROUPS 6
#define RM_PK_MAX_SUBSTEPS 200000

/* Point kinetics in reduced form: y_i = Lambda * C_i, so at equilibrium
 * y_i = beta_i * n / lambda_i and every quantity is in units of n. */
typedef struct {
    double beta[RM_PK_GROUPS];   /* offset 0   */
    double lambda[RM_PK_GROUPS]; /* offset 48  */
    double Lambda;               /* offset 96, prompt neutron generation time [s] */
} rm_pk_params;

typedef struct {
    double n;                    /* offset 0, normalised fission power (1 = rated) */
    double y[RM_PK_GROUPS];      /* offset 8  */
} rm_pk_state;

typedef struct {
    double rho0;     /* offset 0:  reactivity at the start of the step */
    double drho_dt;  /* offset 8:  linear reactivity ramp across the step [1/s] */
    double drho_dE;  /* offset 16: prompt feedback per unit excess energy [1/(rated*s)] */
    double source;   /* offset 24: external neutron source [n/s] */
    double dt;       /* offset 32 */
    double max_rel;  /* offset 40: max relative change of n in one substep */
    double h_min;    /* offset 48: smallest allowed substep */
} rm_pk_step_in;

typedef struct {
    double energy;   /* offset 0: integral of n over the step [rated*s] */
    double rho_end;  /* offset 8: reactivity in the last substep */
    int substeps;    /* offset 16 */
} rm_pk_step_out;

/* Advance the kinetics over in->dt with adaptive implicit-Euler substeps.
 * Returns the number of accepted substeps. */
int rm_pk_advance(const rm_pk_params *p, rm_pk_state *s, const rm_pk_step_in *in,
                  rm_pk_step_out *out);
int rm_pk_advance_c(const rm_pk_params *p, rm_pk_state *s, const rm_pk_step_in *in,
                    rm_pk_step_out *out);

/* Solve many independent tridiagonal systems at once (Thomas algorithm).
 * Structure-of-arrays layout: element k of line j lives at [k*stride + j].
 * a = sub-diagonal (a[0] unused), b = diagonal, c = super-diagonal
 * (c[n-1] unused), d = right-hand side. The solution goes to x, and
 * cp_work (n*stride doubles) is scratch. x may alias d. */
void rm_tridiag_batch(size_t nlines, size_t n, size_t stride,
                      const double *a, const double *b, const double *c,
                      const double *d, double *x, double *cp_work);
void rm_tridiag_batch_c(size_t nlines, size_t n, size_t stride,
                        const double *a, const double *b, const double *c,
                        const double *d, double *x, double *cp_work);

#if defined(RMELM_HAVE_ASM)
RM_ASM_ABI int rm_pk_advance_asm(const rm_pk_params *p, rm_pk_state *s,
                                 const rm_pk_step_in *in, rm_pk_step_out *out);
RM_ASM_ABI void rm_tridiag_batch_sse2(size_t nlines, size_t n, size_t stride,
                                      const double *a, const double *b, const double *c,
                                      const double *d, double *x, double *cp_work);
RM_ASM_ABI void rm_tridiag_batch_avx(size_t nlines, size_t n, size_t stride,
                                     const double *a, const double *b, const double *c,
                                     const double *d, double *x, double *cp_work);
#endif

typedef enum {
    RM_BACKEND_C = 0,
    RM_BACKEND_SSE2 = 1,
    RM_BACKEND_AVX = 2
} rm_backend;

/* Select the best backend for this CPU (called lazily). Tests may force
 * a lower one; requests above what the CPU supports are clamped. */
rm_backend rm_kernels_backend(void);
void rm_kernels_set_backend(rm_backend b);
const char *rm_kernels_backend_name(void);

#endif
