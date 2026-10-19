// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Steering after Reynolds ("Flocks, Herds, and Schools", 1987; "Steering
// Behaviors For Autonomous Characters", 1999): each behavior asks for a
// velocity, and its steering is its weight times that less the particle's
// velocity; flocking's rules are their sums times their weights. Every
// particle reads the step's start (its neighbors through the grid's copy)
// and writes only its own velocity.

#include "steer.h"

#include "neighbor.h"
#include "space.h"

#include <math.h>

static float Length(const float v[3])
{
    return sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

// A point in a space with the instance at a place, in the particles'
// space.
static void MapAt(const mvfxInstance* instance, int32_t emitter, const mvfxTransform* at,
                  uint8_t space, const float p[3], float out[3])
{
    mvfxSpaceMap map = mvfxSpaceMapAt(instance, emitter, space, at);
    mvfxMapPoint(&map, p, out);
}

static void ResolvePoint(const mvfxInstance* instance, int32_t emitter, const mvfxTransform* at,
                         const mvfxSteerPoint* point, float out[3])
{
    float p[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        p[k] = point->fromParameter ? instance->parameters[point->parameter][k] : point->point[k];
    }
    MapAt(instance, emitter, at, point->space, p, out);
}

void mvfxResolveSteering(const mvfxInstance* instance, int32_t emitter, const mvfxTransform* at,
                         mvfxPlannedSteering* targetsOut)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    const mvfxSteeringDef* s = &compiled->steering;
    mvfxPlannedSteering* t = targetsOut;
    *t = (mvfxPlannedSteering){0};
    if (!s->enabled)
    {
        return;
    }
    t->viewCosine = mvfxViewCosine(s);
    ResolvePoint(instance, emitter, at, &s->seek, t->seek);
    ResolvePoint(instance, emitter, at, &s->flee, t->flee);
    t->pointCount = s->path.weight > 0.0f ? s->path.pointCount : 0;
    t->segmentCount = t->pointCount == 0 ? 0 : s->path.closed ? t->pointCount : t->pointCount - 1;
    for (int32_t i = 0; i < t->pointCount; ++i)
    {
        MapAt(instance, emitter, at, s->path.space, compiled->pathPoints[i], t->path[i]);
    }
    for (int32_t g = 0; s->path.closed && g < t->segmentCount; ++g)
    {
        const float* a = t->path[g];
        const float* b = t->path[(g + 1) % t->pointCount];
        const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        t->perimeter += Length(d);
    }
}

// Adds weight times the velocity of the given speed along d less v; a d
// of length 0 asks to stand still.
static void Toward(const float d[3], float speed, const float v[3], float weight, float acc[3])
{
    float length = Length(d);
    float scale = length > 0.0f ? speed / length : 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        acc[k] += weight * (d[k] * scale - v[k]);
    }
}

static void Seek(const mvfxSteeringDef* s, const mvfxPlannedSteering* t, const float p[3],
                 const float v[3], float acc[3])
{
    const float d[3] = {t->seek[0] - p[0], t->seek[1] - p[1], t->seek[2] - p[2]};
    float distance = Length(d);
    float speed = s->cruiseSpeed;
    if (s->seek.radius > 0.0f && distance < s->seek.radius)
    {
        // Arrival: slower as it nears.
        speed *= distance / s->seek.radius;
    }
    Toward(d, speed, v, s->seek.weight, acc);
}

static void Flee(const mvfxSteeringDef* s, const mvfxPlannedSteering* t, const float p[3],
                 const float v[3], float acc[3])
{
    const float d[3] = {p[0] - t->flee[0], p[1] - t->flee[1], p[2] - t->flee[2]};
    float distance = Length(d);
    if (distance > 0.0f && (s->flee.radius == 0.0f || distance < s->flee.radius))
    {
        Toward(d, s->cruiseSpeed, v, s->flee.weight, acc);
    }
}

// The path's point nearest x: its segment and place, the first nearest.
static int32_t Nearest(const mvfxPlannedSteering* t, const float x[3], float out[3])
{
    float best = INFINITY;
    int32_t at = 0;
    for (int32_t g = 0; g < t->segmentCount; ++g)
    {
        const float* a = t->path[g];
        const float* b = t->path[(g + 1) % t->pointCount];
        float ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        float square = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
        float along = (x[0] - a[0]) * ab[0] + (x[1] - a[1]) * ab[1] + (x[2] - a[2]) * ab[2];
        float u = square > 0.0f ? along / square : 0.0f;
        u = u < 0.0f ? 0.0f : u > 1.0f ? 1.0f : u;
        float q[3] = {a[0] + ab[0] * u, a[1] + ab[1] * u, a[2] + ab[2] * u};
        float d[3] = {x[0] - q[0], x[1] - q[1], x[2] - q[2]};
        float distance = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
        if (distance < best)
        {
            best = distance;
            at = g;
            out[0] = q[0];
            out[1] = q[1];
            out[2] = q[2];
        }
    }
    return at;
}

