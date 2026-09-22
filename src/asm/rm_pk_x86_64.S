/*
 * rm_pk_advance_asm - adaptive implicit-Euler point kinetics, 6 delayed groups.
 *
 * int rm_pk_advance_asm(const rm_pk_params *p,     rdi
 *                       rm_pk_state *s,            rsi
 *                       const rm_pk_step_in *in,   rdx
 *                       rm_pk_step_out *out)       rcx
 *
 * Mirrors rm_pk_advance_c() operation for operation (scalar SSE2, no FMA),
 * so the two agree to the last bit. See rm_kernels.h for the struct layout.
 *
 * Per substep of length h, with d_i = 1 + h*lambda_i and hL = h/Lambda:
 *
 *   rho   = rho0 + drho_dt*(t+h) + drho_dE*(Ex + h*(n - n0))
 *   n'    = (n + hL*sum(lambda_i*y_i/d_i) + h*S) / (1 - hL*(rho - sum(beta_i/d_i)))
 *   y_i'  = (y_i + h*beta_i*n') / d_i
 *
 * A substep is rejected and halved when the denominator goes non-positive
 * (step longer than the prompt period) or n changes by more than max_rel.
 * Quiet substeps (< max_rel/4) double the next step.
 *
 * Register map
 *   xmm8  n        xmm9  n0       xmm10 t        xmm11 E (integral of n)
 *   xmm12 Ex       xmm13 h        xmm14 rho      xmm7  hL
 *   xmm2  sum b/d  xmm3  sum l*y/d xmm4 denom    xmm5  n'     xmm6 rel
 *   r8d   accepted substeps        r9   group index
 *   [rsp + 8*i]    d_i
 */
#include "rm_asm.h"

#define P_BETA    0
#define P_LAMBDA  48
#define P_LAMBDA_ 96
#define S_N       0
#define S_Y       8
#define I_RHO0    0
#define I_DRHODT  8
#define I_DRHODE  16
#define I_SRC     24
#define I_DT      32
#define I_MAXREL  40
#define I_HMIN    48
#define O_E       0
#define O_RHO     8
#define O_COUNT   16
#define MAX_SUBSTEPS 200000

    .intel_syntax noprefix

    RODATA
    .p2align 4
pk_absmask:
    .quad 0x7fffffffffffffff, 0x7fffffffffffffff
pk_one:
    .double 1.0
pk_half:
    .double 0.5
pk_quarter:
    .double 0.25
pk_tol:
    .double 1.0e-12

    .text
FUNC_BEGIN(rm_pk_advance_asm)
    push    rbp
    mov     rbp, rsp
    sub     rsp, 64

    movsd   xmm8, QWORD PTR [rsi + S_N]          /* n  */
    movapd  xmm9, xmm8                           /* n0 */
    xorpd   xmm10, xmm10                         /* t  */
    xorpd   xmm11, xmm11                         /* E  */
    xorpd   xmm12, xmm12                         /* Ex */
    movsd   xmm13, QWORD PTR [rdx + I_DT]        /* h = dt */
    movsd   xmm14, QWORD PTR [rdx + I_RHO0]      /* rho */
    xor     r8d, r8d

pk_outer:
    /* rem = dt - t; stop when rem <= 1e-12*dt */
    movsd   xmm0, QWORD PTR [rdx + I_DT]
    subsd   xmm0, xmm10
    movsd   xmm1, QWORD PTR [rip + pk_tol]
    mulsd   xmm1, QWORD PTR [rdx + I_DT]
    comisd  xmm0, xmm1
    jbe     pk_done
    /* h = min(h, rem) */
    comisd  xmm13, xmm0
    jbe     pk_try
    movapd  xmm13, xmm0

pk_try:
    movapd  xmm7, xmm13
    divsd   xmm7, QWORD PTR [rdi + P_LAMBDA_]   /* hL */

    /* rho = rho0 + drho_dt*(t+h) + drho_dE*(Ex + h*(n-n0)) */
    movapd  xmm0, xmm10
    addsd   xmm0, xmm13
    mulsd   xmm0, QWORD PTR [rdx + I_DRHODT]
    movsd   xmm14, QWORD PTR [rdx + I_RHO0]
    addsd   xmm14, xmm0
    movapd  xmm1, xmm8
    subsd   xmm1, xmm9
    mulsd   xmm1, xmm13
    addsd   xmm1, xmm12
    mulsd   xmm1, QWORD PTR [rdx + I_DRHODE]
    addsd   xmm14, xmm1

    xorpd   xmm2, xmm2                           /* sum beta/d     */
    xorpd   xmm3, xmm3                           /* sum lambda*y/d */
    xor     r9d, r9d
