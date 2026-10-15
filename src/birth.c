// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Initial values written into an emitter's arrays.

#include "birth.h"

#include "attach.h"
#include "direction.h"
#include "elementary.h"
#include "event.h"
#include "integrate.h"
#include "random.h"
#include "rigid.h"
#include "shape.h"
#include "value.h"
#include "velocity.h"

// The salts that set each purpose's draws apart.
#define LIFE_SALT        0x4C494645u
#define SHAPE_SALT       0x53484150u
#define VELOCITY_SALT    0x56454C4Fu
#define SIZE_SALT        0x53495A45u
#define COLOR_SALT       0x434F4C52u
#define ANGLE_SALT       0x414E474Cu
#define SPIN_SALT        0x5350494Eu
#define ORIENTATION_SALT 0x4F524E54u
#define ANGULAR_SALT     0x414E4755u
#define FRAME_SALT       0x46524D45u
#define CUSTOM_SALT      0x43555354u
#define DRAG_SALT        0x44524147u
#define MESH_SALT        0x4D455348u

#define TWO_PI 6.28318530717958647692f

// The shortest life a particle is given, one tick.
#define SHORTEST_LIFE 0x1p-20f

// The words of a purpose at a serial.
static mvfxWords4 Draw(const mvfxInstance* instance, int32_t emitter, uint32_t salt,
                       uint64_t serial)
{
    return mvfxPcg4d((mvfxWords4){
        instance->seed,
        ((uint32_t)emitter * 0x9E3779B9u) ^ salt,
        (uint32_t)serial,
        (uint32_t)(serial >> 32u),
    });
}

float mvfxSourceAge(const mvfxCompiledEmitter* emitter, int64_t tick)
{
    int64_t since = tick - emitter->delay;
    if (emitter->duration > 0)
    {
        return (float)((double)(since % emitter->duration) / (double)emitter->duration);
    }
    return (float)((double)since / (double)MVFX_TICKS_PER_SECOND);
}

float mvfxInverseLifeOf(const mvfxInstance* instance, int32_t emitter, uint64_t serial,
                        int64_t tick)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    float life = mvfxDrawScalar(&compiled->lifetime, mvfxSourceAge(compiled, tick),
                                instance->parameters, Draw(instance, emitter, LIFE_SALT, serial));
    life = life >= SHORTEST_LIFE ? life : SHORTEST_LIFE;
    life = life <= MVFX_MAX_LIFETIME ? life : MVFX_MAX_LIFETIME;
    return 1.0f / life;
}

static void Store(mvfxInstance* instance, int32_t emitter, mvfxAttribute attribute, int32_t at,
                  float value)
{
    ((float*)mvfxAttributeArray(instance, emitter, attribute))[at] = value;
}

static void StoreWord(mvfxInstance* instance, int32_t emitter, mvfxAttribute attribute, int32_t at,
                      uint32_t value)
{
    ((uint32_t*)mvfxAttributeArray(instance, emitter, attribute))[at] = value;
}

// Whether the emitter stores an attribute.
static bool Has(const mvfxCompiledEmitter* emitter, mvfxAttribute attribute)
{
    return mvfxHasAttribute(&emitter->plan, attribute);
}

// A uniformly random rotation from three uniforms (Shoemake, "Uniform
// random rotations", Graphics Gems III, 1992), x, y, z, w.
static void RandomRotation(mvfxWords4 words, float q[4])
{
    float u = mvfxUnitFloat(words.x);
    float low = sqrtf(1.0f - u);
    float high = sqrtf(u);
    float s1 = 0.0f;
    float c1 = 0.0f;
    float s2 = 0.0f;
    float c2 = 0.0f;
    mvfxSinCos(TWO_PI * mvfxUnitFloat(words.y), &s1, &c1);
    mvfxSinCos(TWO_PI * mvfxUnitFloat(words.z), &s2, &c2);
    q[0] = low * s1;
    q[1] = low * c1;
    q[2] = high * s2;
    q[3] = high * c2;
}

