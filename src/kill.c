// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "kill.h"

#include "direction.h"

#include <math.h>

static bool IsVectorWithin(const float v[3], float bound)
{
    return fabsf(v[0]) <= bound && fabsf(v[1]) <= bound && fabsf(v[2]) <= bound;
}

static bool IsPositive(float x)
{
    return x > 0.0f && x <= MVFX_MAX_EXTENT;
}

static bool IsRotation(const float q[4])
{
    double square = 0.0;
    for (int32_t k = 0; k < 4; ++k)
    {
        if (!(fabsf(q[k]) <= 1.001f))
        {
            return false;
        }
        square += (double)q[k] * (double)q[k];
    }
    return fabs(sqrt(square) - 1.0) <= 0.001;
}

bool mvfxIsKillRuleValid(const mvfxKillRule* rule)
{
    if (rule->space > mvfx_spaceLocal || !IsVectorWithin(rule->center, MVFX_MAX_EXTENT))
    {
        return false;
    }
    float normal[3] = {rule->normal[0], rule->normal[1], rule->normal[2]};
    switch (rule->kind)
    {
    case mvfx_killInsideSphere:
    case mvfx_killOutsideSphere:
        return IsPositive(rule->radius);
    case mvfx_killInsideBox:
    case mvfx_killOutsideBox:
        return IsPositive(rule->halfExtents[0]) && IsPositive(rule->halfExtents[1]) &&
               IsPositive(rule->halfExtents[2]) && IsRotation(rule->rotation);
    case mvfx_killPastPlane:
        return IsVectorWithin(rule->normal, MVFX_MAX_EXTENT) && mvfxNormalize(normal);
    case mvfx_killSlower:
        return rule->speed >= 0.0f && rule->speed <= MVFX_MAX_EXTENT && rule->minimumAge >= 0.0f &&
               rule->minimumAge <= MVFX_MAX_LIFETIME;
    case mvfx_killCollisions:
        return rule->collisions >= 1;
    default:
        return false;
    }
}

mvfxKillRule mvfxCompileKillRule(const mvfxKillRule* rule)
{
    mvfxKillRule out = *rule;
    if (out.kind == mvfx_killPastPlane)
    {
        (void)mvfxNormalize(out.normal);
    }
    if (out.kind == mvfx_killInsideBox || out.kind == mvfx_killOutsideBox)
    {
        double square = 0.0;
        for (int32_t k = 0; k < 4; ++k)
        {
            square += (double)out.rotation[k] * (double)out.rotation[k];
        }
        double inverse = 1.0 / sqrt(square);
        for (int32_t k = 0; k < 4; ++k)
        {
            out.rotation[k] = (float)((double)out.rotation[k] * inverse);
        }
    }
    return out;
}

mvfxResolvedKillRule mvfxResolveKillRule(const mvfxKillRule* rule, const mvfxSpaceMap* map)
{
    float sigma = map->sigma;
    mvfxResolvedKillRule out = {
        .kind = rule->kind,
        .radius = rule->radius * sigma,
        .halfExtents = {rule->halfExtents[0] * sigma, rule->halfExtents[1] * sigma,
                        rule->halfExtents[2] * sigma},
        .speed = rule->speed * sigma,
        .minimumAge = rule->minimumAge,
        .collisions = rule->collisions,
    };
    mvfxMapPoint(map, rule->center, out.center);
    mvfxMapVector(map, rule->normal, 1.0f, out.normal);
    mvfxMultiplyRotations(map->rotation, rule->rotation, out.rotation);
    return out;
}

static float Dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

bool mvfxSegmentMeetsSphere(const float start[3], const float end[3], const float center[3],
                            float radius)
{
    float d[3] = {end[0] - start[0], end[1] - start[1], end[2] - start[2]};
    float f[3] = {start[0] - center[0], start[1] - center[1], start[2] - center[2]};
    float length = Dot(d, d);
    float t = length > 0.0f ? -Dot(f, d) / length : 0.0f;
    t = fminf(fmaxf(t, 0.0f), 1.0f);
    float closest[3] = {f[0] + t * d[0], f[1] + t * d[1], f[2] + t * d[2]};
    return Dot(closest, closest) <= radius * radius;
}

// A place in a box's own frame.
static void IntoBox(const mvfxResolvedKillRule* box, const float place[3], float out[3])
{
    const float* q = box->rotation;
    const float inverse[4] = {-q[0], -q[1], -q[2], q[3]};
    float d[3] = {place[0] - box->center[0], place[1] - box->center[1], place[2] - box->center[2]};
    mvfxRotate(inverse, d, out);
}

static bool IsInsideBox(const float local[3], const float half[3])
{
    return fabsf(local[0]) <= half[0] && fabsf(local[1]) <= half[1] && fabsf(local[2]) <= half[2];
}

// Whether the segment meets the box: the slabs' overlap along it.
static bool SegmentMeetsBox(const mvfxResolvedKillRule* box, const float start[3],
                            const float end[3])
{
    float a[3];
    float b[3];
    IntoBox(box, start, a);
    IntoBox(box, end, b);
    float low = 0.0f;
    float high = 1.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        float d = b[k] - a[k];
        float h = box->halfExtents[k];
        if (d == 0.0f)
        {
            if (fabsf(a[k]) > h)
            {
                return false;
            }
            continue;
        }
        float t1 = (-h - a[k]) / d;
        float t2 = (h - a[k]) / d;
        low = fmaxf(low, fminf(t1, t2));
        high = fminf(high, fmaxf(t1, t2));
    }
    return low <= high;
}

// Whether one rule kills the particle.
static bool Kills(const mvfxResolvedKillRule* rule, const float start[3], const float end[3],
                  const float velocity[3], double ageSeconds, uint32_t collisions)
{
    float local[3];
    switch (rule->kind)
    {
    case mvfx_killInsideSphere:
        return mvfxSegmentMeetsSphere(start, end, rule->center, rule->radius);
    case mvfx_killOutsideSphere:
    {
        float d[3] = {end[0] - rule->center[0], end[1] - rule->center[1], end[2] - rule->center[2]};
        return Dot(d, d) > rule->radius * rule->radius;
    }
    case mvfx_killInsideBox:
        return SegmentMeetsBox(rule, start, end);
    case mvfx_killOutsideBox:
        IntoBox(rule, end, local);
        return !IsInsideBox(local, rule->halfExtents);
    case mvfx_killPastPlane:
    {
        float d[3] = {end[0] - rule->center[0], end[1] - rule->center[1], end[2] - rule->center[2]};
        return Dot(d, rule->normal) > 0.0f;
    }
    case mvfx_killCollisions:
        return collisions >= (uint32_t)rule->collisions;
    default:
        return ageSeconds >= (double)rule->minimumAge &&
               Dot(velocity, velocity) < rule->speed * rule->speed;
    }
}

bool mvfxIsKilled(const mvfxResolvedKillRule* rules, int32_t count, const float start[3],
                  const float end[3], const float velocity[3], double ageSeconds,
                  uint32_t collisions)
{
    for (int32_t r = 0; r < count; ++r)
    {
        if (Kills(&rules[r], start, end, velocity, ageSeconds, collisions))
        {
            return true;
        }
    }
    return false;
}
