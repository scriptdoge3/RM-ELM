/*
 * Batched tridiagonal solvers (Thomas algorithm), structure-of-arrays layout.
 *
 * void rm_tridiag_batch_{avx,sse2}(size_t nlines,   rdi
 *                                  size_t n,        rsi
 *                                  size_t stride,   rdx  (in elements)
 *                                  const double *a, rcx
 *                                  const double *b, r8
 *                                  const double *c, r9
 *                                  const double *d, [stack]
 *                                  double *x,       [stack]
 *                                  double *cp)      [stack]
 *
 * Element k of line j is at index k*stride + j, so a vector load at fixed k
 * picks up the same row of 4 (AVX) or 2 (SSE2) neighbouring systems. Lines
 * left over at the end are solved one at a time with scalar code. No FMA is
 * used, so every lane gives the same bits as rm_tridiag_batch_c().
 *
 *   forward:  m = b_k - a_k*cp_{k-1};  cp_k = c_k/m;  dp_k = (d_k - a_k*dp_{k-1})/m
 *   backward: x_k = dp_k - cp_k*x_{k+1}
 *
 * Register map
 *   r10 d   r11 x   r12 cp   rdx stride in bytes
 *   rbx byte offset of the current line block   r13 end of the vector part
 *   rax byte offset of the current row          r14 row counter
 */
#include "rm_asm.h"

    .intel_syntax noprefix
    .text

/* ------------------------------------------------------------------ AVX -- */
FUNC_BEGIN(rm_tridiag_batch_avx)
    push    rbx
    push    r12
    push    r13
    push    r14
    mov     r10, QWORD PTR [rsp + 40]
    mov     r11, QWORD PTR [rsp + 48]
    mov     r12, QWORD PTR [rsp + 56]
    test    rsi, rsi
    jz      avx_done
    shl     rdx, 3
    xor     ebx, ebx
    mov     r13, rdi
    and     r13, -4
    shl     r13, 3

avx_block:
    cmp     rbx, r13
    jae     avx_scalar_start
    mov     rax, rbx
    vmovupd ymm0, YMMWORD PTR [r8 + rax]         /* b0 */
    vmovupd ymm1, YMMWORD PTR [r9 + rax]
    vdivpd  ymm1, ymm1, ymm0                     /* cp0 = c0/b0 */
    vmovupd YMMWORD PTR [r12 + rax], ymm1
    vmovupd ymm2, YMMWORD PTR [r10 + rax]
    vdivpd  ymm2, ymm2, ymm0                     /* dp0 = d0/b0 */
    vmovupd YMMWORD PTR [r11 + rax], ymm2
    mov     r14, 1
avx_fwd:
    cmp     r14, rsi
    jae     avx_bwd_start
    add     rax, rdx
    vmovupd ymm3, YMMWORD PTR [rcx + rax]        /* a_k */
    vmulpd  ymm4, ymm3, ymm1                     /* a_k*cp_{k-1} */
    vmovupd ymm0, YMMWORD PTR [r8 + rax]
    vsubpd  ymm0, ymm0, ymm4                     /* m */
    vmovupd ymm5, YMMWORD PTR [r9 + rax]
    vdivpd  ymm1, ymm5, ymm0                     /* cp_k */
    vmovupd YMMWORD PTR [r12 + rax], ymm1
    vmulpd  ymm4, ymm3, ymm2                     /* a_k*dp_{k-1} */
    vmovupd ymm5, YMMWORD PTR [r10 + rax]
    vsubpd  ymm5, ymm5, ymm4
    vdivpd  ymm2, ymm5, ymm0                     /* dp_k */
    vmovupd YMMWORD PTR [r11 + rax], ymm2
    inc     r14
    jmp     avx_fwd
avx_bwd_start:
    mov     r14, rsi
    dec     r14
avx_bwd:
    test    r14, r14
    jz      avx_next
    sub     rax, rdx
    dec     r14
    vmovupd ymm3, YMMWORD PTR [r12 + rax]
    vmulpd  ymm3, ymm3, ymm2                     /* cp_k*x_{k+1} */
    vmovupd ymm4, YMMWORD PTR [r11 + rax]
    vsubpd  ymm2, ymm4, ymm3                     /* x_k */
    vmovupd YMMWORD PTR [r11 + rax], ymm2
    jmp     avx_bwd
avx_next:
    add     rbx, 32
    jmp     avx_block

avx_scalar_start:
    mov     r13, rdi
    shl     r13, 3
avx_scalar_line:
    cmp     rbx, r13
    jae     avx_done
    mov     rax, rbx
    vmovsd  xmm0, QWORD PTR [r8 + rax]
    vmovsd  xmm1, QWORD PTR [r9 + rax]
    vdivsd  xmm1, xmm1, xmm0
    vmovsd  QWORD PTR [r12 + rax], xmm1
    vmovsd  xmm2, QWORD PTR [r10 + rax]
    vdivsd  xmm2, xmm2, xmm0
    vmovsd  QWORD PTR [r11 + rax], xmm2
    mov     r14, 1
avx_s_fwd:
    cmp     r14, rsi
    jae     avx_s_bwd_start
    add     rax, rdx
    vmovsd  xmm3, QWORD PTR [rcx + rax]
    vmulsd  xmm4, xmm3, xmm1
    vmovsd  xmm0, QWORD PTR [r8 + rax]
    vsubsd  xmm0, xmm0, xmm4
    vmovsd  xmm5, QWORD PTR [r9 + rax]
    vdivsd  xmm1, xmm5, xmm0
    vmovsd  QWORD PTR [r12 + rax], xmm1
    vmulsd  xmm4, xmm3, xmm2
    vmovsd  xmm5, QWORD PTR [r10 + rax]
    vsubsd  xmm5, xmm5, xmm4
    vdivsd  xmm2, xmm5, xmm0
    vmovsd  QWORD PTR [r11 + rax], xmm2
    inc     r14
    jmp     avx_s_fwd
