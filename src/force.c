// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "force.h"

#include "direction.h"
#include "elementary.h"

#include <math.h>

static bool IsWithin(float x, float low, float high)
{
    return x >= low && x <= high;
}

static bool IsVectorWithin(const float v[3], float bound)
{
    return fabsf(v[0]) <= bound && fabsf(v[1]) <= bound && fabsf(v[2]) <= bound;
}

static bool IsAxis(const float v[3])
{
    float copy[3] = {v[0], v[1], v[2]};
    return IsVectorWithin(v, MVFX_MAX_EXTENT) && mvfxNormalize(copy);
}

static bool IsFalloffValid(const mvfxFalloff* falloff)
{
    if (falloff->kind == mvfx_falloffNone)
    {
        return true;
    }
    return falloff->kind <= mvfx_falloffCone && IsVectorWithin(falloff->center, MVFX_MAX_EXTENT) &&
           falloff->radius > 0.0f && falloff->radius <= MVFX_MAX_EXTENT &&
           IsWithin(falloff->exponent, 0.0f, MVFX_MAX_FALLOFF_EXPONENT) &&
           (falloff->kind == mvfx_falloffSphere || IsAxis(falloff->axis)) &&
           (falloff->kind != mvfx_falloffCone ||
            (falloff->angle > 0.0f && falloff->angle <= MVFX_MAX_CONE_ANGLE));
}

bool mvfxIsForceValid(const mvfxForce* force, int32_t parameterCount)
{
    if (force->kind > mvfx_forceVortex || force->space > mvfx_spaceLocal ||
        !IsVectorWithin(force->vector, MVFX_MAX_EXTENT) ||
        !IsVectorWithin(force->point, MVFX_MAX_EXTENT) ||
        !IsWithin(force->strength, -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT) ||
        (force->strengthFromParameter &&
         (force->parameter < 0 || force->parameter >= parameterCount)) ||
        !IsFalloffValid(&force->falloff))
    {
        return false;
    }
    switch (force->kind)
    {
    case mvfx_forceWind:
        return IsWithin(force->strength, 0.0f, MVFX_MAX_DRAG);
    case mvfx_forceAttractor:
        return IsWithin(force->softening, MVFX_MIN_SOFTENING, MVFX_MAX_EXTENT);
    case mvfx_forceVortex:
        return IsAxis(force->vector) && IsWithin(force->pull, -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT);
    default:
        return true;
    }
}

mvfxForce mvfxCompileForce(const mvfxForce* force)
{
    mvfxForce out = *force;
    if (out.kind == mvfx_forceVortex)
    {
        (void)mvfxNormalize(out.vector);
    }
    if (out.falloff.kind == mvfx_falloffTube || out.falloff.kind == mvfx_falloffCone)
    {
        (void)mvfxNormalize(out.falloff.axis);
    }
    return out;
}

static void MapPoint(const mvfxSpaceMap* map, const float p[3], float out[3])
{
    float local[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        local[k] = (float)((double)p[k] - map->before[k]) * map->sigma;
    }
    float turned[3];
    mvfxRotate(map->rotation, local, turned);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = (float)((double)turned[k] + map->after[k]);
    }
}

static void MapVector(const mvfxSpaceMap* map, const float v[3], float sigma, float out[3])
{
    float scaled[3] = {v[0] * sigma, v[1] * sigma, v[2] * sigma};
    mvfxRotate(map->rotation, scaled, out);
}

// x held to [-bound, bound], or to [0, bound] when low is 0.
static float Hold(float x, float low, float bound)
{
    return x < low ? low : (x > bound ? bound : x);
}

mvfxResolvedForce mvfxResolveForce(const mvfxForce* force, const mvfxSpaceMap* map,
                                   const float (*parameters)[4])
{
    float strength = force->strength;
    if (force->strengthFromParameter)
    {
        bool wind = force->kind == mvfx_forceWind;
        strength = Hold(strength * parameters[force->parameter][0], wind ? 0.0f : -MVFX_MAX_EXTENT,
                        wind ? MVFX_MAX_DRAG : MVFX_MAX_EXTENT);
    }
    float sigma = map->sigma;
    mvfxResolvedForce out = {
        .kind = force->kind,
        .falloff = force->falloff.kind,
        .strength = strength,
        .softening = force->softening * sigma,
        .pull = force->pull * sigma,
        .radius = force->falloff.radius * sigma,
        .exponent = force->falloff.exponent,
    };
    switch (force->kind)
    {
    case mvfx_forceAttractor:
        out.strength *= sigma;
        break;
    case mvfx_forceVortex:
        // The axis turns; strength and pull are accelerations.
        out.strength *= sigma;
        break;
    default:
        break;
    }
    MapVector(map, force->vector, force->kind == mvfx_forceVortex ? 1.0f : sigma, out.vector);
    MapPoint(map, force->point, out.point);
    MapPoint(map, force->falloff.center, out.center);
    MapVector(map, force->falloff.axis, 1.0f, out.axis);
    if (force->falloff.kind == mvfx_falloffCone)
    {
        float sine = 0.0f;
        mvfxSinCos(force->falloff.angle, &sine, &out.cosine);
    }
    return out;
}

