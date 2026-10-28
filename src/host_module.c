// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "host_module.h"

#include "appearance.h"
#include "random.h"
#include "rigid_shape.h"

#include "maul-vfx/instance.h"

#include <float.h>
#include <math.h>

// The salt of a host module's seed.
#define HOST_SALT 0x484F5354u

// The array of an attribute, or NULL when the emitter does not store it.
static void* ArrayOf(mvfxInstance* instance, int32_t emitter, int32_t attribute)
{
    if (!mvfxHasAttribute(&instance->effect->emitters[emitter].plan, attribute))
    {
        return nullptr;
    }
    return mvfxAttributeArray(instance, emitter, (mvfxAttribute)attribute);
}

// Fills n pointers from consecutive attributes.
static void Fill(mvfxInstance* instance, int32_t emitter, int32_t first, int32_t n, float* out[])
{
    for (int32_t k = 0; k < n; ++k)
    {
        out[k] = ArrayOf(instance, emitter, first + k);
    }
}

static mvfxParticleArrays ArraysOf(mvfxInstance* instance, int32_t emitter, bool killable)
{
    mvfxParticleArrays arrays = {
        .count = instance->emitters[emitter].count,
        .angle = ArrayOf(instance, emitter, mvfx_attributeAngle),
        .spin = ArrayOf(instance, emitter, mvfx_attributeSpin),
        .frame = ArrayOf(instance, emitter, mvfx_attributeFrame),
        .serial = ArrayOf(instance, emitter, mvfx_attributeSerial),
        .birthLow = ArrayOf(instance, emitter, mvfx_attributeBirthLow),
        .birthHigh = ArrayOf(instance, emitter, mvfx_attributeBirthHigh),
        .inverseLife = ArrayOf(instance, emitter, mvfx_attributeInverseLife),
        .collisions = ArrayOf(instance, emitter, mvfx_attributeCollisions),
        .kill = killable ? instance->killScratch : nullptr,
    };
    Fill(instance, emitter, mvfx_attributePositionX, 3, arrays.position);
    Fill(instance, emitter, mvfx_attributeVelocityX, 3, arrays.velocity);
    Fill(instance, emitter, mvfx_attributeColorR, 4, arrays.color);
    Fill(instance, emitter, mvfx_attributeSizeX, 3, arrays.size);
    Fill(instance, emitter, mvfx_attributeOrientationX, 4, arrays.orientation);
    Fill(instance, emitter, mvfx_attributeAngularX, 3, arrays.angularVelocity);
    arrays.neighborCount = ArrayOf(instance, emitter, mvfx_attributeNeighborCount);
    for (int32_t k = 0; k < 3; ++k)
    {
        arrays.neighborCenter[k] =
            ArrayOf(instance, emitter, (mvfxAttribute)(mvfx_attributeNeighborCenterX + k));
        arrays.neighborVelocity[k] =
            ArrayOf(instance, emitter, (mvfxAttribute)(mvfx_attributeNeighborVelocityX + k));
    }
    for (int32_t i = 0; i < MVFX_MAX_CUSTOM_ATTRIBUTES; ++i)
    {
        Fill(instance, emitter, mvfx_attributeCustom + 4 * i, 4, arrays.custom[i]);
    }
    return arrays;
}

// Whether particle i's values in n arrays (NULL for one not stored) are
// each finite and at most bound in size, bound per array.
static bool AreWithin(float* const* arrays, int32_t n, int32_t i, const float* bounds)
{
    bool within = true;
    for (int32_t k = 0; k < n; ++k)
    {
        within = within && (arrays[k] == nullptr || fabsf(arrays[k][i]) <= bounds[k]);
    }
    return within;
}

// Whether any of count values is not finite or past bound in size: a
// reduction over one array, which vectorizes.
static bool AnyPast(const float* values, int32_t count, float bound)
{
    uint32_t past = 0;
    for (int32_t i = 0; values != nullptr && i < count; ++i)
    {
        past |= fabsf(values[i]) <= bound ? 0u : 1u;
    }
    return past != 0;
}

