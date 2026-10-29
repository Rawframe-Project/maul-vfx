// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Effect defs checked: every field of the def and of its emitters against
// its range. A value past one of the library's named bounds answers
// mvfx_errorLimit, anything else wrong mvfx_errorInvalid, and the first
// check that fails decides (refusal.h).

#include "effect_check.h"

#include "collide.h"
#include "effect_gas.h"
#include "force.h"
#include "gas.h"
#include "kill.h"
#include "life.h"
#include "over_life.h"
#include "refusal.h"
#include "shape.h"
#include "value.h"
#include "velocity.h"
#include "velocity_modules.h"
#include "water.h"

#include <math.h>

// Whether a gradient has no keys, so is white.
static bool IsWhite(const mvfxGradient* gradient)
{
    return gradient->colorKeyCount == 0 && gradient->alphaKeyCount == 0;
}

// Whether low <= x <= high; false for NaN.
static bool IsWithin(float x, float low, float high)
{
    return x >= low && x <= high;
}

// Whether a rate curve is valid for an emitter of this rate and duration:
// no keys, or keys over [0, 1] that bake to values never below 0 and never
// past MVFX_MAX_SPAWN_RATE once times rate.
static mvfxResult CheckRateCurve(const mvfxEmitterDef* def)
{
    if (def->rateCurve.keyCount == 0)
    {
        return mvfx_success;
    }
    if (def->duration <= 0.0f)
    {
        return mvfx_errorInvalid;
    }
    mvfxResult curve = mvfxCheckCurve(&def->rateCurve, true);
    if (curve != mvfx_success)
    {
        return curve;
    }
    mvfxRateCurve baked;
    mvfxBakeRateCurve(&def->rateCurve, 1.0f, &baked);
    if (!mvfxIsCurveTableNonNegative(&baked.table))
    {
        return mvfx_errorInvalid;
    }
    for (int32_t i = 0; i < MVFX_CURVE_SAMPLES; ++i)
    {
        if (!((double)baked.table.samples[i] * (double)def->rate <= (double)MVFX_MAX_SPAWN_RATE))
        {
            return mvfx_errorLimit;
        }
    }
    return mvfx_success;
}