static float Dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// The weight (1 - share)^exponent, 0 at or past the edge.
static float Weight(float share, float exponent)
{
    if (!(share < 1.0f))
    {
        return 0.0f;
    }
    return exponent == 0.0f ? 1.0f : mvfxPow(1.0f - share, exponent);
}

// The falloff's weight at x.
static float FalloffAt(const mvfxResolvedForce* force, const float x[3])
{
    if (force->falloff == mvfx_falloffNone)
    {
        return 1.0f;
    }
    float d[3] = {x[0] - force->center[0], x[1] - force->center[1], x[2] - force->center[2]};
    float distance = sqrtf(Dot(d, d));
    float along = Dot(d, force->axis);
    if (force->falloff == mvfx_falloffTube)
    {
        float across[3] = {d[0] - along * force->axis[0], d[1] - along * force->axis[1],
                           d[2] - along * force->axis[2]};
        return Weight(sqrtf(Dot(across, across)) / force->radius, force->exponent);
    }
    float weight = Weight(distance / force->radius, force->exponent);
    if (force->falloff == mvfx_falloffCone && distance > 0.0f)
    {
        // The angle's share by its cosine: 0 on the axis, 1 at the edge.
        float cosine = along / distance;
        float share = (1.0f - cosine) / (1.0f - force->cosine);
        weight *= Weight(share, force->exponent);
    }
    return weight;
}

// The attractor's acceleration at x.
static void Attract(const mvfxResolvedForce* force, const float x[3], float weight, float a[3])
{
    float d[3] = {force->point[0] - x[0], force->point[1] - x[1], force->point[2] - x[2]};
    float distance = sqrtf(Dot(d, d));
    if (!(distance > 0.0f))
    {
        return;
    }
    float ratio = force->softening / distance;
    float magnitude = distance < force->softening ? force->strength / force->softening * distance
                                                  : force->strength * ratio * ratio;
    float scale = weight * magnitude / distance;
    for (int32_t k = 0; k < 3; ++k)
    {
        a[k] += d[k] * scale;
    }
}

// The vortex's acceleration at x.
static void Swirl(const mvfxResolvedForce* force, const float x[3], float weight, float a[3])
{
    const float* u = force->vector;
    float r[3] = {x[0] - force->point[0], x[1] - force->point[1], x[2] - force->point[2]};
    float along = Dot(r, u);
    float across[3] = {r[0] - along * u[0], r[1] - along * u[1], r[2] - along * u[2]};
    float distance = sqrtf(Dot(across, across));
    if (!(distance > 0.0f))
    {
        return;
    }
    float inverse = 1.0f / distance;
    float turn[3] = {u[1] * across[2] - u[2] * across[1], u[2] * across[0] - u[0] * across[2],
                     u[0] * across[1] - u[1] * across[0]};
    for (int32_t k = 0; k < 3; ++k)
    {
        a[k] += weight * inverse * (force->strength * turn[k] - force->pull * across[k]);
    }
}

void mvfxForcesAt(const mvfxResolvedForce* forces, int32_t count, const float x[3], float a[3],
                  float* drag)
{
    for (int32_t f = 0; f < count; ++f)
    {
        const mvfxResolvedForce* force = &forces[f];
        float weight = FalloffAt(force, x);
        if (!(weight > 0.0f))
        {
            continue;
        }
        switch (force->kind)
        {
        case mvfx_forceConstant:
            for (int32_t k = 0; k < 3; ++k)
            {
                a[k] += weight * force->strength * force->vector[k];
            }
            break;
        case mvfx_forceWind:
            *drag += weight * force->strength;
            for (int32_t k = 0; k < 3; ++k)
            {
                a[k] += weight * force->strength * force->vector[k];
            }
            break;
        case mvfx_forceAttractor:
            Attract(force, x, weight, a);
            break;
        default:
            Swirl(force, x, weight, a);
            break;
        }
    }
}