// The quaternion of Euler angles: about X, then Y, then Z (qz qy qx).
static void EulerRotation(const float angles[3], float q[4])
{
    float s[3];
    float c[3];
    for (int32_t i = 0; i < 3; ++i)
    {
        mvfxSinCos(0.5f * angles[i], &s[i], &c[i]);
    }
    q[0] = s[0] * c[1] * c[2] - c[0] * s[1] * s[2];
    q[1] = c[0] * s[1] * c[2] + s[0] * c[1] * s[2];
    q[2] = c[0] * c[1] * s[2] - s[0] * s[1] * c[2];
    q[3] = c[0] * c[1] * c[2] + s[0] * s[1] * s[2];
}

// Stores count components of a vector at consecutive attributes.
static void StoreVector(mvfxInstance* instance, int32_t emitter, mvfxAttribute first, int32_t count,
                        int32_t at, const float* values)
{
    for (int32_t c = 0; c < count; ++c)
    {
        Store(instance, emitter, (mvfxAttribute)(first + c), at, values[c]);
    }
}

// A frame or mesh index a particle starts with: the first, or any from
// first to last alike by its own draw.
static uint32_t DrawIndex(const mvfxInstance* instance, int32_t emitter,
                          const mvfxFrameStart* start, uint32_t salt, uint64_t serial)
{
    uint32_t index = (uint32_t)start->first;
    if (start->mode == mvfx_frameRandom)
    {
        uint64_t span = (uint64_t)(start->last - start->first) + 1;
        uint32_t word = Draw(instance, emitter, salt, serial).x;
        index += (uint32_t)(((uint64_t)word * span) >> 32u);
    }
    return index;
}

// How far back a birth looks for its ribbon's point before.
#define RIBBON_LOOKBACK 256

// The youngest particle before at in ribbon id, at most RIBBON_LOOKBACK
// back; -1 for none.
static int32_t PreviousInRibbon(mvfxInstance* instance, int32_t emitter, int32_t at, uint32_t id)
{
    const uint32_t* ids = mvfxAttributeArray(instance, emitter, mvfx_attributeRibbon);
    int32_t last = at > RIBBON_LOOKBACK ? at - RIBBON_LOOKBACK : 0;
    for (int32_t j = at - 1; j >= last; --j)
    {
        if (ids[j] == id)
        {
            return j;
        }
    }
    return -1;
}

// The distance between particles a and b in their space.
static float GapBetween(mvfxInstance* instance, int32_t emitter, int32_t a, int32_t b)
{
    float square = 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        const float* p = mvfxAttributeArray(instance, emitter, mvfx_attributePositionX + k);
        float d = p[a] - p[b];
        square += d * d;
    }
    return sqrtf(square);
}

// The ribbon a birth at at joins: its batch's first serial; its event's
// particle's serial; else the counter, advanced first when the birth lies
// farther than the break distance from the ribbon's youngest point.
static uint32_t AssignRibbon(mvfxInstance* instance, int32_t emitter, const mvfxBirth* birth,
                             int32_t at)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    mvfxEmitterState* state = &instance->emitters[emitter];
    if (compiled->unitPoints > 1)
    {
        // A unit is a ribbon: its first point's serial.
        return (uint32_t)(birth->serial * (uint64_t)compiled->unitPoints);
    }
    if (compiled->ribbonIds == mvfx_ribbonPerBatch)
    {
        return (uint32_t)(birth->serial - (uint64_t)birth->batchIndex);
    }
    if (compiled->ribbonIds == mvfx_ribbonPerSource && birth->event1 > 0)
    {
        return state->eventQueue[birth->event1 - 1].source;
    }
    if (compiled->ribbonBreakDistance > 0.0f)
    {
        int32_t before = PreviousInRibbon(instance, emitter, at, state->ribbonCounter);
        if (before >= 0 &&
            GapBetween(instance, emitter, at, before) > compiled->ribbonBreakDistance)
        {
            state->ribbonCounter += 1;
        }
    }
    return state->ribbonCounter;
}

