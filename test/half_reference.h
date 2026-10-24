// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The oracle binary32 to binary16 conversion is checked against: rounding
// done in double, which holds every float exactly. (The compiler's
// _Float16 agreed on every float under GCC 14; it is not linked here, as
// Clang's needs compiler-rt.) And a binary16 grid that converts floats
// through the public API: a host gives it floats, and its view hands back
// what it stores.

#ifndef MAUL_VFX_TEST_HALF_REFERENCE_H
#define MAUL_VFX_TEST_HALF_REFERENCE_H

#include "test_harness.h"

#include "maul-vfx/grid.h"

#include <math.h>
#include <string.h>

// The half nearest to value, by arithmetic on doubles: the value's
// quantum among halves (2^-24 below 2^-14, else 2^(e-10)), rint (ties to
// even in the default mode) of the value in quanta, then the encoding.
static inline uint16_t ReferenceHalf(float value)
{
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof bits);
    uint16_t sign = (uint16_t)((bits >> 16u) & 0x8000u);
    if (isnan(value))
    {
        return (uint16_t)(sign | 0x7E00u);
    }
    double magnitude = fabs((double)value);
    if (magnitude >= 65520.0)
    {
        return (uint16_t)(sign | 0x7C00u);
    }
    int exponent = 0;
    (void)frexp(magnitude, &exponent);
    // frexp gives magnitude = m * 2^exponent with m in [0.5, 1).
    int unbiased = exponent - 1;
    double quantum = unbiased < -14 ? ldexp(1.0, -24) : ldexp(1.0, unbiased - 10);
    double units = rint(magnitude / quantum);
    double rounded = units * quantum;
    if (rounded >= 65520.0)
    {
        return (uint16_t)(sign | 0x7C00u);
    }
    if (rounded < ldexp(1.0, -14))
    {
        return (uint16_t)(sign | (uint16_t)rint(rounded / ldexp(1.0, -24)));
    }
    int roundedExponent = 0;
    double fraction = frexp(rounded, &roundedExponent);
    uint16_t field = (uint16_t)(roundedExponent - 1 + 15);
    uint16_t mantissa = (uint16_t)rint((fraction * 2.0 - 1.0) * 1024.0);
    return (uint16_t)(sign | (uint16_t)(field << 10u) | mantissa);
}

// A binary16 grid of HALF_BATCH points of one component, and the floats
// waiting to be stored in it.
#define HALF_BATCH (1 << 16)

typedef struct HalfGrid
{
    mvfxContext* context;
    mvfxGridId grid;
    float floats[HALF_BATCH];
    int32_t pending;
    uint64_t checked;
    uint64_t differing;
} HalfGrid;

static inline void OpenHalfGrid(HalfGrid* h)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &h->context) == mvfx_success, "context");
    static float zeros[HALF_BATCH];
    mvfxGridDef def = mvfxDefaultGridDef();
    def.size[0] = 256;
    def.size[1] = HALF_BATCH / 256;
    def.size[2] = 1;
    def.format = mvfx_gridFloat16;
    def.values = zeros;
    CHECK(mvfxCreateGrid(h->context, &def, &h->grid) == mvfx_success, "a binary16 grid");
    h->pending = 0;
}

// Stores the pending floats (the rest of the batch 0) and compares what
// the grid's view holds with the oracle's halves.
static inline void FlushHalfGrid(HalfGrid* h)
{
    if (h->pending == 0)
    {
        return;
    }
    for (int32_t i = h->pending; i < HALF_BATCH; ++i)
    {
        h->floats[i] = 0.0f;
    }
    mvfxGridView view;
    CHECK(mvfxGrid_SetValues(h->context, h->grid, h->floats, HALF_BATCH) == mvfx_success &&
              mvfxGrid_GetView(h->context, h->grid, &view) == mvfx_success &&
              view.format == mvfx_gridFloat16 && view.bytes == 2 * HALF_BATCH,
          "stored as binary16");
    const uint16_t* halves = view.values;
    for (int32_t i = 0; i < h->pending; ++i)
    {
        h->differing += halves[i] != ReferenceHalf(h->floats[i]) ? 1 : 0;
    }
    h->checked += (uint64_t)h->pending;
    h->pending = 0;
}

// Queues a float, given by its bits, if a grid takes it (finite, at most
// 65,504 in size); whether it did.
static inline bool QueueHalf(HalfGrid* h, uint32_t bits)
{
    float value = 0;
    memcpy(&value, &bits, sizeof value);
    if (!(fabsf(value) <= 65504.0f))
    {
        return false;
    }
    h->floats[h->pending++] = value;
    if (h->pending == HALF_BATCH)
    {
        FlushHalfGrid(h);
    }
    return true;
}

static inline void CloseHalfGrid(HalfGrid* h)
{
    FlushHalfGrid(h);
    mvfxDestroyContext(h->context);
}

#endif