avx_s_bwd_start:
    mov     r14, rsi
    dec     r14
avx_s_bwd:
    test    r14, r14
    jz      avx_s_next
    sub     rax, rdx
    dec     r14
    vmovsd  xmm3, QWORD PTR [r12 + rax]
    vmulsd  xmm3, xmm3, xmm2
    vmovsd  xmm4, QWORD PTR [r11 + rax]
    vsubsd  xmm2, xmm4, xmm3
    vmovsd  QWORD PTR [r11 + rax], xmm2
    jmp     avx_s_bwd
avx_s_next:
    add     rbx, 8
    jmp     avx_scalar_line

avx_done:
    vzeroupper
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret
FUNC_END(rm_tridiag_batch_avx)

/* ----------------------------------------------------------------- SSE2 -- */
FUNC_BEGIN(rm_tridiag_batch_sse2)
    push    rbx
    push    r12
    push    r13
    push    r14
    mov     r10, QWORD PTR [rsp + 40]
    mov     r11, QWORD PTR [rsp + 48]
    mov     r12, QWORD PTR [rsp + 56]
    test    rsi, rsi
    jz      sse_done
    shl     rdx, 3
    xor     ebx, ebx
    mov     r13, rdi
    and     r13, -2
    shl     r13, 3

sse_block:
    cmp     rbx, r13
    jae     sse_scalar_start
    mov     rax, rbx
    movupd  xmm0, XMMWORD PTR [r8 + rax]
    movupd  xmm1, XMMWORD PTR [r9 + rax]
    divpd   xmm1, xmm0
    movupd  XMMWORD PTR [r12 + rax], xmm1
    movupd  xmm2, XMMWORD PTR [r10 + rax]
    divpd   xmm2, xmm0
    movupd  XMMWORD PTR [r11 + rax], xmm2
    mov     r14, 1
sse_fwd:
    cmp     r14, rsi
    jae     sse_bwd_start
    add     rax, rdx
    movupd  xmm3, XMMWORD PTR [rcx + rax]
    movapd  xmm4, xmm3
    mulpd   xmm4, xmm1
    movupd  xmm0, XMMWORD PTR [r8 + rax]
    subpd   xmm0, xmm4
    movupd  xmm1, XMMWORD PTR [r9 + rax]
    divpd   xmm1, xmm0
    movupd  XMMWORD PTR [r12 + rax], xmm1
    mulpd   xmm3, xmm2
    movupd  xmm5, XMMWORD PTR [r10 + rax]
    subpd   xmm5, xmm3
    divpd   xmm5, xmm0
    movapd  xmm2, xmm5
    movupd  XMMWORD PTR [r11 + rax], xmm2
    inc     r14
    jmp     sse_fwd
sse_bwd_start:
    mov     r14, rsi
    dec     r14
sse_bwd:
    test    r14, r14
    jz      sse_next
    sub     rax, rdx
    dec     r14
    movupd  xmm3, XMMWORD PTR [r12 + rax]
    mulpd   xmm3, xmm2
    movupd  xmm2, XMMWORD PTR [r11 + rax]
    subpd   xmm2, xmm3
    movupd  XMMWORD PTR [r11 + rax], xmm2
    jmp     sse_bwd
sse_next:
    add     rbx, 16
    jmp     sse_block

sse_scalar_start:
    mov     r13, rdi
    shl     r13, 3
sse_scalar_line:
    cmp     rbx, r13
    jae     sse_done
    mov     rax, rbx
    movsd   xmm0, QWORD PTR [r8 + rax]
    movsd   xmm1, QWORD PTR [r9 + rax]
    divsd   xmm1, xmm0
    movsd   QWORD PTR [r12 + rax], xmm1
    movsd   xmm2, QWORD PTR [r10 + rax]
    divsd   xmm2, xmm0
    movsd   QWORD PTR [r11 + rax], xmm2
    mov     r14, 1
sse_s_fwd:
    cmp     r14, rsi
    jae     sse_s_bwd_start
    add     rax, rdx
    movsd   xmm3, QWORD PTR [rcx + rax]
    movapd  xmm4, xmm3
    mulsd   xmm4, xmm1
    movsd   xmm0, QWORD PTR [r8 + rax]
    subsd   xmm0, xmm4
    movsd   xmm1, QWORD PTR [r9 + rax]
    divsd   xmm1, xmm0
    movsd   QWORD PTR [r12 + rax], xmm1
    mulsd   xmm3, xmm2
    movsd   xmm5, QWORD PTR [r10 + rax]
    subsd   xmm5, xmm3
    divsd   xmm5, xmm0
    movapd  xmm2, xmm5
    movsd   QWORD PTR [r11 + rax], xmm2
    inc     r14
    jmp     sse_s_fwd
sse_s_bwd_start:
    mov     r14, rsi
    dec     r14
sse_s_bwd:
    test    r14, r14
    jz      sse_s_next
    sub     rax, rdx
    dec     r14
    movsd   xmm3, QWORD PTR [r12 + rax]
    mulsd   xmm3, xmm2
    movsd   xmm2, QWORD PTR [r11 + rax]
    subsd   xmm2, xmm3
    movsd   QWORD PTR [r11 + rax], xmm2
    jmp     sse_s_bwd
sse_s_next:
    add     rbx, 8
    jmp     sse_scalar_line

sse_done:
    pop     r14
    pop     r13
    pop     r12
    pop     rbx
    ret
FUNC_END(rm_tridiag_batch_sse2)

#if defined(__linux__) && defined(__ELF__)
    .section .note.GNU-stack,"",@progbits
#endif