// A ribbon's distance at the particle at: its ribbon's youngest point
// before's distance and the gap to it, in the particles' space; 0 for the
// first (or one past RIBBON_LOOKBACK particles back).
static float RibbonDistanceOf(mvfxInstance* instance, int32_t emitter, int32_t at)
{
    const uint32_t* ids = mvfxAttributeArray(instance, emitter, mvfx_attributeRibbon);
    const float* distances = mvfxAttributeArray(instance, emitter, mvfx_attributeRibbonDistance);
    int32_t before = PreviousInRibbon(instance, emitter, at, ids[at]);
    return before >= 0 ? distances[before] + GapBetween(instance, emitter, at, before) : 0.0f;
}

// A birth's word attributes (its frame, mesh index, ribbon and a count of
// no collisions), and its ribbon's distance.
static void StoreWords(mvfxInstance* instance, int32_t emitter, const mvfxBirth* birth, int32_t at)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    if (Has(compiled, mvfx_attributeFrame))
    {
        StoreWord(instance, emitter, mvfx_attributeFrame, at,
                  DrawIndex(instance, emitter, &compiled->frame, FRAME_SALT, birth->serial));
    }
    if (Has(compiled, mvfx_attributeMesh))
    {
        StoreWord(instance, emitter, mvfx_attributeMesh, at,
                  DrawIndex(instance, emitter, &compiled->meshIndex, MESH_SALT, birth->serial));
    }
    if (Has(compiled, mvfx_attributeRibbon))
    {
        StoreWord(instance, emitter, mvfx_attributeRibbon, at,
                  AssignRibbon(instance, emitter, birth, at));
    }
    if (Has(compiled, mvfx_attributeCollisions))
    {
        StoreWord(instance, emitter, mvfx_attributeCollisions, at, 0);
    }
    if (Has(compiled, mvfx_attributeRibbonDistance))
    {
        Store(instance, emitter, mvfx_attributeRibbonDistance, at,
              RibbonDistanceOf(instance, emitter, at));
    }
}

// The optional values of a birth, each when the emitter stores it.
static void AppendValues(mvfxInstance* instance, int32_t emitter, const mvfxBirth* birth,
                         int32_t at)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    float age = mvfxSourceAge(compiled, birth->tick);
    const float (*parameters)[4] = instance->parameters;
    uint64_t serial = birth->serial;
    float values[4];
    if (Has(compiled, mvfx_attributeSizeX))
    {
        mvfxDrawVector(&compiled->size, age, parameters, Draw(instance, emitter, SIZE_SALT, serial),
                       values);
        StoreVector(instance, emitter, mvfx_attributeSizeX, 3, at, values);
    }
    if (Has(compiled, mvfx_attributeColorR))
    {
        mvfxDrawVector(&compiled->color, age, parameters,
                       Draw(instance, emitter, COLOR_SALT, serial), values);
        StoreVector(instance, emitter, mvfx_attributeColorR, 4, at, values);
    }
    if (Has(compiled, mvfx_attributeAngle))
    {
        Store(instance, emitter, mvfx_attributeAngle, at,
              mvfxDrawScalar(&compiled->angle, age, parameters,
                             Draw(instance, emitter, ANGLE_SALT, serial)));
        Store(instance, emitter, mvfx_attributeSpin, at,
              mvfxDrawScalar(&compiled->spin, age, parameters,
                             Draw(instance, emitter, SPIN_SALT, serial)));
    }
    if (Has(compiled, mvfx_attributeOrientationX))
    {
        float q[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        mvfxWords4 words = Draw(instance, emitter, ORIENTATION_SALT, serial);
        if (compiled->orientationMode == mvfx_orientationRandom)
        {
            RandomRotation(words, q);
        }
        else if (compiled->orientationMode == mvfx_orientationEuler)
        {
            mvfxDrawVector(&compiled->orientation, age, parameters, words, values);
            EulerRotation(values, q);
        }
        StoreVector(instance, emitter, mvfx_attributeOrientationX, 4, at, q);
        mvfxDrawVector(&compiled->angularVelocity, age, parameters,
                       Draw(instance, emitter, ANGULAR_SALT, serial), values);
        StoreVector(instance, emitter, mvfx_attributeAngularX, 3, at, values);
    }
    if (Has(compiled, mvfx_attributeDrag))
    {
        Store(instance, emitter, mvfx_attributeDrag, at,
              mvfxDrawScalar(&compiled->dragScale, age, parameters,
                             Draw(instance, emitter, DRAG_SALT, serial)));
    }
    for (int32_t k = 0; Has(compiled, mvfx_attributeSleep) && k < 4; ++k)
    {
        Store(instance, emitter, (mvfxAttribute)(mvfx_attributeSleep + k), at, 0.0f);
    }
    StoreWords(instance, emitter, birth, at);
    for (int32_t k = 0; Has(compiled, mvfx_attributeScenePointX) && k < 6; ++k)
    {
        // No contact plane yet.
        Store(instance, emitter, (mvfxAttribute)(mvfx_attributeScenePointX + k), at, 0.0f);
    }
    for (int32_t k = 0; Has(compiled, mvfx_attributeNeighborCount) && k < 7; ++k)
    {
        // No neighbors until the next step's query.
        Store(instance, emitter, (mvfxAttribute)(mvfx_attributeNeighborCount + k), at, 0.0f);
    }
    for (int32_t i = 0; i < compiled->customCount; ++i)
    {
        mvfxDrawVector(&compiled->custom[i], age, parameters,
                       Draw(instance, emitter, CUSTOM_SALT + (uint32_t)i, serial), values);
        StoreVector(instance, emitter, (mvfxAttribute)(mvfx_attributeCustom + 4 * i),
                    compiled->custom[i].components, at, values);
    }
}

