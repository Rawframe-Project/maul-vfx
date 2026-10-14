// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Effect defs checked and compiled: every field against its range, then
// each emitter's times in ticks, its rate curve baked with its integral,
// its bursts, and its attribute plan laid out.

#include "effect.h"

#include "allocator.h"
#include "force.h"

#include <math.h>

mvfxEmitterDef mvfxDefaultEmitterDef(void)
{
    return (mvfxEmitterDef){
        .cookie = MVFX_EMITTER_DEF_COOKIE,
        .capacity = 1024,
        .rate = 64.0f,
        .lifetime = {.kind = mvfx_sourceRange, .range = {1.0f, 1.0f}},
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

// Whether a motion's gravity, space and drags are in range.
static bool IsMotionValid(const mvfxMotion* motion)
{
    for (int32_t k = 0; k < 3; ++k)
    {
        if (!(fabsf(motion->gravity[k]) <= MVFX_MAX_EXTENT))
        {
            return false;
        }
    }
    return motion->gravitySpace <= mvfx_spaceLocal && IsWithin(motion->drag, 0.0f, MVFX_MAX_DRAG) &&
           IsWithin(motion->angularDrag, 0.0f, MVFX_MAX_DRAG);
}

static bool IsEmitterDefValid(const mvfxEmitterDef* def)
{
    if (def->cookie != MVFX_EMITTER_DEF_COOKIE || def->capacity < 1 ||
        def->capacity > MVFX_MAX_EMITTER_CAPACITY ||
        !IsWithin(def->rate, 0.0f, MVFX_MAX_SPAWN_RATE) ||
        !IsWithin(def->startDelay, 0.0f, MVFX_MAX_LIFETIME) ||
        !(def->duration == 0.0f || IsWithin(def->duration, MVFX_MIN_DURATION, MVFX_MAX_LIFETIME)) ||
        def->loops < 0 || def->burstCount < 0 || def->burstCount > MVFX_MAX_BURSTS ||
        def->space > mvfx_spaceLocal || !IsMotionValid(&def->motion) ||
        !IsWithin(def->ratePerDistance, 0.0f, MVFX_MAX_DISTANCE_RATE) ||
        !IsWithin(def->inheritVelocity, -10.0f, 10.0f) ||
        (def->space == mvfx_spaceLocal && def->inheritVelocity != 0.0f) ||
        def->hostSpawnCapacity < 0 || def->hostSpawnCapacity > def->capacity ||
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

// Whether a lifetime is valid: required, and a range of lives above 0 and
// at most MVFX_MAX_LIFETIME.
static bool IsLifetimeValid(const mvfxScalarSource* lifetime, bool hasDuration,
                            int32_t parameterCount)
{
    if (!mvfxIsScalarSourceValid(lifetime, false, hasDuration, parameterCount))
    {
        return false;
    }
    return lifetime->kind != mvfx_sourceRange ||
           (lifetime->range.min > 0.0f && lifetime->range.max <= MVFX_MAX_LIFETIME);
}

// Whether an emitter's initial values are valid against its effect's
// parameters.
static bool AreValuesValid(const mvfxEmitterDef* def, int32_t parameterCount)
{
    bool d = def->duration > 0.0f;
    if (!IsLifetimeValid(&def->lifetime, d, parameterCount) ||
        !mvfxIsVectorSourceValid(&def->size, 3, d, parameterCount) ||
        !mvfxIsVectorSourceValid(&def->color, 4, d, parameterCount) ||
        !mvfxIsScalarSourceValid(&def->angle, true, d, parameterCount) ||
        !mvfxIsScalarSourceValid(&def->spin, true, d, parameterCount) ||
        !mvfxIsScalarSourceValid(&def->motion.dragScale, true, d, parameterCount) ||
        def->orientationMode > mvfx_orientationRandom ||
        (def->orientationMode == mvfx_orientationEuler &&
         def->orientation.kind == mvfx_sourceNone) ||
        !mvfxIsVectorSourceValid(&def->orientation, 3, d, parameterCount) ||
        !mvfxIsVectorSourceValid(&def->angularVelocity, 3, d, parameterCount) ||
        def->frame.mode > mvfx_frameRandom || def->frame.first < 0 ||
        def->frame.first > def->frame.last || def->frame.last >= MVFX_MAX_FRAMES ||
        def->customCount < 0 || def->customCount > MVFX_MAX_CUSTOM_ATTRIBUTES ||
        (def->customCount > 0 && def->custom == nullptr) || def->forceCount < 0 ||
        def->forceCount > MVFX_MAX_FORCES || (def->forceCount > 0 && def->forces == nullptr))
    {
        return false;
    }
    for (int32_t f = 0; f < def->forceCount; ++f)
    {
        if (!mvfxIsForceValid(&def->forces[f], parameterCount))
        {
            return false;
        }
    }
    for (int32_t i = 0; i < def->customCount; ++i)
    {
        const mvfxCustomAttribute* custom = &def->custom[i];
        if (custom->components < 1 || custom->components > 4 ||
            !mvfxIsVectorSourceValid(&custom->initial, custom->components, d, parameterCount))
        {
            return false;
        }
    }
    return true;
}

bool mvfxIsEffectDefValid(const mvfxEffectDef* def)
{
    if (def == nullptr || def->cookie != MVFX_EFFECT_DEF_COOKIE || def->emitters == nullptr ||
        def->emitterCount < 1 || def->emitterCount > MVFX_MAX_EMITTERS)
    {
        return false;
    }
    if (def->parameterCount < 0 || def->parameterCount > MVFX_MAX_PARAMETERS ||
        (def->parameterCount > 0 && def->parameters == nullptr))
    {
        return false;
    }
    for (int32_t i = 0; i < 4 * def->parameterCount; ++i)
    {
        if (!isfinite(def->parameters[i]))
        {
            return false;
        }
    }
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        if (!IsEmitterDefValid(&def->emitters[i]) ||
            !AreValuesValid(&def->emitters[i], def->parameterCount))
        {
            return false;
        }
    }
    return true;
}

// The attributes an emitter stores: position, birth tick, inverse life,
// serial and velocity always; each optional value when its source is set;
// each custom attribute's components.
static uint64_t PresentAttributes(const mvfxEmitterDef* def)
{
    uint64_t present = 0;
    for (int32_t a = 0; a <= mvfx_attributeVelocityZ; ++a)
    {
        present |= (uint64_t)1 << a;
    }
    uint64_t three = 7u;
    uint64_t four = 15u;
    present |= def->size.kind != mvfx_sourceNone ? three << mvfx_attributeSizeX : 0;
    present |= def->color.kind != mvfx_sourceNone ? four << mvfx_attributeColorR : 0;
    bool sprite = def->angle.kind != mvfx_sourceNone || def->spin.kind != mvfx_sourceNone;
    present |= sprite ? (uint64_t)3 << mvfx_attributeAngle : 0;
    bool mesh = def->orientationMode != mvfx_orientationNone ||
                def->angularVelocity.kind != mvfx_sourceNone;
    present |= mesh ? (uint64_t)127 << mvfx_attributeOrientationX : 0;
    present |= def->frame.mode != mvfx_frameNone ? (uint64_t)1 << mvfx_attributeFrame : 0;
    present |=
        def->motion.dragScale.kind != mvfx_sourceNone ? (uint64_t)1 << mvfx_attributeDrag : 0;
    for (int32_t i = 0; i < def->customCount; ++i)
    {
        uint64_t components = ((uint64_t)1 << def->custom[i].components) - 1;
        present |= components << (mvfx_attributeCustom + 4 * i);
    }
    return present;
}

// The plan of an emitter's attributes, each padded and aligned.
static mvfxAttributePlan PlanAttributes(const mvfxEmitterDef* def, int32_t paddedCapacity)
{
    mvfxAttributePlan plan = {.present = PresentAttributes(def)};
    mvfxLayout layout = {0};
    for (int32_t a = 0; a < MVFX_ATTRIBUTE_COUNT; ++a)
    {
        if ((plan.present & ((uint64_t)1 << a)) != 0)
        {
            plan.offsets[a] = mvfxLayoutAdd(&layout, (size_t)paddedCapacity, sizeof(float),
                                            MVFX_ATTRIBUTE_ALIGNMENT);
        }
    }
    plan.blockBytes = layout.size;
    return plan;
}

// The curve and gradient tables an emitter's values need.
static void CountTables(const mvfxEmitterDef* def, int32_t* curves, int32_t* gradients)
{
    const mvfxVectorSource* vectors[4] = {&def->size, &def->color, &def->orientation,
                                          &def->angularVelocity};
    *curves += mvfxScalarCurves(&def->lifetime) + mvfxScalarCurves(&def->angle) +
               mvfxScalarCurves(&def->spin) + mvfxScalarCurves(&def->motion.dragScale);
    for (int32_t v = 0; v < 4; ++v)
    {
        *curves += mvfxVectorCurves(vectors[v]);
        *gradients += mvfxVectorGradients(vectors[v]);
    }
    for (int32_t i = 0; i < def->customCount; ++i)
    {
        *curves += mvfxVectorCurves(&def->custom[i].initial);
        *gradients += mvfxVectorGradients(&def->custom[i].initial);
    }
}

// Compiles an emitter's initial values into out, its tables into space.
static void CompileValues(const mvfxEmitterDef* def, mvfxTableSpace* space,
                          mvfxCompiledEmitter* out)
{
    bool d = def->duration > 0.0f;
    mvfxCompileScalar(&def->lifetime, d, space, &out->lifetime);
    mvfxCompileVector(&def->size, 3, d, space, &out->size);
    mvfxCompileVector(&def->color, 4, d, space, &out->color);
    mvfxCompileScalar(&def->angle, d, space, &out->angle);
    mvfxCompileScalar(&def->spin, d, space, &out->spin);
    mvfxCompileScalar(&def->motion.dragScale, d, space, &out->dragScale);
    for (int32_t k = 0; k < 3; ++k)
    {
        out->gravity[k] = def->motion.gravity[k];
    }
    out->gravitySpace = def->motion.gravitySpace;
    out->drag = def->motion.drag;
    out->angularDrag = def->motion.angularDrag;
    out->forceCount = def->forceCount;
    for (int32_t f = 0; f < def->forceCount; ++f)
    {
        out->forces[f] = mvfxCompileForce(&def->forces[f]);
    }
    out->orientationMode = def->orientationMode;
    mvfxCompileVector(&def->orientation, 3, d, space, &out->orientation);
    mvfxCompileVector(&def->angularVelocity, 3, d, space, &out->angularVelocity);
    out->frame = def->frame;
    out->customCount = def->customCount;
    for (int32_t i = 0; i < def->customCount; ++i)
    {
        mvfxCompileVector(&def->custom[i].initial, def->custom[i].components, d, space,
                          &out->custom[i]);
    }
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

// Where emitters' compiled parts go in an effect's block, and the next
// free place of each.
typedef struct mvfxEmitterSpace
{
    mvfxTableSpace values;
    mvfxRateCurve* curves;
    mvfxCompiledBurst* bursts;
    int32_t nextCurve;
    int32_t nextBurst;
} mvfxEmitterSpace;

// Compiles one emitter, its curves, bursts and tables into space.
static void CompileEmitter(const mvfxEmitterDef* source, struct mvfxMesh* mesh,
                           mvfxEmitterSpace* space, mvfxCompiledEmitter* out)
{
    int32_t padded = (source->capacity + MVFX_ATTRIBUTE_PADDING - 1) / MVFX_ATTRIBUTE_PADDING *
                     MVFX_ATTRIBUTE_PADDING;
    const mvfxRateCurve* curve = nullptr;
    if (source->rateCurve.keyCount > 0)
    {
        BakeRateCurve(&source->rateCurve, source->rate, &space->curves[space->nextCurve]);
        curve = &space->curves[space->nextCurve++];
    }
    mvfxCompiledBurst* bursts = &space->bursts[space->nextBurst];
    for (int32_t b = 0; b < source->burstCount; ++b)
    {
        bursts[b] = CompileBurst(&source->bursts[b]);
    }
    *out = (mvfxCompiledEmitter){
        .capacity = source->capacity,
        .paddedCapacity = padded,
        .rate = source->rate,
        .rateCurve = curve,
        .bursts = bursts,
        .burstCount = source->burstCount,
        .firstBurst = space->nextBurst,
        .delay = mvfxSecondsToTicks((double)source->startDelay),
        .duration = mvfxSecondsToTicks((double)source->duration),
        .loops = source->duration > 0.0f ? source->loops : 0,
        .plan = PlanAttributes(source, padded),
        .space = source->space,
        .ratePerDistance = source->ratePerDistance,
        .inheritVelocity = source->inheritVelocity,
        .hostSpawnCapacity = source->hostSpawnCapacity,
    };
    space->nextBurst += source->burstCount;
    CompileValues(source, &space->values, out);
    mvfxCompileShape(&source->shape, mesh, &out->shape);
    mvfxCompileVelocity(&source->velocity, &out->velocity);
    double seconds = (double)out->duration / (double)MVFX_TICKS_PER_SECOND;
    out->loopIntegral = curve != nullptr
                            ? seconds * mvfxCurveIntegralAt(&curve->table, &curve->integral, 1.0)
                            : seconds * (double)source->rate;
}

mvfxEffect* mvfxCompileEffect(const mvfxEffectDef* def, struct mvfxMesh* const* meshes,
                              const mvfxAllocator* allocator)
{
    int32_t curveCount = 0;
    int32_t burstCount = 0;
    int32_t valueCurves = 0;
    int32_t valueGradients = 0;
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        curveCount += def->emitters[i].rateCurve.keyCount > 0 ? 1 : 0;
        burstCount += def->emitters[i].burstCount;
        CountTables(&def->emitters[i], &valueCurves, &valueGradients);
    }
    mvfxLayout layout = {0};
    (void)mvfxLayoutAdd(&layout, 1, sizeof(mvfxEffect), alignof(mvfxEffect));
    size_t emittersAt = mvfxLayoutAdd(&layout, (size_t)def->emitterCount,
                                      sizeof(mvfxCompiledEmitter), alignof(mvfxCompiledEmitter));
    size_t curvesAt =
        mvfxLayoutAdd(&layout, (size_t)curveCount, sizeof(mvfxRateCurve), alignof(mvfxRateCurve));
    size_t burstsAt = mvfxLayoutAdd(&layout, (size_t)burstCount, sizeof(mvfxCompiledBurst),
                                    alignof(mvfxCompiledBurst));
    size_t valueCurvesAt = mvfxLayoutAdd(&layout, (size_t)valueCurves, sizeof(mvfxCurveTable),
                                         alignof(mvfxCurveTable));
    size_t valueGradientsAt = mvfxLayoutAdd(&layout, (size_t)valueGradients,
                                            sizeof(mvfxGradientTable), alignof(mvfxGradientTable));
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
    effect->parameterCount = def->parameterCount;
    for (int32_t p = 0; p < def->parameterCount; ++p)
    {
        for (int32_t c = 0; c < 4; ++c)
        {
            effect->parameters[p][c] = def->parameters[4 * p + c];
        }
    }
    mvfxEmitterSpace emitterSpace = {
        .values =
            {
                .curves = (mvfxCurveTable*)(block + valueCurvesAt),
                .gradients = (mvfxGradientTable*)(block + valueGradientsAt),
            },
        .curves = (mvfxRateCurve*)(block + curvesAt),
        .bursts = (mvfxCompiledBurst*)(block + burstsAt),
    };
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        CompileEmitter(&def->emitters[i], meshes[i], &emitterSpace, &effect->emitters[i]);
    }
    return effect;
}

void mvfxReleaseEffect(mvfxEffect* effect, const mvfxAllocator* allocator)
{
    mvfxRelease(allocator, effect, effect->blockBytes, effect->blockAlignment);
}
