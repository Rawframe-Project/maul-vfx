// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A host's floating-point mode for the tests that check the library runs
// in its own whatever the host's: flush-to-zero, denormals-are-zero
// (x86) and round toward zero set on the calling thread, read back, and
// restored. HOST_MODE_SETTABLE is 0 where the tests cannot set it.

#ifndef MAUL_VFX_TEST_HOST_MODE_H
#define MAUL_VFX_TEST_HOST_MODE_H

#include <stdint.h>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#define HOST_MODE_SETTABLE 1
// Flush-to-zero, denormals-are-zero and round toward zero.
static inline uint64_t SetHostMode(void)
{
    uint32_t saved = _mm_getcsr();
    _mm_setcsr(saved | 0x8000u | 0x0040u | 0x6000u);
    return saved;
}
static inline uint64_t ReadHostMode(void)
{
    return _mm_getcsr();
}
static inline void RestoreHostMode(uint64_t saved)
{
    _mm_setcsr((uint32_t)saved);
}
#elif (defined(__aarch64__) || defined(_M_ARM64)) && !(defined(_MSC_VER) && !defined(__clang__))
#define HOST_MODE_SETTABLE 1
static inline uint64_t ReadHostMode(void)
{
    uint64_t value = 0;
    __asm__ volatile("mrs %0, fpcr" : "=r"(value));
    return value;
}
// Flush-to-zero and round toward zero.
static inline uint64_t SetHostMode(void)
{
    uint64_t saved = ReadHostMode();
    uint64_t wanted = saved | (1u << 24) | (3u << 22);
    __asm__ volatile("msr fpcr, %0" : : "r"(wanted));
    return saved;
}
static inline void RestoreHostMode(uint64_t saved)
{
    __asm__ volatile("msr fpcr, %0" : : "r"(saved));
}
#else
#define HOST_MODE_SETTABLE 0
#endif

#endif