static mvfxResult CheckBurst(const mvfxBurst* burst)
{
    if (burst->minCount < 0 || burst->minCount > burst->maxCount || burst->cycles < 0 ||
        !IsWithin(burst->probability, 0.0f, 1.0f))
    {
        return mvfx_errorInvalid;
    }
    mvfxResult interval = burst->cycles == 1
                              ? mvfx_success
                              : mvfxBounded(burst->interval, MVFX_MIN_DURATION, MVFX_MAX_LIFETIME);
    return mvfxFirst(mvfxFirst(mvfxUpToBound(burst->time, 0.0f, MVFX_MAX_LIFETIME), interval),
                     mvfxWithinBound(burst->maxCount <= MVFX_MAX_EMITTER_CAPACITY));
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
static mvfxResult CheckMotion(const mvfxMotion* motion)
{
    if (motion->gravitySpace > mvfx_spaceLocal)
    {
        return mvfx_errorInvalid;
    }
    return mvfxFirst(mvfxBoundedVector(motion->gravity, MVFX_MAX_EXTENT),
                     mvfxFirst(mvfxUpToBound(motion->drag, 0.0f, MVFX_MAX_DRAG),
                               mvfxUpToBound(motion->angularDrag, 0.0f, MVFX_MAX_DRAG)));
}

static mvfxResult CheckEmitterDef(const mvfxEmitterDef* def)
{
    if (def->cookie != MVFX_EMITTER_DEF_COOKIE || def->capacity < 1 || def->loops < 0 ||
        def->burstCount < 0 || def->space > mvfx_spaceLocal ||
        !IsWithin(def->inheritVelocity, -10.0f, 10.0f) ||
        (def->space == mvfx_spaceLocal && def->inheritVelocity != 0.0f) ||
        def->hostSpawnCapacity < 0 || def->hostSpawnCapacity > def->capacity ||
        (def->burstCount > 0 && def->bursts == nullptr) || def->rateCurve.keyCount < 0)
    {
        return mvfx_errorInvalid;
    }
    mvfxResult duration = def->duration == 0.0f
                              ? mvfx_success
                              : mvfxBounded(def->duration, MVFX_MIN_DURATION, MVFX_MAX_LIFETIME);
    mvfxResult fields = mvfxFirst(
        mvfxFirst(mvfxWithinBound(def->capacity <= MVFX_MAX_EMITTER_CAPACITY),
                  mvfxUpToBound(def->rate, 0.0f, MVFX_MAX_SPAWN_RATE)),
        mvfxFirst(mvfxFirst(mvfxUpToBound(def->startDelay, 0.0f, MVFX_MAX_LIFETIME), duration),
                  mvfxFirst(mvfxWithinBound(def->burstCount <= MVFX_MAX_BURSTS),
                            mvfxUpToBound(def->ratePerDistance, 0.0f, MVFX_MAX_DISTANCE_RATE))));
    if (fields != mvfx_success)
    {
        return fields;
    }
    mvfxResult motion = CheckMotion(&def->motion);
    if (motion != mvfx_success)
    {
        return motion;
    }
    for (int32_t b = 0; b < def->burstCount; ++b)
    {
        mvfxResult burst = CheckBurst(&def->bursts[b]);
        if (burst != mvfx_success)
        {
            return burst;
        }
    }
    mvfxResult rate = mvfxFirst(mvfxWithinBound(BurstRate(def) <= (double)MVFX_MAX_SPAWN_RATE),
                                CheckRateCurve(def));
    if (rate != mvfx_success)
    {
        return rate;
    }
    return mvfxFirst(mvfxCheckShape(&def->shape), mvfxCheckVelocity(&def->velocity));
}

// Whether a lifetime is valid: required, and a range of lives above 0
// (else mvfx_errorInvalid) and at most MVFX_MAX_LIFETIME (else
// mvfx_errorLimit).
static mvfxResult CheckLifetime(const mvfxScalarSource* lifetime, bool hasDuration,
                                int32_t parameterCount)
{
    mvfxResult source =
        mvfxCheckScalarSource(lifetime, false, hasDuration, parameterCount, MVFX_MAX_DRAW);
    if (source != mvfx_success || lifetime->kind != mvfx_sourceRange)
    {
        return source;
    }
    return lifetime->range.min > 0.0f ? mvfxWithinBound(lifetime->range.max <= MVFX_MAX_LIFETIME)
                                      : mvfx_errorInvalid;
}

// Whether a ribbon's tessellation, texture and facing are valid.
static mvfxResult CheckRibbonShape(const mvfxRibbonDef* ribbon)
{
    const float* n = ribbon->normal;
    if (ribbon->pieces < 1 || !IsWithin(ribbon->bendAngle, 0.0f, 3.14159265f) ||
        ribbon->texture > mvfx_ribbonTileByDistance || ribbon->facing > mvfx_ribbonFaceNormal ||
        (n[0] == 0.0f && n[1] == 0.0f && n[2] == 0.0f) ||
        (ribbon->pinHead && ribbon->ids != mvfx_ribbonOne))
    {
        return mvfx_errorInvalid;
    }
    return mvfxFirst(
        mvfxFirst(mvfxWithinBound(ribbon->pieces <= MVFX_MAX_RIBBON_PIECES),
                  mvfxPositiveUpTo(ribbon->tileLength, MVFX_MAX_EXTENT)),
        mvfxFirst(mvfxFirst(mvfxBounded(ribbon->scroll, -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT),
                            mvfxBoundedVector(n, MVFX_MAX_EXTENT)),
                  mvfxUpToBound(ribbon->breakDistance, 0.0f, MVFX_MAX_EXTENT)));
}

// Whether an emitter's mesh index, lights, sorting and ribbons are valid.
static mvfxResult CheckOutputs(const mvfxEmitterDef* def)
{
    const mvfxFrameStart* mesh = &def->meshIndex;
    const mvfxLightDef* light = &def->light;
    const mvfxRibbonDef* ribbon = &def->ribbon;
    if ((light->enabled && (light->every < 1 || light->every > 65536)) ||
        ribbon->ids > mvfx_ribbonPerSource || def->sprite.billboard > mvfx_billboardDirectional ||
        def->sortMode > mvfx_sortFrontToBack || def->sortMeasure > mvfx_sortByDepth ||
        mesh->mode > mvfx_frameRandom || mesh->first < 0 || mesh->first > mesh->last)
    {
        return mvfx_errorInvalid;
    }
    mvfxResult lit =
        light->enabled
            ? mvfxFirst(mvfxUpToBound(light->intensity, 0.0f, MVFX_MAX_EXTENT),
                        mvfxFirst(mvfxUpToBound(light->radius, 0.0f, MVFX_MAX_EXTENT),
                                  mvfxUpToBound(light->radiusBySize, 0.0f, MVFX_MAX_EXTENT)))
            : mvfx_success;
    if (lit != mvfx_success)
    {
        return lit;
    }
    mvfxResult shape = ribbon->ids == mvfx_ribbonNone ? mvfx_success : CheckRibbonShape(ribbon);
    if (shape != mvfx_success)
    {
        return shape;
    }
    mvfxResult width = mvfxCheckLifeCurve(&ribbon->width);
    if (width != mvfx_success)
    {
        return width;
    }
    mvfxResult color = IsWhite(&ribbon->color) ? mvfx_success : mvfxCheckGradient(&ribbon->color);
    if (color != mvfx_success)
    {
        return color;
    }
    return mvfxFirst(mvfxUpToBound(def->sprite.stretch, 0.0f, MVFX_MAX_EXTENT),
                     mvfxWithinBound(mesh->last < MVFX_MAX_MESH_INDEX));
}

// Whether an emitter's collision and colliders are valid, and a kill rule
// counting collisions has collisions to count.
static mvfxResult CheckCollisions(const mvfxEmitterDef* def)
{
    if (def->colliderCount < 0 || (def->colliderCount > 0 && def->colliders == nullptr))
    {
        return mvfx_errorInvalid;
    }
    mvfxResult collision = mvfxFirst(mvfxCheckCollision(&def->collision),
                                     mvfxWithinBound(def->colliderCount <= MVFX_MAX_COLLIDERS));
    if (collision != mvfx_success)
    {
        return collision;
    }
    for (int32_t c = 0; c < def->colliderCount; ++c)
    {
        mvfxResult collider = mvfxCheckCollider(&def->colliders[c]);
        if (collider != mvfx_success)
        {
            return collider;
        }
    }
    for (int32_t r = 0; r < def->killRuleCount; ++r)
    {
        if (def->killRules[r].kind == mvfx_killCollisions && !def->collision.enabled)
        {
            return mvfx_errorInvalid;
        }
    }
    mvfxResult outputs = CheckOutputs(def);
    if (outputs != mvfx_success)
    {
        return outputs;
    }
    for (int32_t k = 0; def->fixedBounds && k < 3; ++k)
    {
        if (!(def->boundsMin[k] <= def->boundsMax[k]))
        {
            return mvfx_errorInvalid;
        }
        mvfxResult bounds =
            mvfxFirst(mvfxBounded(def->boundsMin[k], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT),
                      mvfxBounded(def->boundsMax[k], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT));
        if (bounds != mvfx_success)
        {
            return bounds;
        }
    }
    // Events of known kinds, collisions only where they collide, tears
    // only where units tear, crossings only where they meet water.
    bool collides = def->collision.enabled || (def->events & mvfx_eventMaskCollision) == 0;
    bool tears = (def->unit.kind != mvfx_unitParticle && def->unit.tearStrain > 0.0f) ||
                 (def->events & mvfx_eventMaskTear) == 0;
    bool crosses = def->water.enabled || (def->events & mvfx_eventMaskWater) == 0;
    if (def->events > 31u || !collides || !tears || !crosses)
    {
        return mvfx_errorInvalid;
    }
    return def->events == 0 ? mvfx_success : mvfxCountUpTo(def->eventCapacity, 1, MVFX_MAX_EVENTS);
}

// Whether host modules are valid: a function each, a known stage.
static mvfxResult CheckHostModules(const mvfxHostModule* modules, int32_t count)
{
    if (count < 0 || (count > 0 && modules == nullptr))
    {
        return mvfx_errorInvalid;
    }
    if (count > MVFX_MAX_HOST_MODULES)
    {
        return mvfx_errorLimit;
    }
    for (int32_t m = 0; m < count; ++m)
    {
        if (modules[m].function == nullptr || modules[m].stage > mvfx_stageAfterMotion)
        {
            return mvfx_errorInvalid;
        }
    }
    return mvfx_success;
}

// Whether each parameter's defaults keep the sources reading them within
// their caps, for an effect whose emitters are valid.
static mvfxResult CheckDefaultsReach(const mvfxEffectDef* def)
{
    double reach[MVFX_MAX_PARAMETERS][4] = {{0}};
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        mvfxAddParameterReach(&def->emitters[i], reach);
    }
    for (int32_t p = 0; p < def->parameterCount; ++p)
    {
        mvfxResult within = mvfxCheckParameterReach(&def->parameters[4 * p], reach[p]);
        if (within != mvfx_success)
        {
            return within;
        }
    }
    return mvfx_success;
}

