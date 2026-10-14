// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "integrate.h"

#include "direction.h"
#include "effect.h"
#include "elementary.h"
#include "force.h"

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

// The drag the winds and velocity fields without a falloff give, summed
// in order as mvfxForcesAt sums it.
static float SharedWind(const mvfxResolvedForce* forces, int32_t count)
{
    float wind = 0.0f;
    for (int32_t f = 0; f < count; ++f)
    {
        if (forces[f].falloff == mvfx_falloffNone)
        {
            wind += 1.0f * mvfxForceDrag(&forces[f]);
        }
    }
    return wind;
}

// How a space maps to the space the emitter's particles are stored in,
// with the instance's transform at the step's start: a world-space
// emitter's are offsets from its anchor.
static mvfxSpaceMap SpaceMapOf(const mvfxInstance* instance, int32_t emitter,
                               mvfxSimulationSpace space)
{
    const mvfxTransform* transform = &instance->from;
    const double* anchor = instance->emitters[emitter].anchor;
    bool world = instance->effect->emitters[emitter].space == mvfx_spaceWorld;
    mvfxSpaceMap map = {.rotation = {0.0f, 0.0f, 0.0f, 1.0f}, .sigma = 1.0f};
    const float* q = transform->rotation;
    if (world && space == mvfx_spaceWorld)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            map.after[k] = -anchor[k];
        }
    }
    else if (world)
    {
        // From the instance's space: scaled, turned, moved to its origin.
        for (int32_t k = 0; k < 4; ++k)
        {
            map.rotation[k] = q[k];
        }
        map.sigma = transform->scale;
        for (int32_t k = 0; k < 3; ++k)
        {
            map.after[k] = transform->origin[k] - anchor[k];
        }
    }
    else if (space == mvfx_spaceWorld)
    {
        // From the world into the instance's space: moved, turned back,
        // unscaled.
        map.rotation[0] = -q[0];
        map.rotation[1] = -q[1];
        map.rotation[2] = -q[2];
        map.rotation[3] = q[3];
        map.sigma = 1.0f / transform->scale;
        for (int32_t k = 0; k < 3; ++k)
        {
            map.before[k] = transform->origin[k];
        }
    }
    return map;
}

// Moves particles from first to end over seconds.
// What a range of particles moves by over a span.
typedef struct Range
{
    mvfxInstance* instance;
    int32_t emitter;
    const mvfxCompiledEmitter* compiled;
    float* p[3];
    float* v[3];
    float gravity[3];
    const float* dragScales;
    const mvfxResolvedForce* forces;
    float sharedDrag;
    mvfxDragFactors shared;
    const mvfxResolvedVelocityModules* modules;
    float limitDecay;
    const uint32_t* birthLow;
    const uint32_t* birthHigh;
    const float* inverseLife;
    // The tick and time the span starts at, and its seconds.
    int64_t tick;
    double time;
    double seconds;
} Range;

// Particle i's normalized age at the span's start, held to [0, 1].
static float AgeOf(const Range* r, int32_t i)
{
    int64_t birth = (int64_t)(((uint64_t)r->birthHigh[i] << 32u) | r->birthLow[i]);
    float age =
        (float)((double)(r->tick - birth) / (double)MVFX_TICKS_PER_SECOND) * r->inverseLife[i];
    return fminf(fmaxf(age, 0.0f), 1.0f);
}

// Moves particle i: its speed held to the limit, its motion under gravity,
// drag and forces, then the velocity modules' moves.
static void MoveParticle(const Range* r, int32_t i)
{
    float start[3] = {r->p[0][i], r->p[1][i], r->p[2][i]};
    float age = AgeOf(r, i);
    float acceleration[3] = {r->gravity[0], r->gravity[1], r->gravity[2]};
    float wind = 0.0f;
    mvfxForcesAt(r->forces, r->compiled->forceCount, start, r->time, acceleration, &wind);
    float drag = r->compiled->drag + wind;
    mvfxDragFactors f = r->shared;
    if (r->dragScales != nullptr)
    {
        f = mvfxDragFactorsOf(ParticleDrag(r->compiled, r->dragScales[i]) + (double)wind,
                              r->seconds);
    }
    else if (drag != r->sharedDrag)
    {
        f = mvfxDragFactorsOf((double)drag, r->seconds);
    }
    float velocity[3] = {r->v[0][i], r->v[1][i], r->v[2][i]};
    mvfxLimitSpeed(r->modules, age, r->limitDecay, velocity);
    for (int32_t k = 0; k < 3; ++k)
    {
        r->v[k][i] = velocity[k];
    }
    MoveLinear(r->p, r->v, i, acceleration, f);
    float place[3] = {r->p[0][i], r->p[1][i], r->p[2][i]};
    mvfxMoveByModules(r->modules, age, r->seconds, start, place);
    for (int32_t k = 0; k < 3; ++k)
    {
        r->p[k][i] = place[k];
    }
}

