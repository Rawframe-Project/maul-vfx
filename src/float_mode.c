// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The control registers: MXCSR on x86, FPCR on AArch64. WebAssembly has
// one environment, round to nearest with subnormals, and nothing to set.
// Another architecture is refused at build time: its environment is not
// known to give the library's results.

#include "float_mode.h"

#include <float.h>

#if FLT_EVAL_METHOD != 0
#error "float arithmetic must round to float (FLT_EVAL_METHOD 0)"
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>

// Flush-to-zero (bit 15), the rounding control (bits 13 and 14) and
// denormals-are-zero (bit 6); round to nearest is both rounding bits clear.
#define MXCSR_MODE_BITS 0xE040u

mvfxFloatMode mvfxEnterFloatMode(void)
{
    uint32_t saved = _mm_getcsr();
    uint32_t wanted = saved & ~(uint32_t)MXCSR_MODE_BITS;
    if (wanted != saved)
    {
        _mm_setcsr(wanted);
    }
    return (mvfxFloatMode){saved};
}

void mvfxLeaveFloatMode(mvfxFloatMode mode)
{
    if (_mm_getcsr() != (uint32_t)mode.saved)
    {
        _mm_setcsr((uint32_t)mode.saved);
    }
}

#elif defined(__aarch64__) || defined(_M_ARM64)

// Flush-to-zero (bit 24), the rounding mode (bits 22 and 23), half
// precision flush-to-zero (bit 19), and FEAT_AFP's alternate handling and
// input flush (bits 1 and 0); round to nearest is both mode bits clear.
#define FPCR_MODE_BITS 0x01C80003u

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
// FPCR as _ReadStatusReg names it: op0 3, op1 3, CRn 4, CRm 4, op2 0.
#define FPCR_REGISTER 0x5A20
static uint64_t ReadControl(void)
{
    return (uint64_t)_ReadStatusReg(FPCR_REGISTER);
}
static void WriteControl(uint64_t value)
{
    _WriteStatusReg(FPCR_REGISTER, (__int64)value);
}
#else
static uint64_t ReadControl(void)
{
    uint64_t value = 0;
    __asm__ volatile("mrs %0, fpcr" : "=r"(value));
    return value;
}
static void WriteControl(uint64_t value)
{
    __asm__ volatile("msr fpcr, %0" : : "r"(value));
}
#endif

mvfxFloatMode mvfxEnterFloatMode(void)
{
    uint64_t saved = ReadControl();
    uint64_t wanted = saved & ~(uint64_t)FPCR_MODE_BITS;
    if (wanted != saved)
    {
        WriteControl(wanted);
    }
    return (mvfxFloatMode){saved};
}

void mvfxLeaveFloatMode(mvfxFloatMode mode)
{
    if (ReadControl() != mode.saved)
    {
        WriteControl(mode.saved);
    }
}

#elif defined(__wasm__)

mvfxFloatMode mvfxEnterFloatMode(void)
{
    return (mvfxFloatMode){0};
}

void mvfxLeaveFloatMode(mvfxFloatMode mode)
{
    (void)mode;
}

#else
#error "Maul VFX knows the floating-point environment of x86, AArch64 and WebAssembly only"
#endif