// Whether an emitter's sources are valid within their caps, and its
// frame, force and kill rule counts within their bounds.
static mvfxResult CheckSources(const mvfxEmitterDef* def, int32_t parameterCount,
                               const mvfxSourceCaps* caps)
{
    bool d = def->duration > 0.0f;
    const mvfxResult sources[] = {
        CheckLifetime(&def->lifetime, d, parameterCount),
        mvfxCheckVectorSource(&def->size, 3, d, parameterCount, caps->stored.size),
        mvfxCheckVectorSource(&def->color, 4, d, parameterCount, caps->stored.color),
        mvfxCheckScalarSource(&def->angle, true, d, parameterCount, MVFX_MAX_DRAW),
        mvfxCheckScalarSource(&def->spin, true, d, parameterCount, caps->stored.spin),
        mvfxCheckScalarSource(&def->motion.dragScale, true, d, parameterCount, MVFX_MAX_DRAW),
        mvfxCheckVectorSource(&def->orientation, 3, d, parameterCount, caps->any),
        mvfxCheckVectorSource(&def->angularVelocity, 3, d, parameterCount, caps->spin),
        mvfxWithinBound(def->frame.last < MVFX_MAX_FRAMES),
        mvfxWithinBound(def->forceCount <= MVFX_MAX_FORCES),
        mvfxWithinBound(def->killRuleCount <= MVFX_MAX_KILL_RULES),
    };
    for (size_t k = 0; k < sizeof(sources) / sizeof(sources[0]); ++k)
    {
        if (sources[k] != mvfx_success)
        {
            return sources[k];
        }
    }
    return mvfx_success;
}