// A world-space birth: its place in the instance's space turned, scaled
// and moved by the instance's transform at its tick, then made an offset
// from the emitter's anchor in binary64; its velocity turned and scaled,
// plus the share of the instance's velocity it inherits.
static void PlaceInWorld(const mvfxInstance* instance, int32_t emitter, int64_t tick,
                         float position[3], float velocity[3])
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    mvfxTransform transform;
    mvfxTransformAt(instance, tick, &transform);
    float scaled[3] = {position[0] * transform.scale, position[1] * transform.scale,
                       position[2] * transform.scale};
    float turned[3];
    mvfxRotate(transform.rotation, scaled, turned);
    const double* anchor = instance->emitters[emitter].anchor;
    for (int32_t k = 0; k < 3; ++k)
    {
        position[k] = (float)((transform.origin[k] - anchor[k]) + (double)turned[k]);
        scaled[k] = velocity[k] * transform.scale;
    }
    mvfxRotate(transform.rotation, scaled, velocity);
    for (int32_t k = 0; k < 3; ++k)
    {
        velocity[k] += compiled->inheritVelocity * instance->velocity[k];
    }
}

// The given values of a host's queued particle or of an event's birth;
// NULL for a birth of the emitter's own.
static const mvfxHostBirth* GivenOf(const mvfxInstance* instance, int32_t emitter,
                                    const mvfxBirth* birth)
{
    const mvfxEmitterState* state = &instance->emitters[emitter];
    return birth->host >= 0    ? &state->hostQueue[birth->host]
           : birth->event1 > 0 ? &state->eventQueue[birth->event1 - 1]
                               : nullptr;
}

