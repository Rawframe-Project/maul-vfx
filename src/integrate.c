// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "integrate.h"

#include "effect.h"
#include "elementary.h"

#include <math.h>

// Below this k h, the series: their first terms dropped are below 2^-40
// of the sum.
#define SERIES_LIMIT 0.25

mvfxDragFactors mvfxDragFactorsOf(double drag, double seconds)
{
    double x = drag * seconds;
    double decay = 0.0;
    double phi1 = 0.0;
    double phi2 = 0.0;
    if (x < SERIES_LIMIT)
    {
        // phi_n(x) = sum over j of (-x)^j / (j + n)!, eight terms.
        double term1 = 1.0;
        double term2 = 0.5;
        for (int32_t j = 0; j < 8; ++j)
        {
            phi1 += term1;
            phi2 += term2;
            term1 *= -x / (double)(j + 2);
            term2 *= -x / (double)(j + 3);
        }
        decay = 1.0 - x * phi1;
    }
    else
    {
        decay = mvfxExpDouble(-x);
        phi1 = (1.0 - decay) / x;
        phi2 = (1.0 - phi1) / x;
    }
    return (mvfxDragFactors){
        .decay = (float)decay,
        .carry = (float)(seconds * phi1),
        .push = (float)(seconds * seconds * phi2),
    };
}

// The emitter's gravity in the space its particles are stored in, with
// the instance's transform at the step's start.
static void GravityOf(const mvfxInstance* instance, const mvfxCompiledEmitter* emitter,
                      float out[3])
{
    const mvfxTransform* transform = &instance->from;
    if (emitter->gravitySpace == emitter->space)
    {
        out[0] = emitter->gravity[0];
        out[1] = emitter->gravity[1];
        out[2] = emitter->gravity[2];
        return;
    }
    const float* q = transform->rotation;
    if (emitter->gravitySpace == mvfx_spaceLocal)
    {
        // From the instance's space into the world: scaled, then turned.
        float scaled[3] = {emitter->gravity[0] * transform->scale,
                           emitter->gravity[1] * transform->scale,
                           emitter->gravity[2] * transform->scale};
        mvfxRotate(q, scaled, out);
        return;
    }
    // From the world into the instance's space: turned back, then unscaled.
    const float inverse[4] = {-q[0], -q[1], -q[2], q[3]};
    float turned[3];
    mvfxRotate(inverse, emitter->gravity, turned);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = turned[k] / transform->scale;
    }
}

// A particle's own drag: the emitter's times its multiplier, held to 0
// and MVFX_MAX_DRAG.
static double ParticleDrag(const mvfxCompiledEmitter* emitter, float scale)
{
    float drag = emitter->drag * scale;
    return !(drag > 0.0f) ? 0.0 : (double)fminf(drag, MVFX_MAX_DRAG);
}

// Moves particle i's position and velocity by the factors.
static void MoveLinear(float* const p[3], float* const v[3], int32_t i, const float a[3],
                       mvfxDragFactors f)
{
    for (int32_t k = 0; k < 3; ++k)
    {
        float v0 = v[k][i];
        p[k][i] += v0 * f.carry + a[k] * f.push;
        v[k][i] = v0 * f.decay + a[k] * f.carry;
    }
}

// Turns a unit quaternion q by the rotation vector theta (axis times
// angle), in the parent's frame: q' = r(theta) q, then normalized.
static void TurnBy(float q[4], const float theta[3])
{
    float angle = sqrtf(theta[0] * theta[0] + theta[1] * theta[1] + theta[2] * theta[2]);
    if (!(angle > 0.0f))
    {
        return;
    }
    float sine = 0.0f;
    float cosine = 1.0f;
    mvfxSinCos(0.5f * angle, &sine, &cosine);
    float s = sine / angle;
    float r[4] = {theta[0] * s, theta[1] * s, theta[2] * s, cosine};
    float out[4] = {
        r[3] * q[0] + r[0] * q[3] + r[1] * q[2] - r[2] * q[1],
        r[3] * q[1] - r[0] * q[2] + r[1] * q[3] + r[2] * q[0],
        r[3] * q[2] + r[0] * q[1] - r[1] * q[0] + r[2] * q[3],
        r[3] * q[3] - r[0] * q[0] - r[1] * q[1] - r[2] * q[2],
    };
    float length = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
    for (int32_t k = 0; k < 4; ++k)
    {
        q[k] = out[k] / length;
    }
}

