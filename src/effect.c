// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Effect defs checked and compiled: every field against its range, then
// each emitter's times in ticks, its rate curve baked with its integral,
// its bursts, and its attribute plan laid out.

#include "effect.h"

#include "allocator.h"
#include "collide.h"
#include "elementary.h"
#include "force.h"
#include "kill.h"

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
        .velocityModules = {.motionScale = {.value = 1.0f}},
        .ribbon = {.pieces = 1,
                   .tileLength = 1.0f,
                   .normal = {0.0f, 0.0f, 1.0f},
                   .width = {.value = 1.0f}},
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

// Whether a ribbon's tessellation, texture and facing are valid.
static bool IsRibbonShapeValid(const mvfxRibbonDef* ribbon)
{
    const float* n = ribbon->normal;
    bool normal = IsWithin(n[0], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT) &&
                  IsWithin(n[1], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT) &&
                  IsWithin(n[2], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT) &&
                  (n[0] != 0.0f || n[1] != 0.0f || n[2] != 0.0f);
    return ribbon->pieces >= 1 && ribbon->pieces <= MVFX_MAX_RIBBON_PIECES &&
           IsWithin(ribbon->bendAngle, 0.0f, 3.14159265f) &&
           ribbon->texture <= mvfx_ribbonTileByDistance && ribbon->tileLength > 0.0f &&
           ribbon->tileLength <= MVFX_MAX_EXTENT &&
           IsWithin(ribbon->scroll, -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT) &&
           ribbon->facing <= mvfx_ribbonFaceNormal && normal &&
           IsWithin(ribbon->breakDistance, 0.0f, MVFX_MAX_EXTENT) &&
           (!ribbon->pinHead || ribbon->ids == mvfx_ribbonOne);
}

// Compiles a ribbon's tessellation, texture and facing: for a bend angle,
// the cosine each count of pieces allows a segment's turn down to.
static void CompileRibbonShape(const mvfxRibbonDef* ribbon, mvfxCompiledEmitter* out)
{
    out->ribbonPieces = ribbon->pieces;
    for (int32_t k = 1; k <= MVFX_MAX_RIBBON_PIECES; ++k)
    {
        float turn = fminf(ribbon->bendAngle * (float)k, 3.14159265f);
        float sine = 0.0f;
        float cosine = 1.0f;
        mvfxSinCos(turn, &sine, &cosine);
        out->ribbonCosLimits[k - 1] = ribbon->bendAngle > 0.0f ? cosine : -2.0f;
    }
    out->ribbonTexture = ribbon->texture;
    out->ribbonTileLength = ribbon->tileLength;
    out->ribbonScroll = ribbon->scroll;
    out->ribbonFacing = ribbon->facing;
    out->ribbonBreakDistance = ribbon->breakDistance;
    out->ribbonPinHead = ribbon->pinHead;
    for (int32_t k = 0; k < 3; ++k)
    {
        out->ribbonNormal[k] = ribbon->normal[k];
    }
}

// Whether a gradient has no keys, so is white.
static bool IsWhite(const mvfxGradient* gradient)
{
    return gradient->colorKeyCount == 0 && gradient->alphaKeyCount == 0;
}

// Whether an emitter's mesh index, lights, sorting and ribbons are valid.
static bool AreOutputsValid(const mvfxEmitterDef* def)
{
    const mvfxFrameStart* mesh = &def->meshIndex;
    const mvfxLightDef* light = &def->light;
    bool lightValid =
        !light->enabled || (light->every >= 1 && light->every <= 65536 &&
                            light->intensity >= 0.0f && light->intensity <= MVFX_MAX_EXTENT &&
                            light->radius >= 0.0f && light->radius <= MVFX_MAX_EXTENT &&
                            light->radiusBySize >= 0.0f && light->radiusBySize <= MVFX_MAX_EXTENT);
    const mvfxRibbonDef* ribbon = &def->ribbon;
    bool ribbonValid = ribbon->ids <= mvfx_ribbonPerSource &&
                       (ribbon->ids == mvfx_ribbonNone || IsRibbonShapeValid(ribbon)) &&
                       mvfxIsLifeCurveValid(&ribbon->width) &&
                       (IsWhite(&ribbon->color) || mvfxIsGradientValid(&ribbon->color));
    bool spriteValid = def->sprite.billboard <= mvfx_billboardDirectional &&
                       IsWithin(def->sprite.stretch, 0.0f, MVFX_MAX_EXTENT);
    return lightValid && ribbonValid && spriteValid && def->sortMode <= mvfx_sortFrontToBack &&
           def->sortMeasure <= mvfx_sortByDepth && mesh->mode <= mvfx_frameRandom &&
           mesh->first >= 0 && mesh->first <= mesh->last && mesh->last < MVFX_MAX_MESH_INDEX;
}