// Whether particle i's size (NULL arrays for none stored) keeps the axes
// past the first axes equal to X, as a rigid shape takes them.
static bool IsTied(float* const* size, int32_t axes, int32_t i)
{
    bool tied = true;
    for (int32_t k = axes; size[0] != nullptr && k < 3; ++k)
    {
        tied = tied && size[k][i] == size[0][i];
    }
    return tied;
}

// Whether any of count sizes is not tied (IsTied), as AnyPast.
static bool AnyUntied(float* const* size, int32_t axes, int32_t count)
{
    uint32_t untied = 0;
    for (int32_t k = axes; size[0] != nullptr && k < 3; ++k)
    {
        for (int32_t i = 0; i < count; ++i)
        {
            untied |= size[k][i] == size[0][i] ? 0u : 1u;
        }
    }
    return untied != 0;
}

// How many times the speed bound a rigid body's spin may turn its extent
// at: a sphere rolling with each axis of its velocity at the bound turns
// its diameter at twice its speed, 2 sqrt(3) times the bound.
#define RIGID_SPIN_SPEEDS 4.0

// The fastest a rigid body may spin, in radians a second: the step
// squares its turn over a substep (mvfxTurnBy) and its momentum in its
// units (below 2 times its spin: its mass is below 1 there and its points
// within 1 of its center on each axis) in binary32.
#define RIGID_MOST_SPIN 0x1p60

// Whether rigid body i's spin (none stored is none) is one its own step
// may give it, in binary64, where a tiny body's spin and its square stay
// in range. Within RIGID_MOST_SPIN, and for a body of no volume (no
// inertia: its step does not turn it) within the cap on each axis; for
// another, its angular momentum within what the spin cap about every axis
// gives it (|I w| within sqrt(3) cap times its inertia's trace, which is
// at least its largest moment; the free flow keeps the momentum's length
// as the body precesses and a component passes the cap, and as its energy
// drifts at a turn of many radians a substep), or its extent
// (mvfxBodyExtent) turned within RIGID_SPIN_SPEEDS times the speed bound
// (a sphere 1e-20 across rolling at 1e-9 lengths a second turns at 1e11
// radians a second, past the cap).
static bool IsSpinWithin(mvfxInstance* instance, int32_t emitter, const mvfxParticleArrays* a,
                         int32_t i, double fastest)
{
    if (a->angularVelocity[0] == nullptr)
    {
        return true;
    }
    // In the body's frame, where its inertia is: turned back by its
    // orientation (none stored is none).
    double v[3];
    double q[3] = {0.0, 0.0, 0.0};
    for (int32_t k = 0; k < 3; ++k)
    {
        v[k] = (double)a->angularVelocity[k][i];
        q[k] = a->orientation[0] != nullptr ? -(double)a->orientation[k][i] : 0.0;
    }
    double qw = a->orientation[0] != nullptr ? (double)a->orientation[3][i] : 1.0;
    double t[3] = {2.0 * (q[1] * v[2] - q[2] * v[1]), 2.0 * (q[2] * v[0] - q[0] * v[2]),
                   2.0 * (q[0] * v[1] - q[1] * v[0])};
    double w[3] = {v[0] + qw * t[0] + (q[1] * t[2] - q[2] * t[1]),
                   v[1] + qw * t[1] + (q[2] * t[0] - q[0] * t[2]),
                   v[2] + qw * t[2] + (q[0] * t[1] - q[1] * t[0])};
    float size[3];
    mvfxSizeOf(instance, emitter, i, size);
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    mvfxBodyMass mass;
    (void)mvfxBodyShapeOf(&compiled->rigid, compiled->hull, size, &mass);
    const float* m = mass.inertia;
    double momentum[3] = {(double)m[0] * w[0] + (double)m[3] * w[1] + (double)m[4] * w[2],
                          (double)m[3] * w[0] + (double)m[1] * w[1] + (double)m[5] * w[2],
                          (double)m[4] * w[0] + (double)m[5] * w[1] + (double)m[2] * w[2]};
    double held = momentum[0] * momentum[0] + momentum[1] * momentum[1] + momentum[2] * momentum[2];
    double cap = (double)compiled->overLife.caps.spin;
    double trace = (double)m[0] + (double)m[1] + (double)m[2];
    double turn = w[0] * w[0] + w[1] * w[1] + w[2] * w[2];
    double extent = (double)mvfxBodyExtent(&compiled->rigid, compiled->hull, size);
    double bound = RIGID_SPIN_SPEEDS * fastest;
    if (!(turn <= RIGID_MOST_SPIN * RIGID_MOST_SPIN))
    {
        return false;
    }
    if (!(trace > 0.0))
    {
        return fabs(v[0]) <= cap && fabs(v[1]) <= cap && fabs(v[2]) <= cap;
    }
    return held <= 3.0 * cap * cap * trace * trace || turn * extent * extent <= bound * bound;
}