// Moves particle i's angle and spin, and its orientation and angular
// velocity, by the angular factors.
static void MoveAngular(mvfxInstance* instance, int32_t emitter, int32_t i, mvfxDragFactors f)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    uint64_t present = compiled->plan.present;
    if ((present & ((uint64_t)1 << mvfx_attributeAngle)) != 0)
    {
        float* angle = mvfxAttributeArray(instance, emitter, mvfx_attributeAngle);
        float* spin = mvfxAttributeArray(instance, emitter, mvfx_attributeSpin);
        angle[i] += spin[i] * f.carry;
        spin[i] *= f.decay;
    }
    if ((present & ((uint64_t)1 << mvfx_attributeOrientationX)) != 0)
    {
        float* q[4];
        float* w[3];
        for (int32_t k = 0; k < 4; ++k)
        {
            q[k] = mvfxAttributeArray(instance, emitter,
                                      (mvfxAttribute)(mvfx_attributeOrientationX + k));
        }
        for (int32_t k = 0; k < 3; ++k)
        {
            w[k] =
                mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeAngularX + k));
        }
        float theta[3] = {w[0][i] * f.carry, w[1][i] * f.carry, w[2][i] * f.carry};
        float turned[4] = {q[0][i], q[1][i], q[2][i], q[3][i]};
        TurnBy(turned, theta);
        for (int32_t k = 0; k < 4; ++k)
        {
            q[k][i] = turned[k];
        }
        for (int32_t k = 0; k < 3; ++k)
        {
            w[k][i] *= f.decay;
        }
    }
}

// Moves particles from first to end over seconds.
static void MoveRange(mvfxInstance* instance, int32_t emitter, int32_t first, int32_t end,
                      double seconds)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    float a[3];
    GravityOf(instance, compiled, a);
    float* p[3];
    float* v[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        p[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
        v[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeVelocityX + k));
    }
    bool perParticle = (compiled->plan.present & ((uint64_t)1 << mvfx_attributeDrag)) != 0;
    const float* scales =
        perParticle ? mvfxAttributeArray(instance, emitter, mvfx_attributeDrag) : nullptr;
    mvfxDragFactors shared = mvfxDragFactorsOf((double)compiled->drag, seconds);
    for (int32_t i = first; i < end; ++i)
    {
        mvfxDragFactors f =
            perParticle ? mvfxDragFactorsOf(ParticleDrag(compiled, scales[i]), seconds) : shared;
        MoveLinear(p, v, i, a, f);
    }
    uint64_t rotating =
        ((uint64_t)1 << mvfx_attributeAngle) | ((uint64_t)1 << mvfx_attributeOrientationX);
    if ((compiled->plan.present & rotating) != 0)
    {
        mvfxDragFactors angular = mvfxDragFactorsOf((double)compiled->angularDrag, seconds);
        for (int32_t i = first; i < end; ++i)
        {
            MoveAngular(instance, emitter, i, angular);
        }
    }
}

void mvfxIntegrateEmitter(mvfxInstance* instance, int32_t emitter)
{
    double seconds =
        (double)(instance->clock - instance->stepStart) / (double)MVFX_TICKS_PER_SECOND;
    MoveRange(instance, emitter, 0, instance->emitters[emitter].count, seconds);
}

void mvfxIntegrateNewborn(mvfxInstance* instance, int32_t emitter, int32_t at, int64_t birthTick)
{
    double seconds = (double)(instance->clock - birthTick) / (double)MVFX_TICKS_PER_SECOND;
    if (seconds > 0.0)
    {
        MoveRange(instance, emitter, at, at + 1, seconds);
    }
}
