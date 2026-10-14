// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Effect defs checked and compiled: every field against its range, then
// each emitter's times in ticks, its rate curve baked with its integral,
// its bursts, and its attribute plan laid out.

#include "effect.h"

#include "allocator.h"

mvfxEmitterDef mvfxDefaultEmitterDef(void)
{
    return (mvfxEmitterDef){
        .cookie = MVFX_EMITTER_DEF_COOKIE,
        .capacity = 1024,
        .rate = 64.0f,
        .lifetime = {1.0f, 1.0f},
        .shape =
            {
                .kind = mvfx_shapePoint,
                .rotation = {0.0f, 0.0f, 0.0f, 1.0f},
                .arc = 6.28318530717958647692f,
            },
        .velocity = {.mode = mvfx_directionShapeNormal, .direction = {0.0f, 0.0f, 1.0f}},
    };
}

mvfxEffectDef mvfxDefaultEffectDef(void)
{
    return (mvfxEffectDef){.cookie = MVFX_EFFECT_DEF_COOKIE};
}

// Whether low <= x <= high; false for NaN.
static bool IsWithin(float x, float low, float high)
{
    return x >= low && x <= high;
}

// Bakes a rate curve over a loop's normalized age, its samples times rate.
static void BakeRateCurve(const mvfxCurve* curve, float rate, mvfxRateCurve* out)
{
    mvfxBakeCurve(curve, 0.0f, 1.0f, &out->table);
    for (int32_t i = 0; i < MVFX_CURVE_SAMPLES; ++i)
    {
        out->table.samples[i] *= rate;
    }
    mvfxIntegrateCurve(&out->table, 1.0, &out->integral);
}

// Whether a rate curve is valid for an emitter of this rate and duration:
// no keys, or keys over [0, 1] that bake to values never below 0 and never
// past MVFX_MAX_SPAWN_RATE once times rate.
static bool IsRateCurveValid(const mvfxEmitterDef* def)
{
    if (def->rateCurve.keyCount == 0)
    {
        return true;
    }
    if (def->duration <= 0.0f || !mvfxIsCurveValid(&def->rateCurve, true))
    {
        return false;
    }
    mvfxRateCurve baked;
    BakeRateCurve(&def->rateCurve, 1.0f, &baked);
    if (!mvfxIsCurveTableNonNegative(&baked.table))
    {
        return false;
    }
    for (int32_t i = 0; i < MVFX_CURVE_SAMPLES; ++i)
    {
        if (!((double)baked.table.samples[i] * (double)def->rate <= (double)MVFX_MAX_SPAWN_RATE))
        {
            return false;
        }
    }
    return true;
}

static bool IsBurstValid(const mvfxBurst* burst)
{
    return IsWithin(burst->time, 0.0f, MVFX_MAX_LIFETIME) && burst->minCount >= 0 &&
           burst->minCount <= burst->maxCount && burst->maxCount <= MVFX_MAX_EMITTER_CAPACITY &&
           burst->cycles >= 0 &&
           (burst->cycles == 1 ||
            IsWithin(burst->interval, MVFX_MIN_DURATION, MVFX_MAX_LIFETIME)) &&
           IsWithin(burst->probability, 0.0f, 1.0f);
}

// The most particles a second an emitter's bursts may spawn when they
// repeat: a burst firing more than once a loop at its interval, and one
// firing once a loop at each loop of a repeating emitter.
static double BurstRate(const mvfxEmitterDef* def)
{
    bool repeatsLoops = def->duration > 0.0f && def->loops != 1;
    double total = 0.0;
    for (int32_t b = 0; b < def->burstCount; ++b)
    {
        const mvfxBurst* burst = &def->bursts[b];
        double firings = repeatsLoops ? 1.0 / (double)def->duration : 0.0;
        if (burst->cycles != 1)
        {
            firings += 1.0 / (double)burst->interval;
        }
        total += (double)burst->maxCount * firings;
    }
    return total;
}

static bool IsEmitterDefValid(const mvfxEmitterDef* def)
{
    if (def->cookie != MVFX_EMITTER_DEF_COOKIE || def->capacity < 1 ||
        def->capacity > MVFX_MAX_EMITTER_CAPACITY ||
        !IsWithin(def->rate, 0.0f, MVFX_MAX_SPAWN_RATE) ||
        !IsWithin(def->lifetime.max, 0.0f, MVFX_MAX_LIFETIME) || !(def->lifetime.min > 0.0f) ||
        !(def->lifetime.min <= def->lifetime.max) ||
        !IsWithin(def->startDelay, 0.0f, MVFX_MAX_LIFETIME) ||
        !(def->duration == 0.0f || IsWithin(def->duration, MVFX_MIN_DURATION, MVFX_MAX_LIFETIME)) ||
        def->loops < 0 || def->burstCount < 0 || def->burstCount > MVFX_MAX_BURSTS ||
        (def->burstCount > 0 && def->bursts == nullptr) || def->rateCurve.keyCount < 0)
    {
        return false;
    }
    for (int32_t b = 0; b < def->burstCount; ++b)
    {
        if (!IsBurstValid(&def->bursts[b]))
        {
            return false;
        }
    }
    return BurstRate(def) <= (double)MVFX_MAX_SPAWN_RATE && IsRateCurveValid(def) &&
           mvfxIsShapeValid(&def->shape) && mvfxIsVelocityValid(&def->velocity);
}

