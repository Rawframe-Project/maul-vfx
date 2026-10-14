// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// XPBD (Macklin, Muller and Chentanez, "XPBD: Position-Based Simulation
// of Compliant Constrained Dynamics", MIG 2016) in small steps (Macklin et
// al., "Small Steps in Physics Simulation", SCA 2019): the exact
// integrator predicts each substep's places and velocities, the
// constraints correct the places, and the correction over the substep is
// added to the velocities (with the integrator's places, which move by
// a h^2 / 2, velocities taken wholly from places would lose half of each
// acceleration). One projection of
// each constraint a substep, its multiplier starting at 0, so a
// constraint's step is -C / (w1 + w2 + compliance / h^2). Constraints are
// solved set by set, no two in a set sharing a point (Fratarcangeli and
// Pellacini 2015): a chain's neighbors in two sets by the first point's
// parity, its bending pairs in four by the first point modulo 4. Units
// lie whole and in order in the arrays, so a constraint's points are found
// from its unit's first.

#include "unit.h"

#include "elementary.h"
#include "integrate.h"

#include <math.h>
#include <string.h>

// A substep's view of the points: places, velocities and their weights'
// source.
typedef struct Points
{
    float* x[3];
    float* v[3];
    const float* anchor[3];
    bool pinned;
    int32_t perUnit;
    int32_t units;
} Points;

// A point's inverse mass: 0 for a pinned first point, else 1.
static float WeightOf(const Points* p, int32_t i)
{
    return p->pinned && i % p->perUnit == 0 ? 0.0f : 1.0f;
}

// Projects the distance between points a and b toward rest.
static void Project(const Points* p, int32_t a, int32_t b, float rest, float compliance)
{
    float d[3];
    float square = 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        d[k] = p->x[k][a] - p->x[k][b];
        square += d[k] * d[k];
    }
    float length = sqrtf(square);
    float wa = WeightOf(p, a);
    float wb = WeightOf(p, b);
    float sum = wa + wb + compliance;
    if (!(length > 0.0f) || !(sum > 0.0f))
    {
        return;
    }
    float step = -(length - rest) / sum;
    for (int32_t k = 0; k < 3; ++k)
    {
        float n = d[k] / length;
        p->x[k][a] += wa * step * n;
        p->x[k][b] -= wb * step * n;
    }
}

// Damps the relative velocity of points a and b along the line between
// them by a share.
static void Damp(const Points* p, int32_t a, int32_t b, float share)
{
    float d[3];
    float square = 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        d[k] = p->x[k][a] - p->x[k][b];
        square += d[k] * d[k];
    }
    float length = sqrtf(square);
    float wa = WeightOf(p, a);
    float wb = WeightOf(p, b);
    if (!(length > 0.0f) || !(wa + wb > 0.0f))
    {
        return;
    }
    float along = 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        along += (p->v[k][b] - p->v[k][a]) * d[k] / length;
    }
    float change = share * along / (wa + wb);
    for (int32_t k = 0; k < 3; ++k)
    {
        float n = d[k] / length;
        p->v[k][a] += wa * change * n;
        p->v[k][b] -= wb * change * n;
    }
}

// The constraints of a chain, set by set: each calls one of the above.
typedef enum Pass
{
    PASS_PROJECT,
    PASS_DAMP,
} Pass;

static void ChainSets(const Points* p, Pass pass, float rest, float stretch, float bend,
                      float share)
{
    int32_t n = p->perUnit;
    // Neighbors: first points of one parity, then the other.
    for (int32_t set = 0; set < 2; ++set)
    {
        for (int32_t u = 0; u < p->units; ++u)
        {
            for (int32_t j = set; j + 1 < n; j += 2)
            {
                int32_t a = u * n + j;
                pass == PASS_PROJECT ? Project(p, a, a + 1, rest, stretch)
                                     : Damp(p, a, a + 1, share);
            }
        }
    }
    // Bending, points two apart: first points by their remainder by 4.
    for (int32_t set = 0; set < 4; ++set)
    {
        for (int32_t u = 0; u < p->units; ++u)
        {
            for (int32_t j = set; j + 2 < n; j += 4)
            {
                int32_t a = u * n + j;
                pass == PASS_PROJECT ? Project(p, a, a + 2, 2.0f * rest, bend)
                                     : Damp(p, a, a + 2, share);
            }
        }
    }
}

// Holds each pinned first point at its anchor, at rest.
static void Pin(const Points* p)
{
    for (int32_t u = 0; p->pinned && u < p->units; ++u)
    {
        int32_t i = u * p->perUnit;
        for (int32_t k = 0; k < 3; ++k)
        {
            p->x[k][i] = p->anchor[k][i];
        }
    }
}

static Points PointsOf(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    Points p = {.pinned = compiled->unit.pinFirst,
                .perUnit = compiled->unitPoints,
                .units = instance->emitters[emitter].count / compiled->unitPoints};
    for (int32_t k = 0; k < 3; ++k)
    {
        p.x[k] =
            mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
        p.v[k] =
            mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeVelocityX + k));
        p.anchor[k] = p.pinned ? mvfxAttributeArray(instance, emitter,
                                                    (mvfxAttribute)(mvfx_attributeAnchorX + k))
                               : nullptr;
    }
    return p;
}

void mvfxStepUnits(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    const mvfxUnitDef* unit = &compiled->unit;
    mvfxEmitterState* state = &instance->emitters[emitter];
    Points p = PointsOf(instance, emitter);
    int32_t count = state->count;
    size_t stride = (size_t)state->capacity;
    // The rest length in the particles' space: a world-space emitter's
    // scales with its instance.
    float rest = unit->spacing * (compiled->space == mvfx_spaceWorld ? instance->to.scale : 1.0f);
    int64_t ticks = instance->clock - instance->stepStart;
    int32_t substeps = unit->substeps;
    for (int32_t s = 0; s < substeps; ++s)
    {
        int64_t from = instance->stepStart + ticks * s / substeps;
        int64_t to = instance->stepStart + ticks * (s + 1) / substeps;
        if (to == from)
        {
            continue;
        }
        double seconds = (double)(to - from) / (double)MVFX_TICKS_PER_SECOND;
        float h = (float)seconds;
        mvfxIntegrateRange(instance, emitter, 0, count, from, seconds);
        // The places the integrator predicts, before the constraints.
        for (int32_t k = 0; k < 3; ++k)
        {
            memcpy(state->unitPrevious + (size_t)k * stride, p.x[k], (size_t)count * sizeof(float));
        }
        Pin(&p);
        float inverseSquare = 1.0f / (h * h);
        ChainSets(&p, PASS_PROJECT, rest, unit->stretchCompliance * inverseSquare,
                  unit->bendCompliance * inverseSquare, 0.0f);
        // The integrator's velocity (exact under gravity and drag) and the
        // constraints' correction over the substep; a pinned point at rest.
        for (int32_t k = 0; k < 3; ++k)
        {
            const float* predicted = state->unitPrevious + (size_t)k * stride;
            for (int32_t i = 0; i < count; ++i)
            {
                float corrected = p.v[k][i] + (p.x[k][i] - predicted[i]) / h;
                p.v[k][i] = WeightOf(&p, i) > 0.0f ? corrected : 0.0f;
            }
        }
        float share = 1.0f - mvfxExp(-unit->damping * h);
        if (share > 0.0f)
        {
            ChainSets(&p, PASS_DAMP, rest, 0.0f, 0.0f, share);
        }
    }
}
