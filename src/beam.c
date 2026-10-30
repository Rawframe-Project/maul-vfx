// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "beam.h"

#include "direction.h"
#include "elementary.h"
#include "noise.h"

#include <math.h>

// A beam point's place in the instance's space.
static void PointOf(const mvfxInstance* instance, const mvfxBeamPoint* point, float out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = point->fromParameter ? instance->parameters[point->parameter][k] : point->point[k];
    }
}

// The curve's place a fraction t along it: a line, or a quadratic or
// cubic Bezier curve through the controls.
static void CurveAt(const float p[4][3], int32_t controlCount, float t, float out[3])
{
    float u = 1.0f - t;
    for (int32_t k = 0; k < 3; ++k)
    {
        switch (controlCount)
        {
        case 0:
            out[k] = u * p[0][k] + t * p[1][k];
            break;
        case 1:
            out[k] = u * u * p[0][k] + 2.0f * u * t * p[1][k] + t * t * p[2][k];
            break;
        default:
            out[k] = u * u * u * p[0][k] + 3.0f * u * u * t * p[1][k] + 3.0f * u * t * t * p[2][k] +
                     t * t * t * p[3][k];
            break;
        }
    }
}

// The noise across the beam at a fraction t along it: gradient noise
// along the beam and over time, its part along the chord taken out, faded
// by sin(pi t).
static void NoiseAt(const mvfxInstance* instance, const mvfxBeamDef* beam, uint32_t seed,
                    const float chord[3], float t, float out[3])
{
    double seconds = (double)instance->clock / (double)MVFX_TICKS_PER_SECOND;
    const double place[3] = {(double)(t * beam->noiseFrequency), seconds * (double)beam->noiseSpeed,
                             0.0};
    float value[3];
    float gradient[3][3];
    mvfxGradientNoise3(place, seed, value, gradient);
    float along = value[0] * chord[0] + value[1] * chord[1] + value[2] * chord[2];
    float sine = 0.0f;
    float cosine = 0.0f;
    mvfxSinCos(3.14159265f * t, &sine, &cosine);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = (value[k] - along * chord[k]) * beam->noiseAmplitude * sine;
    }
}

// A point in the instance's space in the particles' space: as it is for a
// local emitter; through the transform less the anchor for a world one.
static void IntoParticles(const mvfxInstance* instance, int32_t emitter, const float point[3],
                          float out[3])
{
    if (instance->effect->emitters[emitter].space == mvfx_spaceLocal)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            out[k] = point[k];
        }
        return;
    }
    const mvfxTransform* to = &instance->to;
    const float scaled[3] = {point[0] * to->scale, point[1] * to->scale, point[2] * to->scale};
    float turned[3];
    mvfxRotate(to->rotation, scaled, turned);
    const double* anchor = instance->emitters[emitter].anchor;
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = (float)((to->origin[k] - anchor[k]) + (double)turned[k]);
    }
}

// The unit chord from the start to the end; 0 where they meet.
static void ChordOf(const mvfxPlannedBeam* resolved, int32_t controlCount, float out[3])
{
    const float* start = resolved->points[0];
    const float* end = resolved->points[controlCount + 1];
    float square = 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = end[k] - start[k];
        square += out[k] * out[k];
    }
    float length = sqrtf(square);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = length > 0.0f ? out[k] / length : 0.0f;
    }
}

void mvfxResolveBeam(const mvfxInstance* instance, int32_t emitter, mvfxPlannedBeam* beamOut)
{
    *beamOut = (mvfxPlannedBeam){0};
    const mvfxBeamDef* beam = &instance->effect->emitters[emitter].beam;
    if (!beam->enable)
    {
        return;
    }
    PointOf(instance, &beam->start, beamOut->points[0]);
    for (int32_t c = 0; c < beam->controlCount; ++c)
    {
        PointOf(instance, &beam->controls[c], beamOut->points[c + 1]);
    }
    PointOf(instance, &beam->end, beamOut->points[beam->controlCount + 1]);
    double seconds = (double)instance->clock / (double)MVFX_TICKS_PER_SECOND;
    beamOut->timeCell = mvfxNoiseCell(seconds * (double)beam->noiseSpeed, &beamOut->timeFraction);
}

void mvfxPlaceBeam(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    const mvfxBeamDef* beam = &compiled->beam;
    mvfxEmitterState* state = &instance->emitters[emitter];
    if (!beam->enable || state->count == 0)
    {
        return;
    }
    float* p[3];
    float* v[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        p[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
        v[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeVelocityX + k));
    }
    // The strike's noise: the seed of its first point.
    const uint32_t* serials = mvfxAttributeArray(instance, emitter, mvfx_attributeSerial);
    uint32_t seed = instance->seed ^ (serials[0] * 0x9E3779B9u);
    mvfxPlannedBeam resolved;
    mvfxResolveBeam(instance, emitter, &resolved);
    float chord[3];
    ChordOf(&resolved, beam->controlCount, chord);
    float* distances = mvfxHasAttribute(&compiled->plan, mvfx_attributeRibbonDistance)
                           ? mvfxAttributeArray(instance, emitter, mvfx_attributeRibbonDistance)
                           : nullptr;
    for (int32_t i = 0; i < state->count; ++i)
    {
        float t = (float)i / (float)beam->segments;
        float point[3];
        float noise[3];
        CurveAt(resolved.points, beam->controlCount, t, point);
        NoiseAt(instance, beam, seed, chord, t, noise);
        for (int32_t k = 0; k < 3; ++k)
        {
            point[k] += noise[k];
        }
        float placed[3];
        IntoParticles(instance, emitter, point, placed);
        float gap = 0.0f;
        for (int32_t k = 0; k < 3; ++k)
        {
            float d = i > 0 ? placed[k] - p[k][i - 1] : 0.0f;
            gap += d * d;
            p[k][i] = placed[k];
            v[k][i] = 0.0f;
        }
        if (distances != nullptr)
        {
            distances[i] = i > 0 ? distances[i - 1] + sqrtf(gap) : 0.0f;
        }
    }
}