// A host's queued particle: its place as given, made an offset from the
// anchor in world space; its velocity as given, or drawn in the instance's
// space at the place (its direction from the instance's origin standing
// for the shape's normal and center) and turned, scaled and given the
// inherited share as a shape's birth is.
static void PlaceHostBirth(const mvfxInstance* instance, int32_t emitter, const mvfxBirth* birth,
                           float position[3], float velocity[3])
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    const mvfxEmitterState* state = &instance->emitters[emitter];
    const mvfxHostBirth* host = GivenOf(instance, emitter, birth);
    bool world = compiled->space == mvfx_spaceWorld;
    mvfxTransform transform;
    mvfxTransformAt(instance, birth->tick, &transform);
    mvfxShapeSample sample = {0};
    float relative[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        position[k] = (float)(world ? host->position[k] - state->anchor[k] : host->position[k]);
        relative[k] =
            world ? (float)((host->position[k] - transform.origin[k]) / (double)transform.scale)
                  : position[k];
    }
    // Into the instance's space: the conjugate turn undoes the rotation.
    const float* q = transform.rotation;
    const float inverse[4] = {-q[0], -q[1], -q[2], q[3]};
    if (world)
    {
        mvfxRotate(inverse, relative, sample.local);
    }
    else
    {
        sample.local[0] = relative[0];
        sample.local[1] = relative[1];
        sample.local[2] = relative[2];
    }
    if ((host->given & mvfx_givenVelocity) != 0)
    {
        velocity[0] = host->velocity[0];
        velocity[1] = host->velocity[1];
        velocity[2] = host->velocity[2];
        return;
    }
    mvfxWords4 words = Draw(instance, emitter, VELOCITY_SALT, birth->serial);
    sample.normal[0] = sample.local[0];
    sample.normal[1] = sample.local[1];
    sample.normal[2] = sample.local[2];
    if (!mvfxNormalize(sample.normal))
    {
        // Any way, from the shape's draw as a point shape's normal is, apart
        // from the velocity's own.
        mvfxWords4 shapeWords = Draw(instance, emitter, SHAPE_SALT, birth->serial);
        mvfxRandomDirection(shapeWords.x, shapeWords.y, sample.normal);
    }
    mvfxDrawVelocity(&compiled->velocity, &sample, words, velocity);
    if (world)
    {
        float scaled[3] = {velocity[0] * transform.scale, velocity[1] * transform.scale,
                           velocity[2] * transform.scale};
        mvfxRotate(transform.rotation, scaled, velocity);
        for (int32_t k = 0; k < 3; ++k)
        {
            velocity[k] += compiled->inheritVelocity * instance->velocity[k];
        }
    }
    for (int32_t k = 0; (host->given & mvfx_givenAddedVelocity) != 0 && k < 3; ++k)
    {
        velocity[k] += host->velocity[k];
    }
}

// The step from one point of a unit to the next along a direction at
// birth, in the particles' space: along it by the spacing, turned and
// scaled with the instance for a world-space emitter.
static void UnitStep(const mvfxInstance* instance, int32_t emitter, int64_t tick,
                     const float direction[3], float out[3])
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    float step[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        step[k] = direction[k] * compiled->unit.spacing;
        out[k] = step[k];
    }
    if (compiled->space == mvfx_spaceWorld)
    {
        mvfxTransform transform;
        mvfxTransformAt(instance, tick, &transform);
        for (int32_t k = 0; k < 3; ++k)
        {
            step[k] *= transform.scale;
        }
        mvfxRotate(transform.rotation, step, out);
    }
}

