// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Values over a particle's life: a value times a curve baked over
// normalized age.

#ifndef MAUL_VFX_SRC_LIFE_H
#define MAUL_VFX_SRC_LIFE_H

#include "curve.h"
#include "value.h"

#include "maul-vfx/modules.h"

// A life curve as compiled: its value and its table, or NULL for 1.
typedef struct mvfxCompiledLife
{
    float value;
    const mvfxCurveTable* curve;
} mvfxCompiledLife;

// Whether a life curve is valid.
bool mvfxIsLifeCurveValid(const mvfxLifeCurve* life);

// The tables a life curve needs: 1 with keys, else 0.
int32_t mvfxLifeCurves(const mvfxLifeCurve* life);

// Compiles a valid life curve, its table into space.
void mvfxCompileLife(const mvfxLifeCurve* life, mvfxTableSpace* space, mvfxCompiledLife* out);

// The largest its value is in size at any age: its table is read between
// samples, so never past the largest sample.
float mvfxLifeBound(const mvfxCompiledLife* life);

// Its value at a normalized age.
static inline float mvfxLifeAt(const mvfxCompiledLife* life, float age)
{
    return life->curve == nullptr ? life->value : life->value * mvfxCurveTableAt(life->curve, age);
}

#endif