// Whether an emitter's initial values are valid against its effect's
// parameters.
static mvfxResult CheckValues(const mvfxEmitterDef* def, int32_t parameterCount)
{
    bool d = def->duration > 0.0f;
    if (def->customCount < 0 || (def->customCount > 0 && def->custom == nullptr))
    {
        return mvfx_errorInvalid;
    }
    if (def->customCount > MVFX_MAX_CUSTOM_ATTRIBUTES)
    {
        return mvfx_errorLimit;
    }
    mvfxResult modules = mvfxCheckVelocityModules(&def->velocityModules);
    if (modules != mvfx_success)
    {
        return modules;
    }
    mvfxResult overLife = mvfxCheckOverLife(&def->overLife, def->custom, def->customCount);
    if (overLife != mvfx_success)
    {
        return overLife;
    }
    if (def->orientationMode > mvfx_orientationRandom ||
        (def->orientationMode == mvfx_orientationEuler &&
         def->orientation.kind == mvfx_sourceNone) ||
        def->frame.mode > mvfx_frameRandom || def->frame.first < 0 ||
        def->frame.first > def->frame.last || def->forceCount < 0 ||
        (def->forceCount > 0 && def->forces == nullptr) || def->killRuleCount < 0 ||
        (def->killRuleCount > 0 && def->killRules == nullptr))
    {
        return mvfx_errorInvalid;
    }
    // The sources' caps follow from the values over life, checked first.
    mvfxSourceCaps caps;
    mvfxSourceCapsOf(&def->overLife, def->custom, def->customCount, &caps);
    mvfxResult sources = CheckSources(def, parameterCount, &caps);
    if (sources != mvfx_success)
    {
        return sources;
    }
    for (int32_t f = 0; f < def->forceCount; ++f)
    {
        mvfxResult force = mvfxCheckForce(&def->forces[f], parameterCount);
        if (force != mvfx_success)
        {
            return force;
        }
    }
    for (int32_t r = 0; r < def->killRuleCount; ++r)
    {
        mvfxResult rule = mvfxCheckKillRule(&def->killRules[r]);
        if (rule != mvfx_success)
        {
            return rule;
        }
    }
    mvfxResult modulesAndCollisions =
        mvfxFirst(CheckHostModules(def->hostModules, def->hostModuleCount), CheckCollisions(def));
    if (modulesAndCollisions != mvfx_success)
    {
        return modulesAndCollisions;
    }
    for (int32_t i = 0; i < def->customCount; ++i)
    {
        const mvfxCustomAttribute* custom = &def->custom[i];
        if (custom->components < 1 || custom->components > 4)
        {
            return mvfx_errorInvalid;
        }
        mvfxResult initial = mvfxCheckVectorSource(&custom->initial, custom->components, d,
                                                   parameterCount, caps.custom[i]);
        if (initial != mvfx_success)
        {
            return initial;
        }
    }
    return mvfx_success;
}