// Moves q on along the path from segment g by a distance: around a closed
// path, to the last point of an open one.
static void Advance(const mvfxPlannedSteering* t, bool closed, int32_t g, float distance,
                    float q[3])
{
    if (closed && t->perimeter > 0.0f)
    {
        distance -= floorf(distance / t->perimeter) * t->perimeter;
    }
    for (int32_t n = 0; n <= t->segmentCount; ++n)
    {
        const float* b = t->path[(g + 1) % t->pointCount];
        float d[3] = {b[0] - q[0], b[1] - q[1], b[2] - q[2]};
        float left = Length(d);
        if (distance <= left)
        {
            float u = left > 0.0f ? distance / left : 0.0f;
            for (int32_t k = 0; k < 3; ++k)
            {
                q[k] += d[k] * u;
            }
            return;
        }
        distance -= left;
        q[0] = b[0];
        q[1] = b[1];
        q[2] = b[2];
        g += 1;
        if (g == t->segmentCount)
        {
            if (!closed)
            {
                return;
            }
            g = 0;
        }
    }
}

static void Follow(const mvfxSteeringDef* s, const mvfxPlannedSteering* t, const float p[3],
                   const float v[3], float acc[3])
{
    const mvfxSteerPath* path = &s->path;
    float lookahead = path->lookahead;
    float predicted[3] = {p[0] + v[0] * lookahead, p[1] + v[1] * lookahead,
                          p[2] + v[2] * lookahead};
    float q[3] = {0.0f, 0.0f, 0.0f};
    int32_t g = Nearest(t, predicted, q);
    float off[3] = {predicted[0] - q[0], predicted[1] - q[1], predicted[2] - q[2]};
    if (!(Length(off) > path->radius))
    {
        return;
    }
    Advance(t, path->closed, g, Length(v) * lookahead, q);
    const float d[3] = {q[0] - p[0], q[1] - p[1], q[2] - p[2]};
    Toward(d, s->cruiseSpeed, v, path->weight, acc);
}

static void Flock(const mvfxSteeringDef* s, const mvfxNeighborSums* sums, const float p[3],
                  const float v[3], float acc[3])
{
    float aligned = sums->aligned > 0 ? 1.0f / (float)sums->aligned : 0.0f;
    float cohered = sums->cohered > 0 ? 1.0f / (float)sums->cohered : 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        acc[k] += s->separation * sums->apart[k];
        acc[k] +=
            sums->aligned > 0 ? s->alignment * (sums->alignedVelocity[k] * aligned - v[k]) : 0.0f;
        acc[k] += sums->cohered > 0 ? s->cohesion * (sums->coheredPlace[k] * cohered - p[k]) : 0.0f;
    }
}

// Holds an acceleration to the most force, 0 for no limit.
static void Limit(float most, float acc[3])
{
    float length = Length(acc);
    if (most > 0.0f && length > most)
    {
        float scale = most / length;
        for (int32_t k = 0; k < 3; ++k)
        {
            acc[k] *= scale;
        }
    }
}

void mvfxSteerEmitter(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    const mvfxSteeringDef* s = &compiled->steering;
    if (!s->enabled && !(compiled->pairRadius > 0.0f))
    {
        return;
    }
    mvfxPlannedSteering targets;
    mvfxResolveSteering(instance, emitter, &instance->from, &targets);
    bool neighbors = compiled->neighbors.enabled;
    mvfxNeighborQuery query = {0};
    if (neighbors)
    {
        query = mvfxOpenNeighborQuery(instance, emitter);
    }
    float seconds =
        (float)((double)(instance->clock - instance->stepStart) / (double)MVFX_TICKS_PER_SECOND);
    float* p[3];
    float* v[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        p[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
        v[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeVelocityX + k));
    }
    for (int32_t i = 0; i < instance->emitters[emitter].count; ++i)
    {
        float place[3] = {p[0][i], p[1][i], p[2][i]};
        float velocity[3] = {v[0][i], v[1][i], v[2][i]};
        float acc[3] = {0.0f, 0.0f, 0.0f};
        float pull[3] = {0.0f, 0.0f, 0.0f};
        if (neighbors)
        {
            mvfxNeighborSums sums = mvfxSearchNeighbors(&query, i, place, velocity);
            mvfxStoreNeighbors(instance, emitter, i, &sums, place, velocity);
            Flock(s, &sums, place, velocity, acc);
            pull[0] = sums.pull[0];
            pull[1] = sums.pull[1];
            pull[2] = sums.pull[2];
        }
        s->seek.weight > 0.0f ? Seek(s, &targets, place, velocity, acc) : (void)0;
        s->flee.weight > 0.0f ? Flee(s, &targets, place, velocity, acc) : (void)0;
        targets.segmentCount > 0 ? Follow(s, &targets, place, velocity, acc) : (void)0;
        Limit(s->maxForce, acc);
        for (int32_t k = 0; k < 3; ++k)
        {
            // The pair force is a force, not steering: not held.
            v[k][i] += (acc[k] + pull[k]) * seconds;
        }
    }
}
