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

// The inputs over-life values are taken at for a run of particles of the
// given ages: the age, or the speed's share of its range, the speed in the
// emitter's units.
static void InputsOf(const mvfxInstance* instance, int32_t emitter, const int32_t* run, int32_t n,
                     const float ages[], mvfxOverLifeInput input, const float range[2], float out[])
{
    if (input == mvfx_overAge)
    {
        for (int32_t j = 0; j < n; ++j)
        {
            out[j] = ages[j];
        }
        return;
    }
    const float* vx = Floats(instance, emitter, mvfx_attributeVelocityX);
    const float* vy = Floats(instance, emitter, mvfx_attributeVelocityY);
    const float* vz = Floats(instance, emitter, mvfx_attributeVelocityZ);
    bool world = instance->effect->emitters[emitter].space == mvfx_spaceWorld;
    for (int32_t j = 0; j < n; ++j)
    {
        float v[3] = {vx[run[j]], vy[run[j]], vz[run[j]]};
        float speed = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (world)
        {
            speed /= instance->to.scale;
        }
        float share = (speed - range[0]) / (range[1] - range[0]);
        out[j] = fminf(fmaxf(share, 0.0f), 1.0f);
    }
}

// A run's stored vectors of n components, or ones when the emitter does
// not store them.
static void Stored(const mvfxInstance* instance, int32_t emitter, const int32_t* run, int32_t n,
                   mvfxAttribute first, int32_t components, float out[][4])
{
    bool stored = Has(&instance->effect->emitters[emitter], first);
    for (int32_t k = 0; k < components; ++k)
    {
        const float* values =
            stored ? Floats(instance, emitter, (mvfxAttribute)(first + k)) : nullptr;
        for (int32_t j = 0; j < n; ++j)
        {
            out[j][k] = values != nullptr ? values[run[j]] : 1.0f;
        }
    }
}

float mvfxLargestStored(const mvfxInstance* instance, int32_t emitter, mvfxAttribute attribute)
{
    if (!Has(&instance->effect->emitters[emitter], attribute))
    {
        return 1.0f;
    }
    const float* values = Floats(instance, emitter, attribute);
    float largest = 0.0f;
    for (int32_t i = 0; i < instance->emitters[emitter].count; ++i)
    {
        float size = fabsf(values[i]);
        largest = size > largest ? size : largest;
    }
    return largest;
}

void mvfxNormalizedAges(const mvfxInstance* instance, int32_t emitter, const int32_t* run,
                        int32_t n, float out[])
{
    for (int32_t j = 0; j < n; ++j)
    {
        out[j] = mvfxNormalizedAge(instance, emitter, run[j]);
    }
}

void mvfxColorsOf(const mvfxInstance* instance, int32_t emitter, const int32_t* run, int32_t n,
                  const float ages[], float out[][4])
{
    Stored(instance, emitter, run, n, mvfx_attributeColorR, 4, out);
    const mvfxCompiledOverLife* over = &instance->effect->emitters[emitter].overLife;
    if (!over->color)
    {
        return;
    }
    if (over->gradient != nullptr)
    {
        float t[MVFX_APPEARANCE_RUN];
        InputsOf(instance, emitter, run, n, ages, over->colorInput, over->colorRange, t);
        for (int32_t j = 0; j < n; ++j)
        {
            float factor[4];
            mvfxGradientTableAt(over->gradient, t[j], factor);
            for (int32_t k = 0; k < 4; ++k)
            {
                out[j][k] *= factor[k];
            }
        }
    }
    for (int32_t j = 0; over->scaleAlpha && j < n; ++j)
    {
        out[j][3] *= mvfxLifeAt(&over->alpha, ages[j]);
    }
}

void mvfxSizesOf(const mvfxInstance* instance, int32_t emitter, const int32_t* run, int32_t n,
                 const float ages[], float out[][4])
{
    Stored(instance, emitter, run, n, mvfx_attributeSizeX, 3, out);
    const mvfxCompiledOverLife* over = &instance->effect->emitters[emitter].overLife;
    if (!over->size)
    {
        return;
    }
    float t[MVFX_APPEARANCE_RUN];
    InputsOf(instance, emitter, run, n, ages, over->sizeInput, over->sizeRange, t);
    for (int32_t j = 0; j < n; ++j)
    {
        // One curve's sample serves all three axes unless they have their
        // own.
        float scale = mvfxLifeAt(&over->sizes[0], t[j]);
        for (int32_t k = 0; k < 3; ++k)
        {
            out[j][k] *= over->perAxis ? mvfxLifeAt(&over->sizes[k], t[j]) : scale;
        }
    }
}

void mvfxColorOf(const mvfxInstance* instance, int32_t emitter, int32_t i, float out[4])
{
    float run[1][4];
    float age = mvfxNormalizedAge(instance, emitter, i);
    mvfxColorsOf(instance, emitter, &i, 1, &age, run);
    for (int32_t k = 0; k < 4; ++k)
    {
        out[k] = run[0][k];
    }
}

void mvfxSizeOf(const mvfxInstance* instance, int32_t emitter, int32_t i, float out[3])
{
    float run[1][4];
    float age = mvfxNormalizedAge(instance, emitter, i);
    mvfxSizesOf(instance, emitter, &i, 1, &age, run);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = run[0][k];
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
