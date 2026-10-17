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

// What a particle's colour or size reads: a stored vector's arrays (NULL
// where the emitter stores none), the velocity's for a value over life
// taken at the speed, and the values over life; resolved once a call or a
// run.
typedef struct Look
{
    const mvfxInstance* instance;
    int32_t emitter;
    const mvfxCompiledOverLife* over;
    const float* stored[4];
    const float* velocity[3];
    float speedScale;
} Look;

static Look LookOf(const mvfxInstance* instance, int32_t emitter, mvfxAttribute first, int32_t n)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    Look look = {.instance = instance,
                 .emitter = emitter,
                 .over = &compiled->overLife,
                 .speedScale = compiled->space == mvfx_spaceWorld ? instance->to.scale : 1.0f};
    bool stored = Has(compiled, first);
    for (int32_t k = 0; k < n; ++k)
    {
        look.stored[k] = stored ? Floats(instance, emitter, (mvfxAttribute)(first + k)) : nullptr;
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        look.velocity[k] = Floats(instance, emitter, (mvfxAttribute)(mvfx_attributeVelocityX + k));
    }
    return look;
}

// Particle i's normalized age: the given one, or taken now.
static float AgeAt(const Look* look, int32_t i, const float* age)
{
    return age != nullptr ? *age : mvfxNormalizedAge(look->instance, look->emitter, i);
}

// The input an over-life value is taken at: the age, or the speed's share
// of its range, the speed in the emitter's units.
static inline float InputOf(const Look* look, int32_t i, const float* age, mvfxOverLifeInput input,
                            const float range[2])
{
    if (input == mvfx_overAge)
    {
        return AgeAt(look, i, age);
    }
    float v[3] = {look->velocity[0][i], look->velocity[1][i], look->velocity[2][i]};
    float speed = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (look->speedScale != 1.0f)
    {
        speed /= look->speedScale;
    }
    float share = (speed - range[0]) / (range[1] - range[0]);
    return fminf(fmaxf(share, 0.0f), 1.0f);
}

// A stored vector of n components, or ones where the emitter stores none.
static void Stored(const Look* look, int32_t i, int32_t n, float out[])
{
    for (int32_t k = 0; k < n; ++k)
    {
        out[k] = look->stored[k] != nullptr ? look->stored[k][i] : 1.0f;
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

// Particle i's colour, at the given normalized age where one is given.
static inline void ColorAt(const Look* look, int32_t i, const float* age, float out[4])
{
    Stored(look, i, 4, out);
    const mvfxCompiledOverLife* over = look->over;
    if (!over->color)
    {
        return;
    }
    if (over->gradient != nullptr)
    {
        float t = InputOf(look, i, age, over->colorInput, over->colorRange);
        float factor[4];
        mvfxGradientTableAt(over->gradient, t, factor);
        for (int32_t k = 0; k < 4; ++k)
        {
            out[k] *= factor[k];
        }
    }
    if (over->scaleAlpha)
    {
        out[3] *= mvfxLifeAt(&over->alpha, AgeAt(look, i, age));
    }
}

// Particle i's size, at the given normalized age where one is given.
static inline void SizeAt(const Look* look, int32_t i, const float* age, float out[3])
{
    Stored(look, i, 3, out);
    const mvfxCompiledOverLife* over = look->over;
    if (!over->size)
    {
        return;
    }
    float t = InputOf(look, i, age, over->sizeInput, over->sizeRange);
    // One curve's sample serves all three axes unless they have their own.
    float scale = mvfxLifeAt(&over->sizes[0], t);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] *= over->perAxis ? mvfxLifeAt(&over->sizes[k], t) : scale;
    }
}

void mvfxColorsOf(const mvfxInstance* instance, int32_t emitter, const int32_t* run, int32_t n,
                  const float ages[], float out[][4])
{
    Look look = LookOf(instance, emitter, mvfx_attributeColorR, 4);
    for (int32_t j = 0; j < n; ++j)
    {
        ColorAt(&look, run[j], ages != nullptr ? &ages[j] : nullptr, out[j]);
    }
}

void mvfxSizesOf(const mvfxInstance* instance, int32_t emitter, const int32_t* run, int32_t n,
                 const float ages[], float out[][4])
{
    Look look = LookOf(instance, emitter, mvfx_attributeSizeX, 3);
    for (int32_t j = 0; j < n; ++j)
    {
        SizeAt(&look, run[j], ages != nullptr ? &ages[j] : nullptr, out[j]);
    }
}

void mvfxColorOf(const mvfxInstance* instance, int32_t emitter, int32_t i, float out[4])
{
    Look look = LookOf(instance, emitter, mvfx_attributeColorR, 4);
    ColorAt(&look, i, nullptr, out);
}

void mvfxSizeOf(const mvfxInstance* instance, int32_t emitter, int32_t i, float out[3])
{
    Look look = LookOf(instance, emitter, mvfx_attributeSizeX, 3);
    SizeAt(&look, i, nullptr, out);
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