bool mvfxIsEffectDefValid(const mvfxEffectDef* def)
{
    if (def == nullptr || def->cookie != MVFX_EFFECT_DEF_COOKIE || def->emitters == nullptr ||
        def->emitterCount < 1 || def->emitterCount > MVFX_MAX_EMITTERS)
    {
        return false;
    }
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        if (!IsEmitterDefValid(&def->emitters[i]))
        {
            return false;
        }
    }
    return true;
}

// The plan of the attributes every emitter has: position, birth tick,
// inverse life, serial and velocity, each padded and aligned.
static mvfxAttributePlan PlanAttributes(int32_t paddedCapacity)
{
    mvfxAttributePlan plan = {0};
    mvfxLayout layout = {0};
    for (mvfxAttribute a = 0; a < MVFX_ATTRIBUTE_COUNT; ++a)
    {
        plan.present |= 1u << a;
        plan.offsets[a] =
            mvfxLayoutAdd(&layout, (size_t)paddedCapacity, sizeof(float), MVFX_ATTRIBUTE_ALIGNMENT);
    }
    plan.blockBytes = layout.size;
    return plan;
}

static mvfxCompiledBurst CompileBurst(const mvfxBurst* burst)
{
    return (mvfxCompiledBurst){
        .time = mvfxSecondsToTicks((double)burst->time),
        .interval = burst->cycles == 1 ? 0 : mvfxSecondsToTicks((double)burst->interval),
        .minCount = burst->minCount,
        .maxCount = burst->maxCount,
        .cycles = burst->cycles,
        .probability = burst->probability,
    };
}

mvfxEffect* mvfxCompileEffect(const mvfxEffectDef* def, const mvfxAllocator* allocator)
{
    int32_t curveCount = 0;
    int32_t burstCount = 0;
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        curveCount += def->emitters[i].rateCurve.keyCount > 0 ? 1 : 0;
        burstCount += def->emitters[i].burstCount;
    }
    mvfxLayout layout = {0};
    (void)mvfxLayoutAdd(&layout, 1, sizeof(mvfxEffect), alignof(mvfxEffect));
    size_t emittersAt = mvfxLayoutAdd(&layout, (size_t)def->emitterCount,
                                      sizeof(mvfxCompiledEmitter), alignof(mvfxCompiledEmitter));
    size_t curvesAt =
        mvfxLayoutAdd(&layout, (size_t)curveCount, sizeof(mvfxRateCurve), alignof(mvfxRateCurve));
    size_t burstsAt = mvfxLayoutAdd(&layout, (size_t)burstCount, sizeof(mvfxCompiledBurst),
                                    alignof(mvfxCompiledBurst));
    if (layout.overflow)
    {
        return nullptr;
    }
    unsigned char* block = mvfxAllocate(allocator, layout.size, layout.alignment);
    if (block == nullptr)
    {
        return nullptr;
    }
    mvfxEffect* effect = (mvfxEffect*)block;
    *effect = (mvfxEffect){
        .emitters = (mvfxCompiledEmitter*)(block + emittersAt),
        .emitterCount = def->emitterCount,
        .burstCount = burstCount,
        .blockBytes = layout.size,
        .blockAlignment = layout.alignment,
    };
    mvfxRateCurve* curves = (mvfxRateCurve*)(block + curvesAt);
    mvfxCompiledBurst* bursts = (mvfxCompiledBurst*)(block + burstsAt);
    int32_t nextCurve = 0;
    int32_t nextBurst = 0;
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        const mvfxEmitterDef* source = &def->emitters[i];
        int32_t padded = (source->capacity + MVFX_ATTRIBUTE_PADDING - 1) / MVFX_ATTRIBUTE_PADDING *
                         MVFX_ATTRIBUTE_PADDING;
        const mvfxRateCurve* curve = nullptr;
        if (source->rateCurve.keyCount > 0)
        {
            BakeRateCurve(&source->rateCurve, source->rate, &curves[nextCurve]);
            curve = &curves[nextCurve++];
        }
        for (int32_t b = 0; b < source->burstCount; ++b)
        {
            bursts[nextBurst + b] = CompileBurst(&source->bursts[b]);
        }
        effect->emitters[i] = (mvfxCompiledEmitter){
            .capacity = source->capacity,
            .paddedCapacity = padded,
            .rate = source->rate,
            .rateCurve = curve,
            .bursts = &bursts[nextBurst],
            .burstCount = source->burstCount,
            .firstBurst = nextBurst,
            .delay = mvfxSecondsToTicks((double)source->startDelay),
            .duration = mvfxSecondsToTicks((double)source->duration),
            .loops = source->duration > 0.0f ? source->loops : 0,
            .lifetime = source->lifetime,
            .plan = PlanAttributes(padded),
        };
        mvfxCompileShape(&source->shape, &effect->emitters[i].shape);
        mvfxCompileVelocity(&source->velocity, &effect->emitters[i].velocity);
        double seconds = (double)effect->emitters[i].duration / (double)MVFX_TICKS_PER_SECOND;
        effect->emitters[i].loopIntegral =
            curve != nullptr ? seconds * mvfxCurveIntegralAt(&curve->table, &curve->integral, 1.0)
                             : seconds * (double)source->rate;
        nextBurst += source->burstCount;
    }
    return effect;
}

void mvfxReleaseEffect(mvfxEffect* effect, const mvfxAllocator* allocator)
{
    mvfxRelease(allocator, effect, effect->blockBytes, effect->blockAlignment);
}