pk_groups:
    movsd   xmm0, QWORD PTR [rdi + P_LAMBDA + r9*8]
    mulsd   xmm0, xmm13
    addsd   xmm0, QWORD PTR [rip + pk_one]       /* d_i = 1 + h*lambda_i */
    movsd   QWORD PTR [rsp + r9*8], xmm0
    movsd   xmm1, QWORD PTR [rdi + P_BETA + r9*8]
    divsd   xmm1, xmm0
    addsd   xmm2, xmm1
    movsd   xmm1, QWORD PTR [rdi + P_LAMBDA + r9*8]
    mulsd   xmm1, QWORD PTR [rsi + S_Y + r9*8]
    divsd   xmm1, xmm0
    addsd   xmm3, xmm1
    inc     r9d
    cmp     r9d, 6
    jl      pk_groups

    /* denom = 1 - hL*(rho - sb) */
    movapd  xmm0, xmm14
    subsd   xmm0, xmm2
    mulsd   xmm0, xmm7
    movsd   xmm4, QWORD PTR [rip + pk_one]
    subsd   xmm4, xmm0

    /* n' = (n + hL*sy + h*S) / denom */
    movapd  xmm5, xmm7
    mulsd   xmm5, xmm3
    addsd   xmm5, xmm8
    movapd  xmm6, xmm13
    mulsd   xmm6, QWORD PTR [rdx + I_SRC]
    addsd   xmm5, xmm6
    divsd   xmm5, xmm4

    /* rel = |n' - n| / max(n, n') */
    movapd  xmm6, xmm5
    subsd   xmm6, xmm8
    andpd   xmm6, XMMWORD PTR [rip + pk_absmask]
    movapd  xmm0, xmm8
    comisd  xmm8, xmm5
    ja      1f
    movapd  xmm0, xmm5
1:
    divsd   xmm6, xmm0

    /* accept if (denom > 0 && rel <= max_rel) || h <= h_min */
    xorpd   xmm1, xmm1
    comisd  xmm4, xmm1
    jbe     pk_check_hmin
    comisd  xmm6, QWORD PTR [rdx + I_MAXREL]
    jbe     pk_accept
pk_check_hmin:
    comisd  xmm13, QWORD PTR [rdx + I_HMIN]
    jbe     pk_accept
    mulsd   xmm13, QWORD PTR [rip + pk_half]
    jmp     pk_try

pk_accept:
    xor     r9d, r9d
pk_update_groups:
    movapd  xmm0, xmm13
    mulsd   xmm0, QWORD PTR [rdi + P_BETA + r9*8]
    mulsd   xmm0, xmm5
    addsd   xmm0, QWORD PTR [rsi + S_Y + r9*8]
    divsd   xmm0, QWORD PTR [rsp + r9*8]
    movsd   QWORD PTR [rsi + S_Y + r9*8], xmm0
    inc     r9d
    cmp     r9d, 6
    jl      pk_update_groups

    /* nav = 0.5*(n+n'); E += h*nav; Ex += h*(nav-n0); t += h; n = n' */
    movapd  xmm0, xmm8
    addsd   xmm0, xmm5
    mulsd   xmm0, QWORD PTR [rip + pk_half]
    movapd  xmm1, xmm13
    mulsd   xmm1, xmm0
    addsd   xmm11, xmm1
    movapd  xmm1, xmm0
    subsd   xmm1, xmm9
    mulsd   xmm1, xmm13
    addsd   xmm12, xmm1
    addsd   xmm10, xmm13
    movapd  xmm8, xmm5

    /* grow the step after a quiet substep */
    movsd   xmm0, QWORD PTR [rip + pk_quarter]
    mulsd   xmm0, QWORD PTR [rdx + I_MAXREL]
    comisd  xmm6, xmm0
    jae     2f
    addsd   xmm13, xmm13
2:
    inc     r8d
    cmp     r8d, MAX_SUBSTEPS
    jl      pk_outer

pk_done:
    movsd   QWORD PTR [rsi + S_N], xmm8
    movsd   QWORD PTR [rcx + O_E], xmm11
    movsd   QWORD PTR [rcx + O_RHO], xmm14
    mov     DWORD PTR [rcx + O_COUNT], r8d
    mov     eax, r8d
    leave
    ret
FUNC_END(rm_pk_advance_asm)

#if defined(__linux__) && defined(__ELF__)
    .section .note.GNU-stack,"",@progbits
#endif
