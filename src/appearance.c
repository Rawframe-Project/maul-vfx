// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "appearance.h"

#include <math.h>

static bool Has(const mvfxCompiledEmitter* emitter, mvfxAttribute attribute)
{
    return mvfxHasAttribute(&emitter->plan, attribute);
}

static const float* Floats(const mvfxInstance* instance, int32_t emitter, mvfxAttribute attribute)
{
    return mvfxAttributeArray((mvfxInstance*)instance, emitter, attribute);
}

// Particle i's age in seconds at the instance's clock.
static double SecondsOf(const mvfxInstance* instance, int32_t emitter, int32_t i)
{
    const uint32_t* low =
        mvfxAttributeArray((mvfxInstance*)instance, emitter, mvfx_attributeBirthLow);
    const uint32_t* high =
        mvfxAttributeArray((mvfxInstance*)instance, emitter, mvfx_attributeBirthHigh);
    int64_t birth = (int64_t)(((uint64_t)high[i] << 32u) | low[i]);
    return (double)(instance->clock - birth) / (double)MVFX_TICKS_PER_SECOND;
}

float mvfxNormalizedAge(const mvfxInstance* instance, int32_t emitter, int32_t i)
{
    float inverse = Floats(instance, emitter, mvfx_attributeInverseLife)[i];
    float age = (float)SecondsOf(instance, emitter, i) * inverse;
    return fminf(fmaxf(age, 0.0f), 1.0f);
}

// The input an over-life value is taken at: the age, or the speed's share
// of its range, the speed in the emitter's units.
static float InputOf(const mvfxInstance* instance, int32_t emitter, int32_t i,
                     mvfxOverLifeInput input, const float range[2])
{
    if (input == mvfx_overAge)
    {
        return mvfxNormalizedAge(instance, emitter, i);
    }
    float v[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        v[k] = Floats(instance, emitter, (mvfxAttribute)(mvfx_attributeVelocityX + k))[i];
    }
    float speed = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (instance->effect->emitters[emitter].space == mvfx_spaceWorld)
    {
        speed /= instance->to.scale;
    }
    float share = (speed - range[0]) / (range[1] - range[0]);
    return fminf(fmaxf(share, 0.0f), 1.0f);
}

// A stored vector of n components, or ones when the emitter does not store
// it.
static void Stored(const mvfxInstance* instance, int32_t emitter, int32_t i, mvfxAttribute first,
                   int32_t n, float out[])
{
    bool stored = Has(&instance->effect->emitters[emitter], first);
    for (int32_t k = 0; k < n; ++k)
    {
        out[k] = stored ? Floats(instance, emitter, (mvfxAttribute)(first + k))[i] : 1.0f;
    }
}

void mvfxColorOf(const mvfxInstance* instance, int32_t emitter, int32_t i, float out[4])
{
    Stored(instance, emitter, i, mvfx_attributeColorR, 4, out);
    const mvfxCompiledOverLife* over = &instance->effect->emitters[emitter].overLife;
    if (!over->color)
    {
        return;
    }
    if (over->gradient != nullptr)
    {
        float t = InputOf(instance, emitter, i, over->colorInput, over->colorRange);
        float factor[4];
        mvfxGradientTableAt(over->gradient, t, factor);
        for (int32_t k = 0; k < 4; ++k)
        {
            out[k] *= factor[k];
        }
    }
    if (over->scaleAlpha)
    {
        out[3] *= mvfxLifeAt(&over->alpha, mvfxNormalizedAge(instance, emitter, i));
    }
}

void mvfxSizeOf(const mvfxInstance* instance, int32_t emitter, int32_t i, float out[3])
{
    Stored(instance, emitter, i, mvfx_attributeSizeX, 3, out);
    const mvfxCompiledOverLife* over = &instance->effect->emitters[emitter].overLife;
    if (!over->size)
    {
        return;
    }
    float t = InputOf(instance, emitter, i, over->sizeInput, over->sizeRange);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] *= mvfxLifeAt(&over->sizes[over->perAxis ? k : 0], t);
    }
}

uint32_t mvfxFrameOf(const mvfxInstance* instance, int32_t emitter, int32_t i)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    uint32_t start = 0;
    if (Has(compiled, mvfx_attributeFrame))
    {
        start = ((const uint32_t*)mvfxAttributeArray((mvfxInstance*)instance, emitter,
                                                     mvfx_attributeFrame))[i];
    }
    const mvfxFrameOverLife* frame = &compiled->overLife.frame;
    double moved = 0.0;
    if (frame->animation == mvfx_frameOverLife)
    {
        moved = (double)mvfxNormalizedAge(instance, emitter, i) * (double)frame->cycles *
                (double)frame->frameCount;
    }
    else if (frame->animation == mvfx_frameAtRate)
    {
        moved = SecondsOf(instance, emitter, i) * (double)frame->frameRate;
    }
    else
    {
        return start;
    }
    // At most 1,000 cycles of 65,536 frames or 10,000 a second of a life
    // of at most MVFX_MAX_LIFETIME seconds: within an int64_t.
    int64_t count = frame->frameCount;
    int64_t at = (int64_t)start + (int64_t)floor(fmax(moved, 0.0));
    return (uint32_t)(frame->loop ? at % count : (at < count ? at : count - 1));
}

void mvfxCustomOf(const mvfxInstance* instance, int32_t emitter, int32_t i, int32_t c, float out[4])
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    int32_t n = compiled->custom[c].components;
    float scale = 1.0f;
    if (compiled->overLife.customScaled[c])
    {
        scale = mvfxLifeAt(&compiled->overLife.custom[c], mvfxNormalizedAge(instance, emitter, i));
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        out[k] = k < n ? scale * Floats(instance, emitter,
                                        (mvfxAttribute)(mvfx_attributeCustom + 4 * c + k))[i]
                       : 0.0f;
    }
}
