// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Effect defs checked and compiled: every field against its range, then
// each emitter's times in ticks, its rate curve baked with its integral,
// its bursts, and its attribute plan laid out.

#include "effect.h"

#include "allocator.h"
#include "collide.h"
#include "effect_gas.h"
#include "elementary.h"
#include "force.h"
#include "hull.h"
#include "kill.h"
#include "refusal.h"

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
        .rigid = {.sleepSpeed = 0.05f, .sleepTime = 0.5f},
        .liquid = {.spacing = 0.1f,
                   .kernelScale = 2.0f,
                   .substeps = 4,
                   .iterations = 3,
                   .relaxation = 0.01f,
                   .tensileDistance = 0.2f,
                   .tensilePower = 4,
                   .viscosity = 0.01f,
                   .maxNeighbors = 64},
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
    BakeRateCurve(&def->rateCurve, 1.0f, &baked);
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
    mvfxResult interval = burst->cycles == 1 ? mvfx_success
                                             : mvfxBounded(burst->interval, MVFX_MIN_DURATION,
                                                           MVFX_MAX_LIFETIME);
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
    mvfxResult duration = def->duration == 0.0f ? mvfx_success
                                                : mvfxBounded(def->duration, MVFX_MIN_DURATION,
                                                              MVFX_MAX_LIFETIME);
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
    return lifetime->range.min > 0.0f
               ? mvfxWithinBound(lifetime->range.max <= MVFX_MAX_LIFETIME)
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

