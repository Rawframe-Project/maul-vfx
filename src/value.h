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
// emitter has a duration), a parameter below the count (else
// mvfx_errorInvalid); bounds at most MVFX_MAX_VALUE in size, and its
// base's largest size (1 for a range, a curve's largest baked sample)
// times its bounds' larger size within cap, a named bound (at most
// MVFX_MAX_DRAW), else mvfx_errorLimit. A parameter's share is checked
// against its value (mvfxCheckParameterReach). Names the field it refuses
// below at: past a cap, the end of the larger size.
mvfxResult mvfxCheckScalarSource(const mvfxFieldPath* at, const mvfxScalarSource* source,
                                 bool optional, bool hasDuration, int32_t parameterCount,
                                 float cap);

// The key starting the first segment of a valid curve source's curve
// whose baked samples fall below 0, -1 for none: where a cubic segment
// between keys of no negative value dips.
int32_t mvfxCurveSourceDip(const mvfxScalarSource* source, bool hasDuration);

// Whether a vector source of components (1 to 4) is valid likewise, the
// gradient kinds only for four components, over age only with a
// duration; its base's largest size (a gradient's largest key) times each
// component's ends' larger size within that component's cap (cap holds
// one a component).
mvfxResult mvfxCheckVectorSource(const mvfxFieldPath* at, const mvfxVectorSource* source,
                                 int32_t components, bool hasDuration, int32_t parameterCount,
                                 const float* cap);

// Raises reach[p][c] to the larger size of the ends of a source that reads
// component c of parameter p over the source's cap for it, for each such
// component.
void mvfxScalarParameterReach(const mvfxScalarSource* source, float cap,
                              double reach[MVFX_MAX_PARAMETERS][4]);
void mvfxVectorParameterReach(const mvfxVectorSource* source, int32_t components, const float* cap,
                              double reach[MVFX_MAX_PARAMETERS][4]);

// Whether a parameter's value times the reach of the sources reading each
// component stays within 1, each source's draw then within its cap:
// mvfx_errorInvalid for a NaN or an infinity, mvfx_errorLimit past a cap.
mvfxResult mvfxCheckParameterReach(const float value[4], const double reach[4]);

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
