// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "force.h"

#include "direction.h"
#include "elementary.h"
#include "grid.h"
#include "noise.h"

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

static bool IsNoiseValid(const mvfxNoise* noise)
{
    return noise->frequency > 0.0f && noise->frequency <= MVFX_MAX_FREQUENCY &&
           noise->octaves >= 1 && noise->octaves <= MVFX_MAX_OCTAVES &&
           IsWithin(noise->lacunarity, 1.0f, 4.0f) && IsWithin(noise->gain, 0.0f, 1.0f) &&
           IsVectorWithin(noise->scroll, MVFX_MAX_EXTENT);
}

// Whether a unit quaternion within 0.001, finite.
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

static bool IsBoxValid(const mvfxForce* force)
{
    for (int32_t k = 0; k < 3; ++k)
    {
        if (!(force->boxSize[k] > 0.0f && force->boxSize[k] <= MVFX_MAX_EXTENT))
        {
            return false;
        }
    }
    return IsRotation(force->boxRotation) && force->border <= mvfx_borderRepeat;
}

bool mvfxIsForceValid(const mvfxForce* force, int32_t parameterCount)
{
    if (force->kind > mvfx_forceGas || force->space > mvfx_spaceLocal ||
        force->fieldMode > mvfx_fieldVelocity || !IsWithin(force->tightness, 0.0f, MVFX_MAX_DRAG) ||
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
        return IsWithin(force->softening, MVFX_MIN_SOFTENING, MVFX_MAX_EXTENT) &&
               IsWithin(force->killRadius, 0.0f, MVFX_MAX_EXTENT);
    case mvfx_forceVortex:
        return IsAxis(force->vector) && IsWithin(force->pull, -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT);
    case mvfx_forceTurbulence:
        return IsNoiseValid(&force->noise);
    case mvfx_forceVectorField:
        return IsBoxValid(force);
    case mvfx_forceGas:
        return force->gas >= 0 && force->gas < MVFX_MAX_GASES;
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
    if (out.kind == mvfx_forceVectorField)
    {
        double square = 0.0;
        for (int32_t k = 0; k < 4; ++k)
        {
            square += (double)out.boxRotation[k] * (double)out.boxRotation[k];
        }
        double inverse = 1.0 / sqrt(square);
        for (int32_t k = 0; k < 4; ++k)
        {
            out.boxRotation[k] = (float)((double)out.boxRotation[k] * inverse);
        }
    }
    return out;
}

void mvfxMapPoint(const mvfxSpaceMap* map, const float p[3], float out[3])
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

void mvfxMapVector(const mvfxSpaceMap* map, const float v[3], float sigma, float out[3])
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
                                   const float (*parameters)[4], uint32_t instanceSeed,
                                   const struct mvfxGrid* grid)
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
        .killRadius = force->killRadius * sigma,
        .pull = force->pull * sigma,
        .radius = force->falloff.radius * sigma,
        .exponent = force->falloff.exponent,
        .fieldMode = force->fieldMode,
        .tightness = force->tightness,
        .map = *map,
        .source = force,
        .grid = grid,
        .seed = force->noise.seedPerInstance ? force->noise.seed + instanceSeed * 0x85EBCA6Bu
                                             : force->noise.seed,
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
    mvfxMapVector(map, force->vector, force->kind == mvfx_forceVortex ? 1.0f : sigma, out.vector);
    mvfxMapPoint(map, force->point, out.point);
    mvfxMapPoint(map, force->falloff.center, out.center);
    mvfxMapVector(map, force->falloff.axis, 1.0f, out.axis);
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

float mvfxForceDrag(const mvfxResolvedForce* force)
{
    if (force->kind == mvfx_forceWind)
    {
        return force->strength;
    }
    bool field = force->kind == mvfx_forceTurbulence || force->kind == mvfx_forceVectorField ||
                 force->kind == mvfx_forceGas;
    return field && force->fieldMode == mvfx_fieldVelocity ? force->tightness : 0.0f;
}

// A vector field's vector at a place in its force's space: the grid
// sampled in the box's own space, turned out with the box; false outside
// it under the none border.
static bool SampleBox(const mvfxResolvedForce* force, const double place[3], float out[3])
{
    const mvfxForce* source = force->source;
    const float* q = source->boxRotation;
    float d[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        d[k] = (float)(place[k] - (double)source->point[k]);
    }
    const float inverse[4] = {-q[0], -q[1], -q[2], q[3]};
    float local[3];
    mvfxRotate(inverse, d, local);
    float u[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        u[k] = local[k] / source->boxSize[k] + 0.5f;
    }
    float sample[4];
    if (!mvfxSampleGrid(force->grid, u, source->border, sample))
    {
        return false;
    }
    mvfxRotate(q, sample, out);
    return true;
}

// A gas's velocity at a place in its box's frame, false outside the box
// (a 2D gas's reaches along its Z without end).
static bool SampleGas(const mvfxResolvedForce* force, const double place[3], float out[3])
{
    const mvfxGasLattice* l = &force->gasLattice;
    float p[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        p[k] = (float)(place[k] / (double)l->cell) + 0.5f * (float)l->n[k];
        if (l->n[k] > 1 && !(p[k] >= 0.0f && p[k] <= (float)l->n[k]))
        {
            return false;
        }
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = l->n[k] > 1 ? mvfxGasSample(force->gasVelocity[k], l, k, p) : 0.0f;
    }
    return true;
}

// A field's vector at x: its acceleration added to a, or, as a velocity,
// its pull and its drag.
static void Field(const mvfxResolvedForce* force, const float x[3], double time, float weight,
                  float a[3], float* drag)
{
    double place[3];
    mvfxUnmapPoint(&force->map, x, place);
    float value[3];
    if (force->kind == mvfx_forceTurbulence)
    {
        mvfxCurlNoise(&force->source->noise, force->seed, place, time, value);
    }
    else if (force->kind == mvfx_forceGas ? !SampleGas(force, place, value)
                                          : !SampleBox(force, place, value))
    {
        return;
    }
    float field[3];
    mvfxMapVector(&force->map, value, force->map.sigma * force->strength, field);
    float scale = weight;
    if (force->fieldMode == mvfx_fieldVelocity)
    {
        *drag += weight * force->tightness;
        scale *= force->tightness;
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        a[k] += scale * field[k];
    }
}

void mvfxForcesAt(const mvfxResolvedForce* forces, int32_t count, const float x[3], double time,
                  float a[3], float* drag)
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
        case mvfx_forceTurbulence:
        case mvfx_forceVectorField:
        case mvfx_forceGas:
            Field(force, x, time, weight, a, drag);
            break;
        default:
            Swirl(force, x, weight, a);
            break;
        }
    }
}

void mvfxUnmapVector(const mvfxSpaceMap* map, const float v[3], float out[3])
{
    const float* q = map->rotation;
    const float inverse[4] = {-q[0], -q[1], -q[2], q[3]};
    mvfxRotate(inverse, v, out);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] /= map->sigma;
    }
}

void mvfxMapPointDouble(const mvfxSpaceMap* map, const double p[3], float out[3])
{
    double d[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        d[k] = (p[k] - map->before[k]) * (double)map->sigma;
    }
    double turned[3];
    mvfxRotateDouble(map->rotation, d, turned);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = (float)(turned[k] + map->after[k]);
    }
}