// The share past the speed bound a held speed may round to: each
// component scaled to it rounds to binary32 apart (mvfxHoldSpeed).
#define SPEED_ROUNDING 0x1p-20

// Whether particle i's speed is within fastest, its hold's rounding
// allowed, in binary64 (a NaN component is not).
static bool IsSpeedWithin(const mvfxParticleArrays* a, int32_t i, double fastest)
{
    double v[3] = {(double)a->velocity[0][i], (double)a->velocity[1][i], (double)a->velocity[2][i]};
    double most = fastest * (1.0 + SPEED_ROUNDING);
    return v[0] * v[0] + v[1] * v[1] + v[2] * v[2] <= most * most;
}

// Whether any particle's speed is past fastest (IsSpeedWithin).
static bool AnySpeedPast(const mvfxParticleArrays* a, double fastest)
{
    bool past = false;
    for (int32_t i = 0; !past && i < a->count; ++i)
    {
        past = !IsSpeedWithin(a, i, fastest);
    }
    return past;
}

// Whether any of an emitter's rigid bodies spins past its bound
// (IsSpinWithin).
static bool AnySpinPast(mvfxInstance* instance, int32_t emitter, const mvfxParticleArrays* a,
                        double fastest)
{
    bool past = false;
    for (int32_t i = 0; !past && i < a->count; ++i)
    {
        past = !IsSpinWithin(instance, emitter, a, i, fastest);
    }
    return past;
}

// Whether any of n arrays holds a value past its bound.
static bool AnyArrayPast(float* const* arrays, int32_t n, int32_t count, const float* bounds)
{
    bool past = false;
    for (int32_t k = 0; k < n; ++k)
    {
        past = past || AnyPast(arrays[k], count, bounds[k]);
    }
    return past;
}

