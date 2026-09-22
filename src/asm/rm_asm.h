/* Shared preprocessor glue for the GAS assembly kernels (System V AMD64 ABI). */
#ifndef RM_ASM_H
#define RM_ASM_H

#if defined(__APPLE__)
#define SYM(x) _##x
#define FUNC_BEGIN(x) .globl SYM(x); .p2align 4; SYM(x):
#define FUNC_END(x)
#define RODATA .section __TEXT,__const
#elif defined(_WIN32)
#define SYM(x) x
#define FUNC_BEGIN(x) .globl x; .p2align 4; x:
#define FUNC_END(x)
#define RODATA .section .rdata,"dr"
#else
#define SYM(x) x
#define FUNC_BEGIN(x) .globl x; .type x, @function; .p2align 4; x:
#define FUNC_END(x) .size x, .-x
#define RODATA .section .rodata
#endif

#endif
