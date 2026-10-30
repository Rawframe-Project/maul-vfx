// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The statuses validators answer. A value past one of the library's named
// bounds (MVFX_MAX_*, MVFX_MIN_*) is refused with mvfx_errorLimit, which a
// program may design around; anything else wrong, NaN and infinities
// included, with mvfx_errorInvalid. A validator answers the first check
// that fails.

#ifndef MAUL_VFX_SRC_REFUSAL_H
#define MAUL_VFX_SRC_REFUSAL_H

#include "maul-vfx/base.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

// Zeroes the count items of size bytes a caller states an output buffer
// holds, so a refused call leaves no earlier answer there. A NULL or
// misaligned buffer, a count below 1 or a span no buffer could hold is
// left: that statement was itself refused.
static inline void mvfxClearSpan(void* buffer, int64_t count, int64_t size, uintptr_t alignment)
{
    if (buffer != nullptr && (uintptr_t)buffer % alignment == 0 && count > 0 && size > 0 &&
        count <= INT64_MAX / size && (uint64_t)(count * size) <= SIZE_MAX)
    {
        memset(buffer, 0, (size_t)(count * size));
    }
}

// The first of two statuses that refuses, or mvfx_success. Both are
// evaluated, so b may not read what a checks.
static inline mvfxResult mvfxFirst(mvfxResult a, mvfxResult b)
{
    return a != mvfx_success ? a : b;
}

// mvfx_success where ok, else mvfx_errorInvalid.
static inline mvfxResult mvfxRequire(bool ok)
{
    return ok ? mvfx_success : mvfx_errorInvalid;
}

// mvfx_success where a value is within a named bound, else
// mvfx_errorLimit.
static inline mvfxResult mvfxWithinBound(bool within)
{
    return within ? mvfx_success : mvfx_errorLimit;
}

// mvfx_success for a finite x within named bounds [low, high];
// mvfx_errorLimit for one past them; mvfx_errorInvalid for NaN or an
// infinity.
static inline mvfxResult mvfxBounded(float x, float low, float high)
{
    if (!isfinite(x))
    {
        return mvfx_errorInvalid;
    }
    return x >= low && x <= high ? mvfx_success : mvfx_errorLimit;
}

// mvfxBounded for each component of a vector, from -bound to bound.
static inline mvfxResult mvfxBoundedVector(const float v[3], float bound)
{
    return mvfxFirst(mvfxBounded(v[0], -bound, bound),
                     mvfxFirst(mvfxBounded(v[1], -bound, bound), mvfxBounded(v[2], -bound, bound)));
}

// mvfx_success for a finite x from floor up to a named bound;
// mvfx_errorInvalid for NaN, an infinity or x below floor, which no bound
// names; mvfx_errorLimit for x past the bound.
static inline mvfxResult mvfxUpToBound(float x, float floor, float bound)
{
    if (!isfinite(x) || x < floor)
    {
        return mvfx_errorInvalid;
    }
    return x <= bound ? mvfx_success : mvfx_errorLimit;
}

// mvfx_success for a finite x above 0 and up to a named bound;
// mvfx_errorInvalid for NaN, an infinity or x at most 0; mvfx_errorLimit
// for x past the bound.
static inline mvfxResult mvfxPositiveUpTo(float x, float bound)
{
    return x > 0.0f ? mvfxUpToBound(x, 0.0f, bound) : mvfx_errorInvalid;
}

// mvfx_success for a count from floor up to a named bound;
// mvfx_errorInvalid below floor; mvfx_errorLimit past the bound.
static inline mvfxResult mvfxCountUpTo(int64_t count, int64_t floor, int64_t bound)
{
    if (count < floor)
    {
        return mvfx_errorInvalid;
    }
    return count <= bound ? mvfx_success : mvfx_errorLimit;
}

#endif
