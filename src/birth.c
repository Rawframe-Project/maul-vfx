// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Initial values written into an emitter's arrays.

#include "birth.h"

#include "random.h"
#include "shape.h"
#include "velocity.h"

// The salts that set each purpose's draws apart.
#define LIFE_SALT     0x4C494645u
#define SHAPE_SALT    0x53484150u
#define VELOCITY_SALT 0x56454C4Fu

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

float mvfxInverseLifeOf(const mvfxInstance* instance, int32_t emitter, uint64_t serial)
{
    const mvfxRange* range = &instance->effect->emitters[emitter].lifetime;
    mvfxWords4 words = Draw(instance, emitter, LIFE_SALT, serial);
    float life = range->min + (range->max - range->min) * mvfxUnitFloat(words.x);
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
    instance->emitters[emitter].count = at + 1;
}