// Whether a beam's point is finite and in range, its parameter one of the
// effect's.
static mvfxResult CheckBeamPoint(const mvfxBeamPoint* point, int32_t parameterCount)
{
    if (point->fromParameter && (point->parameter < 0 || point->parameter >= parameterCount))
    {
        return mvfx_errorInvalid;
    }
    return mvfxBoundedVector(point->point, MVFX_MAX_EXTENT);
}

// Whether a beam is valid: its shape in range, and nothing that would
// spawn or move its points but the beam.
static mvfxResult CheckBeam(const mvfxEmitterDef* def, int32_t parameterCount)
{
    const mvfxBeamDef* beam = &def->beam;
    if (!beam->enabled)
    {
        return mvfx_success;
    }
    bool alone = def->rate == 0.0f && def->burstCount == 0 && def->ratePerDistance == 0.0f &&
                 !def->eventSpawn.enabled && def->hostSpawnCapacity == 0 &&
                 !def->collision.enabled && def->forceCount == 0 && !def->contacts.enabled &&
                 !def->steering.enabled && !def->pairForce.enabled &&
                 (def->events & mvfx_eventMaskBirth) == 0;
    // The count first: it bounds the sum below.
    if (beam->segments > MVFX_MAX_BEAM_SEGMENTS)
    {
        return mvfx_errorLimit;
    }
    if (beam->segments < 1 || def->capacity < beam->segments + 1 || beam->controlCount < 0 ||
        beam->controlCount > 2 || !IsWithin(beam->noiseFrequency, 0.0f, 1000.0f) ||
        !IsWithin(beam->noiseSpeed, -1000.0f, 1000.0f) || !alone ||
        def->ribbon.ids != mvfx_ribbonOne || def->ribbon.pinHead)
    {
        return mvfx_errorInvalid;
    }
    mvfxResult shape = mvfxFirst(mvfxUpToBound(beam->noiseAmplitude, 0.0f, MVFX_MAX_EXTENT),
                                 mvfxFirst(CheckBeamPoint(&beam->start, parameterCount),
                                           CheckBeamPoint(&beam->end, parameterCount)));
    for (int32_t c = 0; shape == mvfx_success && c < beam->controlCount; ++c)
    {
        shape = CheckBeamPoint(&beam->controls[c], parameterCount);
    }
    return shape;
}

// Whether an emitter's spawning from events is valid against its effect:
// a source not its own that writes the kind, counts and a share in range.
static mvfxResult CheckEventSpawn(const mvfxEffectDef* def, int32_t i)
{
    const mvfxEventSpawn* spawn = &def->emitters[i].eventSpawn;
    if (!spawn->enabled)
    {
        return mvfx_success;
    }
    if (spawn->source < 0 || spawn->source >= def->emitterCount || spawn->source == i ||
        spawn->kind > mvfx_eventWater)
    {
        return mvfx_errorInvalid;
    }
    bool written = (def->emitters[spawn->source].events & (1u << spawn->kind)) != 0;
    if (!written || spawn->minCount < 0 || spawn->minCount > spawn->maxCount ||
        !(spawn->inheritVelocity >= 0.0f && spawn->inheritVelocity <= 1.0f) || spawn->capacity < 1)
    {
        return mvfx_errorInvalid;
    }
    return mvfxFirst(mvfxWithinBound(spawn->maxCount <= MVFX_MAX_EVENT_SPAWN),
                     mvfxWithinBound(spawn->capacity <= MVFX_MAX_EMITTER_CAPACITY));
}

