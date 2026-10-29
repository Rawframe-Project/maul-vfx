// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A flat plate's drag, split into its normal (pressure) and tangential
// (skin) parts, as Houdini's Vellum cloth sets "normal drag" and
// "tangent drag": an inclined plate's normal force has a part across the
// flow, so lift comes without a lift law of its own, and nothing is
// singular when the flow lies along the plate. Each triangle's force is
// shared among its three points of unit mass, applied as a velocity
// change at a substep's start (before the integrator predicts the
// places), held to a sixth of the triangle's relative speed so the six
// triangles that may meet at a point never reverse it.

#include "aero.h"

#include "direction.h"
#include "force.h"

#include <math.h>
#include <string.h>

// The forces that move the air (winds, velocity fields) and the step's
// time, for the air's velocity at a place.
typedef struct Air
{
    mvfxResolvedForce forces[MVFX_MAX_FORCES];
    int32_t count;
    double time;
} Air;

// The air's velocity at x: its forces' targets' mean weighted by their
// pulls, 0 where none reaches.
static void AirAt(const Air* air, const float x[3], float out[3])
{
    float pull[3] = {0.0f, 0.0f, 0.0f};
    float drag = 0.0f;
    mvfxForcesAt(air->forces, air->count, x, air->time, pull, &drag);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = drag > 0.0f ? pull[k] / drag : 0.0f;
    }
}

// A substep's view of a sheet emitter: places, velocities, the change
// each point takes, the coefficients and the air.
typedef struct Sheets
{
    const float* x[3];
    const float* v[3];
    float* change[3];
    float normalDrag;
    float tangentDrag;
    float h;
    const Air* air;
} Sheets;

// The share of a relative speed a triangle of area A takes over a
// substep at a coefficient: k A |speed| h over its three points, at most
// a sixth. A zero coefficient or speed takes none, even where the product
// of the others overflows (0 times infinity is a NaN, which would read as
// past a sixth).
static float ShareOf(const Sheets* s, float k, float area, float speed)
{
    if (!(k > 0.0f) || !(speed > 0.0f))
    {
        return 0.0f;
    }
    float share = k * area * speed * s->h / 3.0f;
    return share < 1.0f / 6.0f ? share : 1.0f / 6.0f;
}

// Adds the triangle of points a, b, c's drag to their changes.
static void Triangle(const Sheets* s, int32_t a, int32_t b, int32_t c)
{
    float e1[3];
    float e2[3];
    float middle[3];
    float v[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        e1[k] = s->x[k][b] - s->x[k][a];
        e2[k] = s->x[k][c] - s->x[k][a];
        middle[k] = (s->x[k][a] + s->x[k][b] + s->x[k][c]) / 3.0f;
        v[k] = (s->v[k][a] + s->v[k][b] + s->v[k][c]) / 3.0f;
    }
    // Edges with a component past 2^63 are crossed scaled by 2^-64 each,
    // so no product overflows, and the area scaled back (infinite past
    // binary32's largest, which holds a share at a sixth).
    float most = 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        most = fmaxf(most, fmaxf(fabsf(e1[k]), fabsf(e2[k])));
    }
    bool scaled = most >= 0x1p63f;
    for (int32_t k = 0; scaled && k < 3; ++k)
    {
        e1[k] *= 0x1p-64f;
        e2[k] *= 0x1p-64f;
    }
    float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                  e1[0] * e2[1] - e1[1] * e2[0]};
    float twice = mvfxLengthOf(n[0], n[1], n[2]);
    if (!(twice > 0.0f))
    {
        return;
    }
    float wind[3];
    AirAt(s->air, middle, wind);
    float along = 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        n[k] /= twice;
        v[k] -= wind[k];
        along += v[k] * n[k];
    }
    float tangent[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        tangent[k] = v[k] - along * n[k];
    }
    float area = scaled ? 0.5f * twice * 0x1p64f * 0x1p64f : 0.5f * twice;
    float normalShare = ShareOf(s, s->normalDrag, area, fabsf(along));
    float tangentShare =
        ShareOf(s, s->tangentDrag, area, mvfxLengthOf(tangent[0], tangent[1], tangent[2]));
    const int32_t points[3] = {a, b, c};
    for (int32_t k = 0; k < 3; ++k)
    {
        float d = -normalShare * along * n[k] - tangentShare * tangent[k];
        for (int32_t j = 0; j < 3; ++j)
        {
            s->change[k][points[j]] += d;
        }
    }
}

// The air of an emitter's step: its forces that pull toward a velocity.
static void AirOf(const mvfxInstance* instance, int32_t emitter, int64_t tick, Air* out)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    out->count = 0;
    out->time = (double)tick / (double)MVFX_TICKS_PER_SECOND;
    for (int32_t f = 0; f < compiled->forceCount; ++f)
    {
        const mvfxResolvedForce* force = &instance->emitters[emitter].forces[f];
        if (mvfxForceDrag(force) > 0.0f)
        {
            out->forces[out->count++] = *force;
        }
    }
}

// Adds every whole triangle's drag of a sheet whose first point is first.
static void BlowSheet(const Sheets* s, const mvfxCompiledEmitter* compiled, const uint32_t* torn,
                      int32_t first)
{
    int32_t width = compiled->unitWidth;
    int32_t rows = compiled->unitPoints / width;
    for (int32_t r = 0; r + 1 < rows; ++r)
    {
        for (int32_t c = 0; c + 1 < width; ++c)
        {
            int32_t q = first + r * width + c;
            if (mvfxIsQuadWhole(torn, q, width))
            {
                Triangle(s, q, q + 1, q + width);
                Triangle(s, q + width, q + 1, q + width + 1);
            }
        }
    }
}

void mvfxBlowSheets(mvfxInstance* instance, int32_t emitter, int64_t tick, float h, float* room)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    const mvfxUnitDef* unit = &compiled->unit;
    if (unit->kind != mvfx_unitSheet || (unit->normalDrag == 0.0f && unit->tangentDrag == 0.0f))
    {
        return;
    }
    mvfxEmitterState* state = &instance->emitters[emitter];
    static_assert(sizeof(Air) < 4096, "the air fits the stack");
    Air air;
    AirOf(instance, emitter, tick, &air);
    Sheets s = {
        .normalDrag = unit->normalDrag, .tangentDrag = unit->tangentDrag, .h = h, .air = &air};
    float* v[3];
    size_t stride = (size_t)state->capacity;
    for (int32_t k = 0; k < 3; ++k)
    {
        s.x[k] =
            mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
        v[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeVelocityX + k));
        s.v[k] = v[k];
        s.change[k] = room + (size_t)k * stride;
        memset(s.change[k], 0, (size_t)state->count * sizeof(float));
    }
    const uint32_t* torn = mvfxHasAttribute(&compiled->plan, mvfx_attributeTorn)
                               ? mvfxAttributeArray(instance, emitter, mvfx_attributeTorn)
                               : nullptr;
    for (int32_t u = 0; u < state->count / compiled->unitPoints; ++u)
    {
        BlowSheet(&s, compiled, torn, u * compiled->unitPoints);
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        for (int32_t i = 0; i < state->count; ++i)
        {
            v[k][i] += s.change[k][i];
        }
    }
}
