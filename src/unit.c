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
// Pellacini 2015). A chain is a sheet of one row; a sheet's constraints
// come in six families, each in sets by its first point's place across or
// its row: stretch across in two sets by the place's parity and down in
// two by the row's, shear along each diagonal in two by the row's, and
// bending (the distance across two edges, Provot 1995's flexion springs,
// cheaper than a dihedral angle) across in four by the place modulo 4 and
// down in four by the row modulo 4. Units lie whole and in order in the arrays, so a constraint's
// points are found from its unit's first.

#include "unit.h"

#include "attach.h"
#include "elementary.h"
#include "integrate.h"

#include <math.h>
#include <string.h>

// A substep's view of the points: places, velocities, the units' shape,
// which points are pinned and which units are let go.
typedef struct Points
{
    float* x[3];
    float* v[3];
    const uint32_t* flags;
    const mvfxInstance* instance;
    int32_t emitterIndex;
    const mvfxCompiledEmitter* emitter;
    bool pinned;
    int32_t perUnit;
    int32_t width;
    int32_t rows;
    int32_t units;
} Points;

// A point's inverse mass: 0 for a pinned point of a unit not let go, else
// 1.
static float WeightOf(const Points* p, int32_t i)
{
    int32_t point = i % p->perUnit;
    bool held = p->pinned && mvfxIsPinnedPoint(p->emitter, point) &&
                (p->flags == nullptr || (p->flags[i - point] & MVFX_ATTACH_LET_GO) == 0);
    return held ? 0.0f : 1.0f;
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

// What a pass does to each constraint.
typedef enum Pass
{
    PASS_PROJECT,
    PASS_DAMP,
} Pass;

// A family of constraints: from the point at (c + c0, r + r0) to that at
// (c + c1, r + r1) for each place c across and row r where both lie in the
// unit, in sets of the place's (byColumn) or the row's remainder by sets;
// its rest length and compliance.
typedef struct Family
{
    int8_t c0;
    int8_t r0;
    int8_t c1;
    int8_t r1;
    int8_t sets;
    bool byColumn;
    float rest;
    float compliance;
} Family;

// One set of a family over every unit.
static void SolveSet(const Points* p, const Family* f, int32_t set, Pass pass, float share)
{
    int32_t columns = p->width - (f->c0 > f->c1 ? f->c0 : f->c1);
    int32_t rows = p->rows - (f->r0 > f->r1 ? f->r0 : f->r1);
    int32_t cStep = f->byColumn ? f->sets : 1;
    int32_t rStep = f->byColumn ? 1 : f->sets;
    for (int32_t u = 0; u < p->units; ++u)
    {
        int32_t first = u * p->perUnit;
        for (int32_t r = f->byColumn ? 0 : set; r < rows; r += rStep)
        {
            for (int32_t c = f->byColumn ? set : 0; c < columns; c += cStep)
            {
                int32_t a = first + (r + f->r0) * p->width + c + f->c0;
                int32_t b = first + (r + f->r1) * p->width + c + f->c1;
                pass == PASS_PROJECT ? Project(p, a, b, f->rest, f->compliance)
                                     : Damp(p, a, b, share);
            }
        }
    }
}

// The six families, their rests in the particles' space and their
// compliances over h^2.
static void FamiliesOf(const mvfxCompiledEmitter* compiled, float scale, float inverseSquare,
                       Family out[6])
{
    const mvfxUnitDef* unit = &compiled->unit;
    const float* rests = compiled->unitRests;
    float stretch = unit->stretchCompliance * inverseSquare;
    float shear = unit->shearCompliance * inverseSquare;
    float bend = unit->bendCompliance * inverseSquare;
    out[0] = (Family){0, 0, 1, 0, 2, true, rests[0] * scale, stretch};
    out[1] = (Family){0, 0, 0, 1, 2, false, rests[1] * scale, stretch};
    out[2] = (Family){0, 0, 1, 1, 2, false, rests[2] * scale, shear};
    out[3] = (Family){1, 0, 0, 1, 2, false, rests[3] * scale, shear};
    out[4] = (Family){0, 0, 2, 0, 4, true, 2.0f * rests[0] * scale, bend};
    out[5] = (Family){0, 0, 0, 2, 4, false, 2.0f * rests[1] * scale, bend};
}

// Every constraint, family by family and set by set.
static void SolveAll(const Points* p, const Family families[6], Pass pass, float share)
{
    for (int32_t f = 0; f < 6; ++f)
    {
        for (int32_t set = 0; set < families[f].sets; ++set)
        {
            SolveSet(p, &families[f], set, pass, share);
        }
    }
}

// Holds each pinned point where its frame puts it at a substep's end,
// its velocity its move over the substep.
static void Pin(const Points* p, const mvfxPinFrame* from, const mvfxPinFrame* to, float h)
{
    int32_t count = p->units * p->perUnit;
    for (int32_t i = 0; p->pinned && i < count; ++i)
    {
        if (WeightOf(p, i) == 0.0f)
        {
            float before[3];
            float after[3];
            mvfxPinPlace(p->instance, p->emitterIndex, from, i / p->perUnit, i, before);
            mvfxPinPlace(p->instance, p->emitterIndex, to, i / p->perUnit, i, after);
            for (int32_t k = 0; k < 3; ++k)
            {
                p->x[k][i] = after[k];
                p->v[k][i] = (after[k] - before[k]) / h;
            }
        }
    }
}

static Points PointsOf(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    Points p = {.instance = instance,
                .emitterIndex = emitter,
                .flags = mvfxHasAttribute(&compiled->plan, mvfx_attributeAttachFlags)
                             ? mvfxAttributeArray(instance, emitter, mvfx_attributeAttachFlags)
                             : nullptr,
                .emitter = compiled,
                .pinned = mvfxHasAttribute(&compiled->plan, mvfx_attributeAnchorX),
                .perUnit = compiled->unitPoints,
                .width = compiled->unitWidth,
                .rows = compiled->unitPoints / compiled->unitWidth,
                .units = instance->emitters[emitter].count / compiled->unitPoints};
    for (int32_t k = 0; k < 3; ++k)
    {
        p.x[k] =
            mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
        p.v[k] =
            mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeVelocityX + k));
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
    // Rest lengths in the particles' space: a world-space emitter's scale
    // with its instance.
    float scale = compiled->space == mvfx_spaceWorld ? instance->to.scale : 1.0f;
    int64_t ticks = instance->clock - instance->stepStart;
    int32_t substeps = unit->substeps;
    mvfxBeginAttachments(instance, emitter);
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
        mvfxPinFrame before = mvfxPinFrameAt(instance, emitter, from);
        mvfxPinFrame after = mvfxPinFrameAt(instance, emitter, to);
        Pin(&p, &before, &after, h);
        Family families[6];
        FamiliesOf(compiled, scale, 1.0f / (h * h), families);
        SolveAll(&p, families, PASS_PROJECT, 0.0f);
        // The integrator's velocity (exact under gravity and drag) and the
        // constraints' correction over the substep; a pinned point's its
        // frame's.
        for (int32_t k = 0; k < 3; ++k)
        {
            const float* predicted = state->unitPrevious + (size_t)k * stride;
            for (int32_t i = 0; i < count; ++i)
            {
                float corrected = p.v[k][i] + (p.x[k][i] - predicted[i]) / h;
                p.v[k][i] = WeightOf(&p, i) > 0.0f ? corrected : p.v[k][i];
            }
        }
        float share = 1.0f - mvfxExp(-unit->damping * h);
        if (share > 0.0f)
        {
            SolveAll(&p, families, PASS_DAMP, share);
        }
    }
    mvfxEndAttachments(instance, emitter);
}
