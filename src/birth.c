// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Initial values written into an emitter's arrays.

#include "birth.h"

#include "elementary.h"
#include "random.h"
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
    return (emitter->plan.present & ((uint64_t)1 << attribute)) != 0;
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
    if (Has(compiled, mvfx_attributeFrame))
    {
        uint32_t frame = (uint32_t)compiled->frame.first;
        if (compiled->frame.mode == mvfx_frameRandom)
        {
            uint64_t span = (uint64_t)(compiled->frame.last - compiled->frame.first) + 1;
            uint32_t word = Draw(instance, emitter, FRAME_SALT, serial).x;
            frame += (uint32_t)(((uint64_t)word * span) >> 32u);
        }
        StoreWord(instance, emitter, mvfx_attributeFrame, at, frame);
    }
    for (int32_t i = 0; i < compiled->customCount; ++i)
    {
        mvfxDrawVector(&compiled->custom[i], age, parameters,
                       Draw(instance, emitter, CUSTOM_SALT + (uint32_t)i, serial), values);
        StoreVector(instance, emitter, (mvfxAttribute)(mvfx_attributeCustom + 4 * i),
                    compiled->custom[i].components, at, values);
    }
}

void mvfxAppendBirth(mvfxInstance* instance, int32_t emitter, const mvfxBirth* birth,
                     float inverseLife)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    int32_t at = instance->emitters[emitter].count;
    mvfxWords4 shapeWords = Draw(instance, emitter, SHAPE_SALT, birth->serial);
    double age = (double)(birth->tick - compiled->delay) / (double)MVFX_TICKS_PER_SECOND;
    float arc =
        mvfxArcFraction(&compiled->shape, shapeWords, age, birth->batchIndex, birth->batchCount);
    mvfxShapeSample sample;
    mvfxSampleShape(&compiled->shape, instance->emitters[emitter].mesh, shapeWords, arc, &sample);
    float velocity[3];
    mvfxDrawVelocity(&compiled->velocity, &sample,
                     Draw(instance, emitter, VELOCITY_SALT, birth->serial), velocity);
    Store(instance, emitter, mvfx_attributePositionX, at, sample.position[0]);
    Store(instance, emitter, mvfx_attributePositionY, at, sample.position[1]);
    Store(instance, emitter, mvfx_attributePositionZ, at, sample.position[2]);
    Store(instance, emitter, mvfx_attributeVelocityX, at, velocity[0]);
    Store(instance, emitter, mvfx_attributeVelocityY, at, velocity[1]);
    Store(instance, emitter, mvfx_attributeVelocityZ, at, velocity[2]);
    StoreWord(instance, emitter, mvfx_attributeBirthLow, at,
              (uint32_t)((uint64_t)birth->tick & 0xFFFFFFFFu));
    StoreWord(instance, emitter, mvfx_attributeBirthHigh, at,
              (uint32_t)((uint64_t)birth->tick >> 32u));
    Store(instance, emitter, mvfx_attributeInverseLife, at, inverseLife);
    StoreWord(instance, emitter, mvfx_attributeSerial, at, (uint32_t)birth->serial);
    AppendValues(instance, emitter, birth, at);
    instance->emitters[emitter].count = at + 1;
}