// Whether an emitter's collision and colliders are valid, and a kill rule
// counting collisions has collisions to count.
static bool AreCollisionsValid(const mvfxEmitterDef* def)
{
    if (!mvfxIsCollisionValid(&def->collision) || def->colliderCount < 0 ||
        def->colliderCount > MVFX_MAX_COLLIDERS ||
        (def->colliderCount > 0 && def->colliders == nullptr))
    {
        return false;
    }
    for (int32_t c = 0; c < def->colliderCount; ++c)
    {
        if (!mvfxIsColliderValid(&def->colliders[c]))
        {
            return false;
        }
    }
    for (int32_t r = 0; r < def->killRuleCount; ++r)
    {
        if (def->killRules[r].kind == mvfx_killCollisions && !def->collision.enabled)
        {
            return false;
        }
    }
    if (!AreOutputsValid(def))
    {
        return false;
    }
    for (int32_t k = 0; def->fixedBounds && k < 3; ++k)
    {
        if (!(fabsf(def->boundsMin[k]) <= MVFX_MAX_EXTENT) ||
            !(fabsf(def->boundsMax[k]) <= MVFX_MAX_EXTENT) ||
            !(def->boundsMin[k] <= def->boundsMax[k]))
        {
            return false;
        }
    }
    // Events of known kinds, collisions only where they collide.
    bool collides = def->collision.enabled || (def->events & mvfx_eventMaskCollision) == 0;
    return def->events <= 7u && collides &&
           (def->events == 0 || (def->eventCapacity >= 1 && def->eventCapacity <= MVFX_MAX_EVENTS));
}

