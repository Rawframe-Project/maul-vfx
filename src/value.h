// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Value sources checked, compiled (their curves and gradients baked into
// the effect's block) and drawn at a particle's birth.

#ifndef MAUL_VFX_SRC_VALUE_H
#define MAUL_VFX_SRC_VALUE_H

#include "curve.h"
#include "gradient.h"
#include "random.h"

#include "maul-vfx/value.h"

// A scalar source as a birth draws it.
typedef struct mvfxCompiledScalar
{
    mvfxSourceKind kind;
    float min;
    float max;
    const mvfxCurveTable* curve;
    int32_t parameter;
} mvfxCompiledScalar;

// A vector source as a birth draws it.
typedef struct mvfxCompiledVector
{
    mvfxSourceKind kind;
    bool uniform;
    int32_t components;
    float min[4];
    float max[4];
    const mvfxCurveTable* curve;
    const mvfxGradientTable* gradient;
    int32_t parameter;
} mvfxCompiledVector;

// Where a source's curves and gradients go when it is compiled: room the
// caller laid out by counting them.
typedef struct mvfxTableSpace
{
    mvfxCurveTable* curves;
    mvfxGradientTable* gradients;
    int32_t nextCurve;
    int32_t nextGradient;
} mvfxTableSpace;

// Whether a scalar source is valid: a known kind (none only when
// optional), finite bounds in order, a valid curve (over [0, 1] when the
// emitter has a duration), a parameter below the count.
bool mvfxIsScalarSourceValid(const mvfxScalarSource* source, bool optional, bool hasDuration,
                             int32_t parameterCount);

// Whether a vector source of components (1 to 4) is valid likewise, the
// gradient kinds only for four components, over age only with a
// duration.
bool mvfxIsVectorSourceValid(const mvfxVectorSource* source, int32_t components, bool hasDuration,
                             int32_t parameterCount);

// How many curve and gradient tables a source needs.
int32_t mvfxScalarCurves(const mvfxScalarSource* source);
int32_t mvfxVectorCurves(const mvfxVectorSource* source);
int32_t mvfxVectorGradients(const mvfxVectorSource* source);

// Compiles valid sources, baking into space.
void mvfxCompileScalar(const mvfxScalarSource* source, bool hasDuration, mvfxTableSpace* space,
                       mvfxCompiledScalar* out);
void mvfxCompileVector(const mvfxVectorSource* source, int32_t components, bool hasDuration,
                       mvfxTableSpace* space, mvfxCompiledVector* out);

// Draws a scalar at an age, with an instance's parameters and four words.
float mvfxDrawScalar(const mvfxCompiledScalar* source, float age, const float (*parameters)[4],
                     mvfxWords4 words);

// Draws a vector's components at an age likewise.
void mvfxDrawVector(const mvfxCompiledVector* source, float age, const float (*parameters)[4],
                    mvfxWords4 words, float out[4]);

#endif