// Whether an effect's quality levels are valid: a count in range, scales
// in range, and only its own emitters left out.
static mvfxResult CheckQualityLevels(const mvfxEffectDef* def)
{
    if (def->qualityLevelCount < 0 || (def->qualityLevelCount > 0 && def->qualityLevels == nullptr))
    {
        return mvfx_errorInvalid;
    }
    if (def->qualityLevelCount > MVFX_MAX_QUALITY_LEVELS)
    {
        return mvfx_errorLimit;
    }
    uint64_t own =
        def->emitterCount >= 64 ? ~(uint64_t)0 : ((uint64_t)1 << (uint32_t)def->emitterCount) - 1u;
    for (int32_t q = 0; q < def->qualityLevelCount; ++q)
    {
        const mvfxQualityLevel* level = &def->qualityLevels[q];
        if (!(level->capacityScale > 0.0f && level->capacityScale <= 1.0f) ||
            !IsWithin(level->rateScale, 0.0f, 1.0f) || (level->omitted & ~own) != 0)
        {
            return mvfx_errorInvalid;
        }
    }
    return mvfx_success;
}

mvfxResult mvfxCheckEffectDef(const mvfxEffectDef* def)
{
    if (def == nullptr || def->cookie != MVFX_EFFECT_DEF_COOKIE ||
        (def->emitterCount > 0 && def->emitters == nullptr) || def->emitterCount < 0 ||
        def->gasCount < 0 || (def->gasCount > 0 && def->gases == nullptr) || def->waterCount < 0 ||
        (def->waterCount > 0 && def->waters == nullptr) || def->parameterCount < 0 ||
        (def->parameterCount > 0 && def->parameters == nullptr))
    {
        return mvfx_errorInvalid;
    }
    // The counts bound the sum below and the arrays the checks fill.
    if (def->emitterCount > MVFX_MAX_EMITTERS || def->gasCount > MVFX_MAX_GASES ||
        def->waterCount > MVFX_MAX_WATERS || def->parameterCount > MVFX_MAX_PARAMETERS)
    {
        return mvfx_errorLimit;
    }
    if (def->emitterCount + def->gasCount + def->waterCount < 1)
    {
        return mvfx_errorInvalid;
    }
    for (int32_t i = 0; i < 4 * def->parameterCount; ++i)
    {
        if (!isfinite(def->parameters[i]))
        {
            return mvfx_errorInvalid;
        }
    }
    for (int32_t g = 0; g < def->gasCount; ++g)
    {
        mvfxResult gas = mvfxCheckGasDef(&def->gases[g]);
        if (gas != mvfx_success)
        {
            return gas;
        }
    }
    for (int32_t w = 0; w < def->waterCount; ++w)
    {
        mvfxResult water = mvfxCheckWaterDef(&def->waters[w]);
        if (water != mvfx_success)
        {
            return water;
        }
    }
    mvfxResult levels = CheckQualityLevels(def);
    if (levels != mvfx_success)
    {
        return levels;
    }
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        const mvfxEmitterDef* emitter = &def->emitters[i];
        mvfxResult checked = CheckEmitterDef(emitter);
        checked = checked != mvfx_success ? checked : CheckValues(emitter, def->parameterCount);
        checked = checked != mvfx_success ? checked : CheckEventSpawn(def, i);
        checked = checked != mvfx_success ? checked : CheckBeam(emitter, def->parameterCount);
        checked = checked != mvfx_success
                      ? checked
                      : mvfxCheckWaterCoupling(&emitter->water, def->waters, def->waterCount);
        if (checked != mvfx_success)
        {
            return checked;
        }
    }
    mvfxResult reach = CheckDefaultsReach(def);
    if (reach != mvfx_success)
    {
        return reach;
    }
    mvfxResult gases = mvfxCheckEffectGases(def);
    if (gases != mvfx_success)
    {
        return gases;
    }
    int32_t order[MVFX_MAX_EMITTERS];
    return mvfxRequire(mvfxOrderEmitters(def, order));
}