// Appends point j of a birth at a place with a velocity.
static void AppendPoint(mvfxInstance* instance, int32_t emitter, const mvfxBirth* birth,
                        float inverseLife, int32_t j, const float position[3],
                        const float velocity[3])
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    int32_t at = instance->emitters[emitter].count;
    const mvfxHostBirth* given = GivenOf(instance, emitter, birth);
    StoreVector(instance, emitter, mvfx_attributePositionX, 3, at, position);
    StoreVector(instance, emitter, mvfx_attributeVelocityX, 3, at, velocity);
    if (Has(compiled, mvfx_attributeAnchorX))
    {
        StoreVector(instance, emitter, mvfx_attributeAnchorX, 3, at, position);
    }
    StoreWord(instance, emitter, mvfx_attributeBirthLow, at,
              (uint32_t)((uint64_t)birth->tick & 0xFFFFFFFFu));
    StoreWord(instance, emitter, mvfx_attributeBirthHigh, at,
              (uint32_t)((uint64_t)birth->tick >> 32u));
    Store(instance, emitter, mvfx_attributeInverseLife, at, inverseLife);
    StoreWord(instance, emitter, mvfx_attributeSerial, at,
              (uint32_t)(birth->serial * (uint64_t)compiled->unitPoints + (uint64_t)j));
    AppendValues(instance, emitter, birth, at);
    if (given != nullptr)
    {
        const mvfxHostBirth* host = given;
        if ((host->given & mvfx_givenColor) != 0)
        {
            StoreVector(instance, emitter, mvfx_attributeColorR, 4, at, host->color);
        }
        if ((host->given & mvfx_givenSize) != 0)
        {
            StoreVector(instance, emitter, mvfx_attributeSizeX, 3, at, host->size);
        }
    }
    if (Has(compiled, mvfx_attributeTorn))
    {
        StoreWord(instance, emitter, mvfx_attributeTorn, at, 0u);
    }
    if (j == 0 && Has(compiled, mvfx_attributeAttachFlags))
    {
        mvfxMarkNewUnit(instance, emitter, at, given, birth->event1 > 0);
    }
    instance->emitters[emitter].count = at + 1;
    mvfxRecordEvent(instance, emitter,
                    &(mvfxEventFact){.kind = mvfx_eventBirth,
                                     .particle = at,
                                     .tick = birth->tick,
                                     .placeTick = birth->tick});
    if (compiled->rigid.enabled)
    {
        mvfxIntegrateRigidNewborn(instance, emitter, at, birth->tick);
    }
    else
    {
        mvfxIntegrateNewborn(instance, emitter, at, birth->tick);
    }
    if (mvfxIsPinnedPoint(compiled, j))
    {
        // A pinned point stays where it was born.
        static const float still[3] = {0.0f, 0.0f, 0.0f};
        StoreVector(instance, emitter, mvfx_attributePositionX, 3, at, position);
        StoreVector(instance, emitter, mvfx_attributeVelocityX, 3, at, still);
    }
}

void mvfxAppendBirth(mvfxInstance* instance, int32_t emitter, const mvfxBirth* birth,
                     float inverseLife)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    float position[3];
    float velocity[3];
    const mvfxHostBirth* given = GivenOf(instance, emitter, birth);
    if (given != nullptr)
    {
        PlaceHostBirth(instance, emitter, birth, position, velocity);
    }
    else
    {
        mvfxWords4 shapeWords = Draw(instance, emitter, SHAPE_SALT, birth->serial);
        double age = (double)(birth->tick - compiled->delay) / (double)MVFX_TICKS_PER_SECOND;
        float arc = mvfxArcFraction(&compiled->shape, shapeWords, age, birth->batchIndex,
                                    birth->batchCount);
        mvfxShapeSample sample;
        mvfxSampleShape(&compiled->shape, instance->emitters[emitter].mesh, shapeWords, arc,
                        &sample);
        mvfxDrawVelocity(&compiled->velocity, &sample,
                         Draw(instance, emitter, VELOCITY_SALT, birth->serial), velocity);
        for (int32_t k = 0; k < 3; ++k)
        {
            position[k] = sample.position[k];
        }
        if (compiled->space == mvfx_spaceWorld)
        {
            PlaceInWorld(instance, emitter, birth->tick, position, velocity);
        }
    }
    float across[3] = {0.0f, 0.0f, 0.0f};
    float down[3] = {0.0f, 0.0f, 0.0f};
    if (compiled->unitPoints > 1)
    {
        UnitStep(instance, emitter, birth->tick, compiled->unit.direction, across);
        UnitStep(instance, emitter, birth->tick, compiled->unit.downDirection, down);
    }
    // A unit's points from its root, across each row, row after row.
    for (int32_t j = 0; j < compiled->unitPoints; ++j)
    {
        int32_t row = j / compiled->unitWidth;
        float c = (float)(j % compiled->unitWidth);
        float r = (float)row;
        float place[3];
        for (int32_t k = 0; k < 3; ++k)
        {
            place[k] = position[k] + across[k] * c + down[k] * r;
        }
        AppendPoint(instance, emitter, birth, inverseLife, j, place, velocity);
    }
}