// Whether host modules are valid: a function each, a known stage.
static bool AreHostModulesValid(const mvfxHostModule* modules, int32_t count)
{
    if (count < 0 || count > MVFX_MAX_HOST_MODULES || (count > 0 && modules == nullptr))
    {
        return false;
    }
    for (int32_t m = 0; m < count; ++m)
    {
        if (modules[m].function == nullptr || modules[m].stage > mvfx_stageAfterMotion)
        {
            return false;
        }
    }
    return true;
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
    if (!mvfxAreVelocityModulesValid(&def->velocityModules) ||
        !mvfxIsOverLifeValid(&def->overLife, def->custom, def->customCount))
    {
        return false;
    }
    if (def->killRuleCount < 0 || def->killRuleCount > MVFX_MAX_KILL_RULES ||
        (def->killRuleCount > 0 && def->killRules == nullptr))
    {
        return false;
    }
    for (int32_t r = 0; r < def->killRuleCount; ++r)
    {
        if (!mvfxIsKillRuleValid(&def->killRules[r]))
        {
            return false;
        }
    }
    if (!AreHostModulesValid(def->hostModules, def->hostModuleCount))
    {
        return false;
    }
    if (!AreCollisionsValid(def))
    {
        return false;
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

// Whether a beam's point is finite and in range, its parameter one of the
// effect's.
static bool IsBeamPointValid(const mvfxBeamPoint* point, int32_t parameterCount)
{
    return IsWithin(point->point[0], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT) &&
           IsWithin(point->point[1], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT) &&
           IsWithin(point->point[2], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT) &&
           (!point->fromParameter || (point->parameter >= 0 && point->parameter < parameterCount));
}

// Whether a beam is valid: its shape in range, and nothing that would
// spawn or move its points but the beam.
static bool IsBeamValid(const mvfxEmitterDef* def, int32_t parameterCount)
{
    const mvfxBeamDef* beam = &def->beam;
    if (!beam->enabled)
    {
        return true;
    }
    bool shape = beam->segments >= 1 && beam->segments <= MVFX_MAX_BEAM_SEGMENTS &&
                 def->capacity >= beam->segments + 1 && beam->controlCount >= 0 &&
                 beam->controlCount <= 2 && IsBeamPointValid(&beam->start, parameterCount) &&
                 IsBeamPointValid(&beam->end, parameterCount) &&
                 IsWithin(beam->noiseAmplitude, 0.0f, MVFX_MAX_EXTENT) &&
                 IsWithin(beam->noiseFrequency, 0.0f, 1000.0f) &&
                 IsWithin(beam->noiseSpeed, -1000.0f, 1000.0f);
    for (int32_t c = 0; shape && c < beam->controlCount; ++c)
    {
        shape = IsBeamPointValid(&beam->controls[c], parameterCount);
    }
    bool alone = def->rate == 0.0f && def->burstCount == 0 && def->ratePerDistance == 0.0f &&
                 !def->eventSpawn.enabled && def->hostSpawnCapacity == 0 &&
                 !def->collision.enabled && def->forceCount == 0 && !def->contacts.enabled &&
                 !def->steering.enabled && !def->pairForce.enabled &&
                 (def->events & mvfx_eventMaskBirth) == 0;
    return shape && alone && def->ribbon.ids == mvfx_ribbonOne && !def->ribbon.pinHead;
}

// Whether an emitter's spawning from events is valid against its effect:
// a source not its own that writes the kind, counts and a share in range.
static bool IsEventSpawnValid(const mvfxEffectDef* def, int32_t i)
{
    const mvfxEventSpawn* spawn = &def->emitters[i].eventSpawn;
    if (!spawn->enabled)
    {
        return true;
    }
    if (spawn->source < 0 || spawn->source >= def->emitterCount || spawn->source == i ||
        spawn->kind > mvfx_eventCollision)
    {
        return false;
    }
    bool written = (def->emitters[spawn->source].events & (1u << spawn->kind)) != 0;
    return written && spawn->minCount >= 0 && spawn->minCount <= spawn->maxCount &&
           spawn->maxCount <= MVFX_MAX_EVENT_SPAWN && spawn->inheritVelocity >= 0.0f &&
           spawn->inheritVelocity <= 1.0f && spawn->capacity >= 1 &&
           spawn->capacity <= MVFX_MAX_EMITTER_CAPACITY;
}

// Whether a source is an emitter of the effect already placed: one out of
// range never is, so its def is refused here, before any check of it.
static bool IsPlaced(const bool placed[MVFX_MAX_EMITTERS], int32_t source, int32_t count)
{
    return source >= 0 && source < count && placed[source];
}

// The order an effect's emitters step in: repeatedly the lowest index
// whose source (if any) has stepped; false for a cycle or a source out of
// range.
static bool OrderEmitters(const mvfxEffectDef* def, int32_t order[MVFX_MAX_EMITTERS])
{
    bool placed[MVFX_MAX_EMITTERS] = {false};
    for (int32_t n = 0; n < def->emitterCount; ++n)
    {
        int32_t next = -1;
        for (int32_t i = 0; next < 0 && i < def->emitterCount; ++i)
        {
            const mvfxEventSpawn* spawn = &def->emitters[i].eventSpawn;
            const mvfxNeighborDef* neighbors = &def->emitters[i].neighbors;
            bool ready =
                (!spawn->enabled || IsPlaced(placed, spawn->source, def->emitterCount)) &&
                (!neighbors->enabled || neighbors->source == -1 || neighbors->source == i ||
                 IsPlaced(placed, neighbors->source, def->emitterCount));
            next = !placed[i] && ready ? i : next;
        }
        if (next < 0)
        {
            return false;
        }
        placed[next] = true;
        order[n] = next;
    }
    return true;
}

// Whether an effect's quality levels are valid: a count in range, scales
// in range, and only its own emitters left out.
static bool AreQualityLevelsValid(const mvfxEffectDef* def)
{
    if (def->qualityLevelCount < 0 || def->qualityLevelCount > MVFX_MAX_QUALITY_LEVELS ||
        (def->qualityLevelCount > 0 && def->qualityLevels == nullptr))
    {
        return false;
    }
    uint64_t own =
        def->emitterCount >= 64 ? ~(uint64_t)0 : ((uint64_t)1 << (uint32_t)def->emitterCount) - 1u;
    for (int32_t q = 0; q < def->qualityLevelCount; ++q)
    {
        const mvfxQualityLevel* level = &def->qualityLevels[q];
        if (!(level->capacityScale > 0.0f && level->capacityScale <= 1.0f) ||
            !IsWithin(level->rateScale, 0.0f, 1.0f) || (level->omitted & ~own) != 0)
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
        (def->parameterCount > 0 && def->parameters == nullptr) || !AreQualityLevelsValid(def))
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
            !AreValuesValid(&def->emitters[i], def->parameterCount) || !IsEventSpawnValid(def, i) ||

            !IsBeamValid(&def->emitters[i], def->parameterCount))
        {
            return false;
        }
    }
    int32_t order[MVFX_MAX_EMITTERS];
    return OrderEmitters(def, order);
}