// Moves particles from first to end over seconds from tick.
static void MoveRange(mvfxInstance* instance, int32_t emitter, int32_t first, int32_t end,
                      int64_t tick, double seconds)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    Range r = {
        .instance = instance,
        .emitter = emitter,
        .compiled = compiled,
        .forces = instance->emitters[emitter].forces,
        .modules = &instance->emitters[emitter].velocityModules,
        .birthLow = mvfxAttributeArray(instance, emitter, mvfx_attributeBirthLow),
        .birthHigh = mvfxAttributeArray(instance, emitter, mvfx_attributeBirthHigh),
        .inverseLife = mvfxAttributeArray(instance, emitter, mvfx_attributeInverseLife),
        .tick = tick,
        .time = (double)tick / (double)MVFX_TICKS_PER_SECOND,
        .seconds = seconds,
    };
    GravityOf(instance, compiled, r.gravity);
    for (int32_t k = 0; k < 3; ++k)
    {
        r.p[k] =
            mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
        r.v[k] =
            mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeVelocityX + k));
    }
    if ((compiled->plan.present & ((uint64_t)1 << mvfx_attributeDrag)) != 0)
    {
        r.dragScales = mvfxAttributeArray(instance, emitter, mvfx_attributeDrag);
    }
    // The drag every particle has when its winds weigh as at no falloff:
    // its factors serve each particle whose drag comes to the same.
    r.sharedDrag = compiled->drag + SharedWind(r.forces, compiled->forceCount);
    r.shared = mvfxDragFactorsOf((double)r.sharedDrag, seconds);
    r.limitDecay = mvfxDragFactorsOf((double)compiled->velocityModules.limitDampen, seconds).decay;
    for (int32_t i = first; i < end; ++i)
    {
        MoveParticle(&r, i);
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

// Resolves the emitter's velocity modules for the step: their centers,
// axes and directions from the emitter's space to the particles'.
static void ResolveVelocityModules(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledVelocityModules* compiled =
        &instance->effect->emitters[emitter].velocityModules;
    mvfxResolvedVelocityModules* out = &instance->emitters[emitter].velocityModules;
    mvfxSpaceMap map = SpaceMapOf(instance, emitter, mvfx_spaceLocal);
    out->compiled = compiled;
    out->sigma = map.sigma;
    mvfxMapVector(&map, compiled->addedDirection, 1.0f, out->addedDirection);
    mvfxMapVector(&map, compiled->orbitAxis, 1.0f, out->orbitAxis);
    mvfxMapPoint(&map, compiled->orbitCenter, out->orbitCenter);
    mvfxMapPoint(&map, compiled->radialCenter, out->radialCenter);
}

void mvfxIntegrateEmitter(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    for (int32_t f = 0; f < compiled->forceCount; ++f)
    {
        const mvfxForce* force = &compiled->forces[f];
        mvfxSpaceMap map = SpaceMapOf(instance, emitter, force->space);
        instance->emitters[emitter].forces[f] =
            mvfxResolveForce(force, &map, (const float (*)[4])instance->parameters, instance->seed,
                             compiled->forceGrids[f]);
    }
    double seconds =
        (double)(instance->clock - instance->stepStart) / (double)MVFX_TICKS_PER_SECOND;
    ResolveVelocityModules(instance, emitter);
    MoveRange(instance, emitter, 0, instance->emitters[emitter].count, instance->stepStart,
              seconds);
}

void mvfxIntegrateNewborn(mvfxInstance* instance, int32_t emitter, int32_t at, int64_t birthTick)
{
    double seconds = (double)(instance->clock - birthTick) / (double)MVFX_TICKS_PER_SECOND;
    if (seconds > 0.0)
    {
        MoveRange(instance, emitter, at, at + 1, birthTick, seconds);
    }
}