// Marks for the kill each particle a host module left holding a value
// that is not finite or is past its limit: a place past MVFX_MAX_PLACE, a
// speed past the one the step holds (IsSpeedWithin), a size, colour,
// spin, angular velocity or custom attribute past what the emitter's
// particles may store (a rigid body's angular velocity past its surface's
// bound, IsSpinWithin), an angle or orientation not finite, a size whose
// axes its rigid shape does not take differ from X (mvfxRigidShape).
// Those not marked already are counted in valuesMarked.
static void MarkPastLimits(mvfxInstance* instance, int32_t emitter, const mvfxParticleArrays* a)
{
    mvfxEmitterState* state = &instance->emitters[emitter];
    const mvfxStoredCaps* caps = &instance->effect->emitters[emitter].overLife.caps;
    float fastest = MVFX_MAX_SPEED * state->velocityModules.sigma;
    const float finite[4] = {FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX};
    const float places[3] = {MVFX_MAX_PLACE, MVFX_MAX_PLACE, MVFX_MAX_PLACE};
    const float spins[3] = {caps->spin, caps->spin, caps->spin};
    float* angle[1] = {a->angle};
    float* spin[1] = {a->spin};
    int32_t axes = mvfxSizeAxes(&instance->effect->emitters[emitter].rigid);
    bool rigid = instance->effect->emitters[emitter].rigid.enabled;
    // An array at a time first: the particles are looked at one by one
    // only where some value is past its limit.
    int32_t n = a->count;
    bool past = AnyArrayPast(a->position, 3, n, places) || AnySpeedPast(a, (double)fastest) ||
                AnyArrayPast(a->color, 4, n, caps->color) ||
                AnyArrayPast(a->size, 3, n, caps->size) || AnyArrayPast(angle, 1, n, finite) ||
                AnyArrayPast(spin, 1, n, &caps->spin) ||
                AnyArrayPast(a->orientation, 4, n, finite) ||
                (rigid ? AnySpinPast(instance, emitter, a, (double)fastest)
                       : AnyArrayPast(a->angularVelocity, 3, n, spins)) ||
                AnyUntied(a->size, axes, n);
    for (int32_t c = 0; !past && c < MVFX_MAX_CUSTOM_ATTRIBUTES; ++c)
    {
        const float custom[4] = {caps->custom[c], caps->custom[c], caps->custom[c],
                                 caps->custom[c]};
        past = AnyArrayPast(a->custom[c], 4, n, custom);
    }
    for (int32_t i = 0; past && i < n; ++i)
    {
        bool within =
            AreWithin(a->position, 3, i, places) && IsSpeedWithin(a, i, (double)fastest) &&
            AreWithin(a->color, 4, i, caps->color) && AreWithin(a->size, 3, i, caps->size) &&
            AreWithin(angle, 1, i, finite) && AreWithin(spin, 1, i, &caps->spin) &&
            AreWithin(a->orientation, 4, i, finite) &&
            (rigid ? IsSpinWithin(instance, emitter, a, i, (double)fastest)
                   : AreWithin(a->angularVelocity, 3, i, spins)) &&
            IsTied(a->size, axes, i);
        for (int32_t c = 0; within && c < MVFX_MAX_CUSTOM_ATTRIBUTES; ++c)
        {
            const float custom[4] = {caps->custom[c], caps->custom[c], caps->custom[c],
                                     caps->custom[c]};
            within = AreWithin(a->custom[c], 4, i, custom);
        }
        if (!within && instance->killScratch[i] == 0)
        {
            instance->killScratch[i] = 1;
            state->valuesMarked += 1;
        }
    }
}

void mvfxRunHostModules(mvfxInstance* instance, int32_t emitter, mvfxModuleStage stage)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    for (int32_t m = 0; m < compiled->hostModuleCount; ++m)
    {
        const mvfxHostModule* module = &compiled->hostModules[m];
        if (module->stage != stage)
        {
            continue;
        }
        mvfxWords4 seed =
            mvfxPcg4d((mvfxWords4){instance->seed, (uint32_t)emitter, (uint32_t)m, HOST_SALT});
        const double* anchor = instance->emitters[emitter].anchor;
        mvfxModuleCall call = {
            .particles = ArraysOf(instance, emitter, stage == mvfx_stageAfterMotion),
            .stage = stage,
            .emitter = emitter,
            .module = m,
            .seconds =
                (double)(instance->clock - instance->stepStart) / (double)MVFX_TICKS_PER_SECOND,
            .startTick = instance->stepStart,
            .endTick = instance->clock,
            .anchor = {anchor[0], anchor[1], anchor[2]},
            .worldSpace = compiled->space == mvfx_spaceWorld,
            .seed = seed.x,
        };
        module->function(&call, module->user);
        MarkPastLimits(instance, emitter, &call.particles);
    }
}

void mvfxModule_Random(const mvfxModuleCall* call, uint32_t serial, uint32_t stream,
                       uint32_t out[4])
{
    mvfxWords4 words =
        mvfxPcg4d((mvfxWords4){call->seed, serial, stream, (uint32_t)(uint64_t)call->endTick});
    out[0] = words.x;
    out[1] = words.y;
    out[2] = words.z;
    out[3] = words.w;
}