// Adds count attributes from first to a plan when a condition holds.
static void Store(mvfxAttributePlan* plan, bool when, int32_t first, int32_t count)
{
    for (int32_t a = first; when && a < first + count; ++a)
    {
        plan->present[a >> 6] |= (uint64_t)1 << (a & 63);
    }
}

// The attributes an emitter stores: position, birth tick, inverse life,
// serial and velocity always; each optional value when its source is set;
// each custom attribute's components.
static void StoreAttributes(const mvfxEmitterDef* def, mvfxAttributePlan* plan)
{
    Store(plan, true, 0, mvfx_attributeVelocityZ + 1);
    Store(plan, def->size.kind != mvfx_sourceNone, mvfx_attributeSizeX, 3);
    Store(plan, def->color.kind != mvfx_sourceNone, mvfx_attributeColorR, 4);
    bool sprite = def->angle.kind != mvfx_sourceNone || def->spin.kind != mvfx_sourceNone;
    Store(plan, sprite, mvfx_attributeAngle, 2);
    bool mesh = def->orientationMode != mvfx_orientationNone ||
                def->angularVelocity.kind != mvfx_sourceNone;
    Store(plan, mesh, mvfx_attributeOrientationX, 7);
    Store(plan, def->frame.mode != mvfx_frameNone, mvfx_attributeFrame, 1);
    Store(plan, def->meshIndex.mode != mvfx_frameNone, mvfx_attributeMesh, 1);
    Store(plan, def->ribbon.ids != mvfx_ribbonNone, mvfx_attributeRibbon, 1);
    Store(plan,
          def->ribbon.ids != mvfx_ribbonNone && def->ribbon.texture == mvfx_ribbonTileByDistance,
          mvfx_attributeRibbonDistance, 1);
    Store(plan, def->motion.dragScale.kind != mvfx_sourceNone, mvfx_attributeDrag, 1);
    Store(plan, def->collision.enabled, mvfx_attributeCollisions, 1);
    Store(plan, def->neighbors.enabled, mvfx_attributeNeighborCount, 7);
    Store(plan, def->unit.kind != mvfx_unitParticle && def->unit.pinFirst, mvfx_attributeAnchorX,
          3);
    Store(plan, def->collision.enabled && def->collision.sceneQueries, mvfx_attributeScenePointX,
          6);
    for (int32_t i = 0; i < def->customCount; ++i)
    {
        Store(plan, true, mvfx_attributeCustom + 4 * i, def->custom[i].components);
    }
}

// The plan of an emitter's attributes: a slot for each it stores.
static mvfxAttributePlan PlanAttributes(const mvfxEmitterDef* def)
{
    mvfxAttributePlan plan = {0};
    StoreAttributes(def, &plan);
    for (int32_t a = 0; a < MVFX_ATTRIBUTE_COUNT; ++a)
    {
        if (mvfxHasAttribute(&plan, a))
        {
            plan.slots[a] = (uint8_t)plan.arrayCount++;
        }
    }
    return plan;
}

// The curve and gradient tables an emitter's values need.
static void CountTables(const mvfxEmitterDef* def, int32_t* curves, int32_t* gradients)
{
    const mvfxVectorSource* vectors[4] = {&def->size, &def->color, &def->orientation,
                                          &def->angularVelocity};
    *curves += mvfxScalarCurves(&def->lifetime) + mvfxScalarCurves(&def->angle) +
               mvfxScalarCurves(&def->spin) + mvfxScalarCurves(&def->motion.dragScale) +
               mvfxVelocityModuleCurves(&def->velocityModules);
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
    mvfxOverLifeTables(&def->overLife, def->custom, def->customCount, curves, gradients);
    *curves += mvfxLifeCurves(&def->ribbon.width);
    *curves += def->pairForce.enabled ? mvfxLifeCurves(&def->pairForce.strength) : 0;
    *gradients += IsWhite(&def->ribbon.color) ? 0 : 1;
}