// Compiles a ribbon's tessellation, texture and facing: for a bend angle,
// the cosine each count of pieces allows a segment's turn down to.
static void CompileRibbonShape(const mvfxRibbonDef* ribbon, mvfxCompiledEmitter* out)
{
    out->ribbonPieces = ribbon->pieces;
    // A ribbon's fields are checked only when it is drawn: an emitter
    // without one bends nothing, whatever its bend angle.
    bool bends = ribbon->ids != mvfx_ribbonNone && ribbon->bendAngle > 0.0f;
    for (int32_t k = 1; k <= MVFX_MAX_RIBBON_PIECES; ++k)
    {
        float turn = bends ? fminf(ribbon->bendAngle * (float)k, 3.14159265f) : 0.0f;
        float sine = 0.0f;
        float cosine = 1.0f;
        mvfxSinCos(turn, &sine, &cosine);
        out->ribbonCosLimits[k - 1] = bends ? cosine : -2.0f;
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
static mvfxResult CheckOutputs(const mvfxEmitterDef* def)
{
    const mvfxFrameStart* mesh = &def->meshIndex;
    const mvfxLightDef* light = &def->light;
    const mvfxRibbonDef* ribbon = &def->ribbon;
    if ((light->enabled && (light->every < 1 || light->every > 65536)) ||
        ribbon->ids > mvfx_ribbonPerSource ||
        def->sprite.billboard > mvfx_billboardDirectional || def->sortMode > mvfx_sortFrontToBack ||
        def->sortMeasure > mvfx_sortByDepth || mesh->mode > mvfx_frameRandom || mesh->first < 0 ||
        mesh->first > mesh->last)
    {
        return mvfx_errorInvalid;
    }
    mvfxResult lit = light->enabled
                         ? mvfxFirst(mvfxUpToBound(light->intensity, 0.0f, MVFX_MAX_EXTENT),
                                     mvfxFirst(mvfxUpToBound(light->radius, 0.0f, MVFX_MAX_EXTENT),
                                               mvfxUpToBound(light->radiusBySize, 0.0f,
                                                             MVFX_MAX_EXTENT)))
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
    mvfxResult collision =
        mvfxFirst(mvfxCheckCollision(&def->collision),
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
    return def->events == 0 ? mvfx_success
                            : mvfxCountUpTo(def->eventCapacity, 1, MVFX_MAX_EVENTS);
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

// Raises reach to the share of its cap of the ends of each source of an
// emitter's values that reads a parameter's component
// (mvfxCheckParameterReach), for an emitter whose values over life are
// valid.
static void AddParameterReach(const mvfxEmitterDef* def, double reach[MVFX_MAX_PARAMETERS][4])
{
    mvfxSourceCaps caps;
    mvfxSourceCapsOf(&def->overLife, def->custom, def->customCount, &caps);
    const mvfxScalarSource* scalars[4] = {&def->lifetime, &def->angle, &def->spin,
                                          &def->motion.dragScale};
    const float scalarCaps[4] = {MVFX_MAX_DRAW, MVFX_MAX_DRAW, caps.stored.spin, MVFX_MAX_DRAW};
    const mvfxVectorSource* vectors[4] = {&def->size, &def->color, &def->orientation,
                                          &def->angularVelocity};
    const float* vectorCaps[4] = {caps.stored.size, caps.stored.color, caps.any, caps.spin};
    static const int32_t components[4] = {3, 4, 3, 3};
    for (int32_t s = 0; s < 4; ++s)
    {
        mvfxScalarParameterReach(scalars[s], scalarCaps[s], reach);
        mvfxVectorParameterReach(vectors[s], components[s], vectorCaps[s], reach);
    }
    for (int32_t i = 0; i < def->customCount; ++i)
    {
        mvfxVectorParameterReach(&def->custom[i].initial, def->custom[i].components, caps.custom[i],
                                 reach);
    }
}

// Whether each parameter's defaults keep the sources reading them within
// their caps, for an effect whose emitters are valid.
static mvfxResult CheckDefaultsReach(const mvfxEffectDef* def)
{
    double reach[MVFX_MAX_PARAMETERS][4] = {{0}};
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        AddParameterReach(&def->emitters[i], reach);
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
    const mvfxResult sources[] = {
        CheckLifetime(&def->lifetime, d, parameterCount),
        mvfxCheckVectorSource(&def->size, 3, d, parameterCount, caps.stored.size),
        mvfxCheckVectorSource(&def->color, 4, d, parameterCount, caps.stored.color),
        mvfxCheckScalarSource(&def->angle, true, d, parameterCount, MVFX_MAX_DRAW),
        mvfxCheckScalarSource(&def->spin, true, d, parameterCount, caps.stored.spin),
        mvfxCheckScalarSource(&def->motion.dragScale, true, d, parameterCount, MVFX_MAX_DRAW),
        mvfxCheckVectorSource(&def->orientation, 3, d, parameterCount, caps.any),
        mvfxCheckVectorSource(&def->angularVelocity, 3, d, parameterCount, caps.spin),
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
    if (beam->segments < 1 || def->capacity < beam->segments + 1 || beam->controlCount < 0 ||
        beam->controlCount > 2 || !IsWithin(beam->noiseFrequency, 0.0f, 1000.0f) ||
        !IsWithin(beam->noiseSpeed, -1000.0f, 1000.0f) || !alone ||
        def->ribbon.ids != mvfx_ribbonOne || def->ribbon.pinHead)
    {
        return mvfx_errorInvalid;
    }
    mvfxResult shape =
        mvfxFirst(mvfxFirst(mvfxWithinBound(beam->segments <= MVFX_MAX_BEAM_SEGMENTS),
                            mvfxUpToBound(beam->noiseAmplitude, 0.0f, MVFX_MAX_EXTENT)),
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
        !(spawn->inheritVelocity >= 0.0f && spawn->inheritVelocity <= 1.0f) ||
        spawn->capacity < 1)
    {
        return mvfx_errorInvalid;
    }
    return mvfxFirst(mvfxWithinBound(spawn->maxCount <= MVFX_MAX_EVENT_SPAWN),
                     mvfxWithinBound(spawn->capacity <= MVFX_MAX_EMITTER_CAPACITY));
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
        (def->parameterCount > 0 && def->parameters == nullptr) ||
        def->emitterCount + def->gasCount + def->waterCount < 1)
    {
        return mvfx_errorInvalid;
    }
    // The counts bound the arrays the checks below fill.
    if (def->emitterCount > MVFX_MAX_EMITTERS || def->gasCount > MVFX_MAX_GASES ||
        def->waterCount > MVFX_MAX_WATERS || def->parameterCount > MVFX_MAX_PARAMETERS)
    {
        return mvfx_errorLimit;
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
    return mvfxRequire(OrderEmitters(def, order));
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
                def->angularVelocity.kind != mvfx_sourceNone || def->rigid.enabled;
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
    bool pinned =
        def->unit.kind != mvfx_unitParticle &&
        (def->unit.pinFirst || (def->unit.kind == mvfx_unitSheet && def->unit.pinFirstRow));
    Store(plan, pinned, mvfx_attributeAnchorX, 3);
    mvfxAttachKind attach =
        def->unit.kind != mvfx_unitParticle ? def->unit.attach : mvfx_attachNone;
    bool framed = attach == mvfx_attachHost || attach == mvfx_attachParticles;
    Store(plan, framed, mvfx_attributeAttachX, 3);
    Store(plan, framed, mvfx_attributeAttachRotationX, 4);
    Store(plan, framed, mvfx_attributeAttachSource, 1);
    Store(plan, attach != mvfx_attachNone, mvfx_attributeAttachFlags, 1);
    Store(plan, def->unit.kind != mvfx_unitParticle && def->unit.tearStrain > 0.0f,
          mvfx_attributeTorn, 1);
    Store(plan, def->collision.enabled && def->collision.sceneQueries, mvfx_attributeScenePointX,
          6);
    Store(plan, def->rigid.enabled && def->rigid.sleepSpeed > 0.0f, mvfx_attributeSleep, 4);
    Store(plan, def->liquid.enabled && def->liquid.solver == mvfx_liquidGrid, mvfx_attributeAffine,
          9);
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
    out->gasSplat = def->gasSplat;
    out->water = def->water;
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
        .unitWidth = 1,
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

// Compiles an effect's gases and water surfaces into the room its block
// holds for them.
static void CompileFields(const mvfxEffectDef* def, mvfxEffect* effect)
{
    for (int32_t g = 0; g < def->gasCount; ++g)
    {
        effect->gases[g] = mvfxCompileGas(&def->gases[g]);
        bool surface = def->gases[g].kind == mvfx_gasSurface;
        effect->gases[g].emitter = surface ? def->gases[g].surface.emitter : mvfxLiquidOf(def, g);
    }
    effect->gasCount = def->gasCount;
    for (int32_t w = 0; w < def->waterCount; ++w)
    {
        effect->waters[w] = mvfxCompileWater(&def->waters[w]);
    }
    effect->waterCount = def->waterCount;
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
    size_t gasesAt = mvfxLayoutAdd(&layout, (size_t)def->gasCount, sizeof(mvfxCompiledGas),
                                   alignof(mvfxCompiledGas));
    size_t watersAt = mvfxLayoutAdd(&layout, (size_t)def->waterCount, sizeof(mvfxCompiledWater),
                                    alignof(mvfxCompiledWater));
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
        .gases = (mvfxCompiledGas*)(block + gasesAt),
        .waters = (mvfxCompiledWater*)(block + watersAt),
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
    for (int32_t i = 0; i < def->emitterCount; ++i)
    {
        AddParameterReach(&def->emitters[i], effect->parameterReach);
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
    CompileFields(def, effect);
    return effect;
}

void mvfxReleaseEffect(mvfxEffect* effect, const mvfxAllocator* allocator)
{
    for (int32_t i = 0; i < effect->emitterCount; ++i)
    {
        mvfxRelease(allocator, effect->emitters[i].hull, sizeof(mvfxHull), alignof(mvfxHull));
    }
    mvfxRelease(allocator, effect, effect->blockBytes, effect->blockAlignment);
}
