// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Counter-based random numbers: pcg4d, the four-in, four-out hash of
// Jarzynski and Olano ("Hash Functions for GPU Rendering", JCGT 9(3),
// 2020). A value depends only on its four coordinates (a seed, an emitter
// and module salt, a particle's serial and a counter), never on the order
// values are drawn in, and it is 32-bit integer arithmetic alone, so a GPU
// computes the same bits.

#ifndef MAUL_VFX_SRC_RANDOM_H
#define MAUL_VFX_SRC_RANDOM_H

#include <stdint.h>

// Four 32-bit words: a hash's coordinates or its outputs.
typedef struct mvfxWords4
{
    uint32_t x;
    uint32_t y;
    uint32_t z;
    uint32_t w;
} mvfxWords4;

// One round of pcg4d's permutation: each word gains the product of two
// others, in order, each step reading the words already updated.
static inline mvfxWords4 mvfxPcg4dMix(mvfxWords4 v)
{
    v.x += v.y * v.w;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    v.w += v.y * v.z;
    return v;
}

// pcg4d: a linear congruential step on each word, a round, a xor of each
// word's high half into its low half, and a second round.
static inline mvfxWords4 mvfxPcg4d(mvfxWords4 v)
{
    v.x = v.x * 1664525u + 1013904223u;
    v.y = v.y * 1664525u + 1013904223u;
    v.z = v.z * 1664525u + 1013904223u;
    v.w = v.w * 1664525u + 1013904223u;
    v = mvfxPcg4dMix(v);
    v.x ^= v.x >> 16u;
    v.y ^= v.y >> 16u;
    v.z ^= v.z >> 16u;
    v.w ^= v.w >> 16u;
    return mvfxPcg4dMix(v);
}

// A word as a float in [0, 1): its top 24 bits times 2^-24, exact in
// binary32, so every value is a multiple of 2^-24 and 1 is never reached.
static inline float mvfxUnitFloat(uint32_t word)
{
    return (float)(word >> 8u) * 0x1p-24f;
}

#endif