// Compiles an emitter's pair force, its strength's table into space.
static void CompilePairForce(const mvfxPairForceDef* pair, mvfxTableSpace* space,
                             mvfxCompiledEmitter* out)
{
    out->pairRadius = pair->enabled ? pair->radius : 0.0f;
    out->pairStrength = (mvfxCompiledLife){0};
    if (pair->enabled)
    {
        mvfxCompileLife(&pair->strength, space, &out->pairStrength);
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
    mvfxCompileVelocityModules(&def->velocityModules, space, &out->velocityModules);
    mvfxCompileOverLife(&def->overLife, def->custom, def->customCount, space, &out->overLife);
    out->events = def->events;
    out->eventSpawn = def->eventSpawn;
    out->drawPriority = def->drawPriority;
    out->fixedBounds = def->fixedBounds;
    for (int32_t k = 0; k < 3; ++k)
    {
        out->boundsMin[k] = def->boundsMin[k];
        out->boundsMax[k] = def->boundsMax[k];
    }
    out->eventCapacity = def->events != 0 ? def->eventCapacity : 0;
    out->collision = def->collision;
    out->colliderCount = def->colliderCount;
    for (int32_t c = 0; c < def->colliderCount; ++c)
    {
        out->colliders[c] = mvfxCompileCollider(&def->colliders[c]);
    }
    out->hostModuleCount = def->hostModuleCount;
    for (int32_t m = 0; m < def->hostModuleCount; ++m)
    {
        out->hostModules[m] = def->hostModules[m];
    }
    out->killRuleCount = def->killRuleCount;
    for (int32_t r = 0; r < def->killRuleCount; ++r)
    {
        out->killRules[r] = mvfxCompileKillRule(&def->killRules[r]);
    }
    out->forceCount = def->forceCount;
    for (int32_t f = 0; f < def->forceCount; ++f)
    {
        out->forces[f] = mvfxCompileForce(&def->forces[f]);
    }
    out->orientationMode = def->orientationMode;
    mvfxCompileVector(&def->orientation, 3, d, space, &out->orientation);
    mvfxCompileVector(&def->angularVelocity, 3, d, space, &out->angularVelocity);
    out->frame = def->frame;
    out->meshIndex = def->meshIndex;
    out->light = def->light;
    out->sortMode = def->sortMode;
    out->sortMeasure = def->sortMeasure;
    out->ribbonIds = def->ribbon.ids;
    out->beam = def->beam;
    out->sprite = def->sprite;
    CompileRibbonShape(&def->ribbon, out);
    mvfxCompileLife(&def->ribbon.width, space, &out->ribbonWidth);
    CompilePairForce(&def->pairForce, space, out);
    out->ribbonColor = nullptr;
    if (!IsWhite(&def->ribbon.color))
    {
        mvfxGradientTable* table = &space->gradients[space->nextGradient];
        space->nextGradient += 1;
        mvfxBakeGradient(&def->ribbon.color, table);
        out->ribbonColor = table;
    }
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
        // A particle a birth until mvfxCompileEffectParts says otherwise.
        .unitPoints = 1,
        .paddedCapacity = padded,
        .rate = source->rate,
        .rateCurve = curve,
        .bursts = bursts,
        .burstCount = source->burstCount,
        .firstBurst = space->nextBurst,
        .delay = mvfxSecondsToTicks((double)source->startDelay),
        .duration = mvfxSecondsToTicks((double)source->duration),
        .loops = source->duration > 0.0f ? source->loops : 0,
        .plan = PlanAttributes(source),
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
    effect->qualityLevels[0] = (mvfxQualityLevel){.capacityScale = 1.0f, .rateScale = 1.0f};
    for (int32_t q = 0; q < def->qualityLevelCount; ++q)
    {
        effect->qualityLevels[q + 1] = def->qualityLevels[q];
    }
    effect->qualityLevelCount = def->qualityLevelCount + 1;
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
    (void)OrderEmitters(def, effect->stepOrder);
    return effect;
}

void mvfxReleaseEffect(mvfxEffect* effect, const mvfxAllocator* allocator)
{
    mvfxRelease(allocator, effect, effect->blockBytes, effect->blockAlignment);
}
