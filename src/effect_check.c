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
// past MVFX_MAX_SPAWN_RATE once times rate; naming the field it refuses
// below the emitter's path at.
static mvfxResult CheckRateCurve(const mvfxFieldPath* at, const mvfxEmitterDef* def)
{
    if (def->rateCurve.keyCount == 0)
    {
        return mvfx_success;
    }
    if (def->duration <= 0.0f)
    {
        return mvfxRefuseField(at, MVFX_TAG_EMITTER_DEF_RATE_CURVE, mvfx_errorInvalid);
    }
    mvfxFieldPath curveAt = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_RATE_CURVE, -1);
    mvfxResult curve = mvfxCheckCurve(&curveAt, &def->rateCurve, true);
    if (curve != mvfx_success)
    {
        return curve;
    }
    mvfxRateCurve baked;
    mvfxBakeRateCurve(&def->rateCurve, 1.0f, &baked);
    if (!mvfxIsCurveTableNonNegative(&baked.table))
    {
        return mvfxRefuseHere(&curveAt, mvfx_errorInvalid);
    }
    for (int32_t i = 0; i < MVFX_CURVE_SAMPLES; ++i)
    {
        if (!((double)baked.table.samples[i] * (double)def->rate <= (double)MVFX_MAX_SPAWN_RATE))
        {
            return mvfxRefuseHere(&curveAt, mvfx_errorLimit);
        }
    }
    return mvfx_success;
}

static mvfxResult CheckBurst(const mvfxFieldPath* at, const mvfxBurst* burst)
{
    uint32_t refused = burst->minCount < 0 || burst->minCount > burst->maxCount
                           ? MVFX_TAG_BURST_MIN_COUNT
                       : burst->cycles < 0                         ? MVFX_TAG_BURST_CYCLES
                       : !IsWithin(burst->probability, 0.0f, 1.0f) ? MVFX_TAG_BURST_PROBABILITY
                                                                   : 0;
    if (refused != 0)
    {
        return mvfxRefuseField(at, refused, mvfx_errorInvalid);
    }
    mvfxResult valid = mvfxRefuseField(at, MVFX_TAG_BURST_TIME,
                                       mvfxUpToBound(burst->time, 0.0f, MVFX_MAX_LIFETIME));
    valid =
        valid != mvfx_success || burst->cycles == 1
            ? valid
            : mvfxRefuseField(at, MVFX_TAG_BURST_INTERVAL,
                              mvfxBounded(burst->interval, MVFX_MIN_DURATION, MVFX_MAX_LIFETIME));
    return valid != mvfx_success
               ? valid
               : mvfxRefuseField(at, MVFX_TAG_BURST_MAX_COUNT,
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
static mvfxResult CheckMotion(const mvfxFieldPath* at, const mvfxMotion* motion)
{
    if (!mvfxIsKind(motion->gravitySpace, mvfx_spaceLocal))
    {
        return mvfxRefuseField(at, MVFX_TAG_MOTION_GRAVITY_SPACE, mvfx_errorInvalid);
    }
    mvfxResult valid =
        mvfxRefuseVector(at, MVFX_TAG_MOTION_GRAVITY, motion->gravity, MVFX_MAX_EXTENT);
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(at, MVFX_TAG_MOTION_DRAG,
                                  mvfxUpToBound(motion->drag, 0.0f, MVFX_MAX_DRAG));
    return valid != mvfx_success
               ? valid
               : mvfxRefuseField(at, MVFX_TAG_MOTION_ANGULAR_DRAG,
                                 mvfxUpToBound(motion->angularDrag, 0.0f, MVFX_MAX_DRAG));
}

// The field of an emitter def out of a range that no named bound covers,
// 0 for none.
static uint32_t RefusedEmitterField(const mvfxEmitterDef* def)
{
    if (def->capacity < 1)
    {
        return MVFX_TAG_EMITTER_DEF_CAPACITY;
    }
    if (def->loops < 0)
    {
        return MVFX_TAG_EMITTER_DEF_LOOPS;
    }
    if (def->burstCount < 0 || (def->burstCount > 0 && def->bursts == nullptr))
    {
        return MVFX_TAG_EMITTER_DEF_BURSTS;
    }
    if (!mvfxIsKind(def->space, mvfx_spaceLocal))
    {
        return MVFX_TAG_EMITTER_DEF_SPACE;
    }
    if (!IsWithin(def->inheritVelocity, -10.0f, 10.0f) ||
        (def->space == mvfx_spaceLocal && def->inheritVelocity != 0.0f))
    {
        return MVFX_TAG_EMITTER_DEF_INHERIT_VELOCITY;
    }
    if (def->hostSpawnCapacity < 0 || def->hostSpawnCapacity > def->capacity)
    {
        return MVFX_TAG_EMITTER_DEF_HOST_SPAWN_CAPACITY;
    }
    return def->rateCurve.keyCount < 0 ? MVFX_TAG_EMITTER_DEF_RATE_CURVE : 0;
}

static mvfxResult CheckEmitterDef(const mvfxFieldPath* at, const mvfxEmitterDef* def)
{
    if (def->cookie != MVFX_EMITTER_DEF_COOKIE)
    {
        return mvfxRefuseHere(at, mvfx_errorInvalid);
    }
    uint32_t refused = RefusedEmitterField(def);
    if (refused != 0)
    {
        return mvfxRefuseField(at, refused, mvfx_errorInvalid);
    }
    mvfxResult valid = mvfxRefuseField(at, MVFX_TAG_EMITTER_DEF_CAPACITY,
                                       mvfxWithinBound(def->capacity <= MVFX_MAX_EMITTER_CAPACITY));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(at, MVFX_TAG_EMITTER_DEF_RATE,
                                  mvfxUpToBound(def->rate, 0.0f, MVFX_MAX_SPAWN_RATE));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(at, MVFX_TAG_EMITTER_DEF_START_DELAY,
                                  mvfxUpToBound(def->startDelay, 0.0f, MVFX_MAX_LIFETIME));
    valid = valid != mvfx_success || def->duration == 0.0f
                ? valid
                : mvfxRefuseField(at, MVFX_TAG_EMITTER_DEF_DURATION,
                                  mvfxBounded(def->duration, MVFX_MIN_DURATION, MVFX_MAX_LIFETIME));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(at, MVFX_TAG_EMITTER_DEF_BURSTS,
                                  mvfxWithinBound(def->burstCount <= MVFX_MAX_BURSTS));
    valid =
        valid != mvfx_success
            ? valid
            : mvfxRefuseField(at, MVFX_TAG_EMITTER_DEF_RATE_PER_DISTANCE,
                              mvfxUpToBound(def->ratePerDistance, 0.0f, MVFX_MAX_DISTANCE_RATE));
    mvfxFieldPath motion = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_MOTION, -1);
    valid = valid != mvfx_success ? valid : CheckMotion(&motion, &def->motion);
    for (int32_t b = 0; valid == mvfx_success && b < def->burstCount; ++b)
    {
        mvfxFieldPath burst = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_BURSTS, b);
        valid = CheckBurst(&burst, &def->bursts[b]);
    }
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(at, MVFX_TAG_EMITTER_DEF_BURSTS,
                                  mvfxWithinBound(BurstRate(def) <= (double)MVFX_MAX_SPAWN_RATE));
    valid = valid != mvfx_success ? valid : CheckRateCurve(at, def);
    mvfxFieldPath shape = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_SHAPE, -1);
    valid = valid != mvfx_success ? valid : mvfxCheckShape(&shape, &def->shape);
    mvfxFieldPath velocity = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_VELOCITY, -1);
    return valid != mvfx_success ? valid : mvfxCheckVelocity(&velocity, &def->velocity);
}

// Whether a lifetime is valid: required, and a range of lives above 0
// (else mvfx_errorInvalid) and at most MVFX_MAX_LIFETIME (else
// mvfx_errorLimit); naming the field it refuses below its path at.
static mvfxResult CheckLifetime(const mvfxFieldPath* at, const mvfxScalarSource* lifetime,
                                bool hasDuration, int32_t parameterCount)
{
    mvfxResult source =
        mvfxCheckScalarSource(at, lifetime, false, hasDuration, parameterCount, MVFX_MAX_DRAW);
    if (source != mvfx_success || lifetime->kind != mvfx_sourceRange)
    {
        return source;
    }
    mvfxFieldPath range = mvfxFieldIn(at, MVFX_TAG_SCALAR_SOURCE_RANGE, -1);
    return lifetime->range.min > 0.0f
               ? mvfxRefuseField(&range, MVFX_TAG_RANGE_MAX,
                                 mvfxWithinBound(lifetime->range.max <= MVFX_MAX_LIFETIME))
               : mvfxRefuseField(&range, MVFX_TAG_RANGE_MIN, mvfx_errorInvalid);
}

// The field of a ribbon's tessellation, texture or facing out of a range
// that no named bound covers, 0 for none.
static uint32_t RefusedRibbonField(const mvfxRibbonDef* ribbon)
{
    const float* n = ribbon->normal;
    if (ribbon->pieces < 1)
    {
        return MVFX_TAG_RIBBON_DEF_PIECES;
    }
    if (!IsWithin(ribbon->bendAngle, 0.0f, 3.14159265f))
    {
        return MVFX_TAG_RIBBON_DEF_BEND_ANGLE;
    }
    if (!mvfxIsKind(ribbon->texture, mvfx_ribbonTileByDistance))
    {
        return MVFX_TAG_RIBBON_DEF_TEXTURE;
    }
    if (!mvfxIsKind(ribbon->facing, mvfx_ribbonFaceNormal))
    {
        return MVFX_TAG_RIBBON_DEF_FACING;
    }
    if (n[0] == 0.0f && n[1] == 0.0f && n[2] == 0.0f)
    {
        return MVFX_TAG_RIBBON_DEF_NORMAL;
    }
    return ribbon->pinHead && ribbon->ids != mvfx_ribbonOne ? MVFX_TAG_RIBBON_DEF_PIN_HEAD : 0;
}

// Whether a ribbon's tessellation, texture and facing are valid.
static mvfxResult CheckRibbonShape(const mvfxFieldPath* at, const mvfxRibbonDef* ribbon)
{
    uint32_t refused = RefusedRibbonField(ribbon);
    if (refused != 0)
    {
        return mvfxRefuseField(at, refused, mvfx_errorInvalid);
    }
    mvfxResult valid = mvfxRefuseField(at, MVFX_TAG_RIBBON_DEF_PIECES,
                                       mvfxWithinBound(ribbon->pieces <= MVFX_MAX_RIBBON_PIECES));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(at, MVFX_TAG_RIBBON_DEF_TILE_LENGTH,
                                  mvfxPositiveUpTo(ribbon->tileLength, MVFX_MAX_EXTENT));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(at, MVFX_TAG_RIBBON_DEF_SCROLL,
                                  mvfxBounded(ribbon->scroll, -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseVector(at, MVFX_TAG_RIBBON_DEF_NORMAL, ribbon->normal, MVFX_MAX_EXTENT);
    return valid != mvfx_success
               ? valid
               : mvfxRefuseField(at, MVFX_TAG_RIBBON_DEF_BREAK_DISTANCE,
                                 mvfxUpToBound(ribbon->breakDistance, 0.0f, MVFX_MAX_EXTENT));
}

// The field of an emitter's outputs out of a range that no named bound
// covers, 0 for none: one of the light, the ribbon, the sprite or the
// mesh index, whose path holds; the emitter's own otherwise.
static uint32_t RefusedOutput(const mvfxEmitterDef* def, uint32_t* holder)
{
    const mvfxFrameStart* mesh = &def->meshIndex;
    const mvfxLightDef* light = &def->light;
    *holder = 0;
    if (light->enable && (light->every < 1 || light->every > 65536))
    {
        *holder = MVFX_TAG_EMITTER_DEF_LIGHT;
        return MVFX_TAG_LIGHT_DEF_EVERY;
    }
    if (!mvfxIsKind(def->ribbon.ids, mvfx_ribbonPerSource))
    {
        *holder = MVFX_TAG_EMITTER_DEF_RIBBON;
        return MVFX_TAG_RIBBON_DEF_IDS;
    }
    if (!mvfxIsKind(def->sprite.billboard, mvfx_billboardDirectional))
    {
        *holder = MVFX_TAG_EMITTER_DEF_SPRITE;
        return MVFX_TAG_SPRITE_DEF_BILLBOARD;
    }
    if (!mvfxIsKind(def->sortMode, mvfx_sortFrontToBack))
    {
        return MVFX_TAG_EMITTER_DEF_SORT_MODE;
    }
    if (!mvfxIsKind(def->sortMeasure, mvfx_sortByDepth))
    {
        return MVFX_TAG_EMITTER_DEF_SORT_MEASURE;
    }
    *holder = MVFX_TAG_EMITTER_DEF_MESH_INDEX;
    if (!mvfxIsKind(mesh->mode, mvfx_frameRandom))
    {
        return MVFX_TAG_FRAME_START_MODE;
    }
    return mesh->first < 0 || mesh->first > mesh->last ? MVFX_TAG_FRAME_START_FIRST : 0;
}

// Whether an emitter's mesh index, lights, sorting and ribbons are valid.
static mvfxResult CheckOutputs(const mvfxFieldPath* at, const mvfxEmitterDef* def)
{
    const mvfxLightDef* light = &def->light;
    const mvfxRibbonDef* ribbon = &def->ribbon;
    uint32_t holder = 0;
    uint32_t refused = RefusedOutput(def, &holder);
    if (refused != 0)
    {
        mvfxFieldPath in = holder != 0 ? mvfxFieldIn(at, holder, -1) : *at;
        return mvfxRefuseField(&in, refused, mvfx_errorInvalid);
    }
    mvfxFieldPath lightAt = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_LIGHT, -1);
    const float lights[3] = {light->intensity, light->radius, light->radiusBySize};
    const uint32_t lightTags[3] = {MVFX_TAG_LIGHT_DEF_INTENSITY, MVFX_TAG_LIGHT_DEF_RADIUS,
                                   MVFX_TAG_LIGHT_DEF_RADIUS_BY_SIZE};
    mvfxResult valid = mvfx_success;
    for (int32_t k = 0; light->enable && valid == mvfx_success && k < 3; ++k)
    {
        valid = mvfxRefuseField(&lightAt, lightTags[k],
                                mvfxUpToBound(lights[k], 0.0f, MVFX_MAX_EXTENT));
    }
    mvfxFieldPath ribbonAt = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_RIBBON, -1);
    valid = valid != mvfx_success || ribbon->ids == mvfx_ribbonNone
                ? valid
                : CheckRibbonShape(&ribbonAt, ribbon);
    mvfxFieldPath width = mvfxFieldIn(&ribbonAt, MVFX_TAG_RIBBON_DEF_WIDTH, -1);
    valid = valid != mvfx_success ? valid : mvfxCheckLifeCurve(&width, &ribbon->width);
    mvfxFieldPath color = mvfxFieldIn(&ribbonAt, MVFX_TAG_RIBBON_DEF_COLOR, -1);
    valid = valid != mvfx_success || IsWhite(&ribbon->color)
                ? valid
                : mvfxCheckGradient(&color, &ribbon->color);
    mvfxFieldPath sprite = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_SPRITE, -1);
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(&sprite, MVFX_TAG_SPRITE_DEF_STRETCH,
                                  mvfxUpToBound(def->sprite.stretch, 0.0f, MVFX_MAX_EXTENT));
    mvfxFieldPath mesh = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_MESH_INDEX, -1);
    return valid != mvfx_success
               ? valid
               : mvfxRefuseField(&mesh, MVFX_TAG_FRAME_START_LAST,
                                 mvfxWithinBound(def->meshIndex.last < MVFX_MAX_MESH_INDEX));
}

// Whether an emitter's fixed bounds are valid: each axis's ends in order
// and within MVFX_MAX_EXTENT.
static mvfxResult CheckBounds(const mvfxFieldPath* at, const mvfxEmitterDef* def)
{
    for (int32_t k = 0; def->fixedBounds && k < 3; ++k)
    {
        if (!(def->boundsMin[k] <= def->boundsMax[k]))
        {
            return mvfxRefuseItem(at, MVFX_TAG_EMITTER_DEF_BOUNDS_MIN, k, mvfx_errorInvalid);
        }
        mvfxResult bounds =
            mvfxRefuseItem(at, MVFX_TAG_EMITTER_DEF_BOUNDS_MIN, k,
                           mvfxBounded(def->boundsMin[k], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT));
        bounds =
            bounds != mvfx_success
                ? bounds
                : mvfxRefuseItem(at, MVFX_TAG_EMITTER_DEF_BOUNDS_MAX, k,
                                 mvfxBounded(def->boundsMax[k], -MVFX_MAX_EXTENT, MVFX_MAX_EXTENT));
        if (bounds != mvfx_success)
        {
            return bounds;
        }
    }
    return mvfx_success;
}

// Whether an emitter's collision and colliders are valid, and a kill rule
// counting collisions has collisions to count.
static mvfxResult CheckCollisions(const mvfxFieldPath* at, const mvfxEmitterDef* def)
{
    if (def->colliderCount < 0 || (def->colliderCount > 0 && def->colliders == nullptr))
    {
        return mvfxRefuseField(at, MVFX_TAG_EMITTER_DEF_COLLIDERS, mvfx_errorInvalid);
    }
    mvfxFieldPath collisionAt = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_COLLISION, -1);
    mvfxResult valid = mvfxCheckCollision(&collisionAt, &def->collision);
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(at, MVFX_TAG_EMITTER_DEF_COLLIDERS,
                                  mvfxWithinBound(def->colliderCount <= MVFX_MAX_COLLIDERS));
    for (int32_t c = 0; valid == mvfx_success && c < def->colliderCount; ++c)
    {
        mvfxFieldPath collider = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_COLLIDERS, c);
        valid = mvfxCheckCollider(&collider, &def->colliders[c]);
    }
    for (int32_t r = 0; valid == mvfx_success && r < def->killRuleCount; ++r)
    {
        if (def->killRules[r].kind == mvfx_killCollisions && !def->collision.enable)
        {
            mvfxFieldPath rule = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_KILL_RULES, r);
            valid = mvfxRefuseField(&rule, MVFX_TAG_KILL_RULE_KIND, mvfx_errorInvalid);
        }
    }
    valid = valid != mvfx_success ? valid : CheckOutputs(at, def);
    valid = valid != mvfx_success ? valid : CheckBounds(at, def);
    if (valid != mvfx_success)
    {
        return valid;
    }
    // Events of known kinds, collisions only where they collide, tears
    // only where units tear, crossings only where they meet water.
    bool collides = def->collision.enable || (def->events & mvfx_eventMaskCollision) == 0;
    bool tears = (def->unit.kind != mvfx_unitParticle && def->unit.tearStrain > 0.0f) ||
                 (def->events & mvfx_eventMaskTear) == 0;
    bool crosses = def->water.enable || (def->events & mvfx_eventMaskWater) == 0;
    if (def->events > 31u || !collides || !tears || !crosses)
    {
        return mvfxRefuseField(at, MVFX_TAG_EMITTER_DEF_EVENTS, mvfx_errorInvalid);
    }
    return def->events == 0
               ? mvfx_success
               : mvfxRefuseField(at, MVFX_TAG_EMITTER_DEF_EVENT_CAPACITY,
                                 mvfxCountUpTo(def->eventCapacity, 1, MVFX_MAX_EVENTS));
}

// Whether host modules are valid: a function each, a known stage.
static mvfxResult CheckHostModules(const mvfxFieldPath* at, const mvfxHostModule* modules,
                                   int32_t count)
{
    if (count < 0 || (count > 0 && modules == nullptr))
    {
        return mvfxRefuseField(at, MVFX_TAG_EMITTER_DEF_HOST_MODULES, mvfx_errorInvalid);
    }
    if (count > MVFX_MAX_HOST_MODULES)
    {
        return mvfxRefuseField(at, MVFX_TAG_EMITTER_DEF_HOST_MODULES, mvfx_errorLimit);
    }
    for (int32_t m = 0; m < count; ++m)
    {
        mvfxFieldPath module = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_HOST_MODULES, m);
        if (modules[m].function == nullptr)
        {
            return mvfxRefuseField(&module, MVFX_TAG_HOST_MODULE_FUNCTION, mvfx_errorInvalid);
        }
        if (!mvfxIsKind(modules[m].stage, mvfx_stageAfterMotion))
        {
            return mvfxRefuseField(&module, MVFX_TAG_HOST_MODULE_STAGE, mvfx_errorInvalid);
        }
    }
    return mvfx_success;
}

// Whether each parameter's defaults keep the sources reading them within
// their caps, for an effect whose emitters are valid.
static mvfxResult CheckDefaultsReach(const mvfxFieldPath* root, const mvfxEffectDef* def)
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
            return mvfxRefuseItem(root, MVFX_TAG_EFFECT_DEF_PARAMETERS, p, within);
        }
    }
    return mvfx_success;
}

// Whether an emitter's sources are valid within their caps, and its
// frame, force and kill rule counts within their bounds.
static mvfxResult CheckSources(const mvfxFieldPath* at, const mvfxEmitterDef* def,
                               int32_t parameterCount, const mvfxSourceCaps* caps)
{
    bool d = def->duration > 0.0f;
    mvfxFieldPath lifetime = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_LIFETIME, -1);
    mvfxFieldPath size = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_SIZE, -1);
    mvfxFieldPath color = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_COLOR, -1);
    mvfxFieldPath angle = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_ANGLE, -1);
    mvfxFieldPath spin = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_SPIN, -1);
    mvfxFieldPath motion = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_MOTION, -1);
    mvfxFieldPath dragScale = mvfxFieldIn(&motion, MVFX_TAG_MOTION_DRAG_SCALE, -1);
    mvfxFieldPath orientation = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_ORIENTATION, -1);
    mvfxFieldPath angular = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_ANGULAR_VELOCITY, -1);
    mvfxFieldPath frame = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_FRAME, -1);
    mvfxResult valid = CheckLifetime(&lifetime, &def->lifetime, d, parameterCount);
    valid = valid != mvfx_success
                ? valid
                : mvfxCheckVectorSource(&size, &def->size, 3, d, parameterCount, caps->stored.size);
    valid = valid != mvfx_success ? valid
                                  : mvfxCheckVectorSource(&color, &def->color, 4, d, parameterCount,
                                                          caps->stored.color);
    valid = valid != mvfx_success ? valid
                                  : mvfxCheckScalarSource(&angle, &def->angle, true, d,
                                                          parameterCount, MVFX_MAX_DRAW);
    valid = valid != mvfx_success ? valid
                                  : mvfxCheckScalarSource(&spin, &def->spin, true, d,
                                                          parameterCount, caps->stored.spin);
    valid = valid != mvfx_success ? valid
                                  : mvfxCheckScalarSource(&dragScale, &def->motion.dragScale, true,
                                                          d, parameterCount, MVFX_MAX_DRAW);
    valid = valid != mvfx_success ? valid
                                  : mvfxCheckVectorSource(&orientation, &def->orientation, 3, d,
                                                          parameterCount, caps->any);
    valid = valid != mvfx_success ? valid
                                  : mvfxCheckVectorSource(&angular, &def->angularVelocity, 3, d,
                                                          parameterCount, caps->spin);
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(&frame, MVFX_TAG_FRAME_START_LAST,
                                  mvfxWithinBound(def->frame.last < MVFX_MAX_FRAMES));
    valid = valid != mvfx_success
                ? valid
                : mvfxRefuseField(at, MVFX_TAG_EMITTER_DEF_FORCES,
                                  mvfxWithinBound(def->forceCount <= MVFX_MAX_FORCES));
    return valid != mvfx_success
               ? valid
               : mvfxRefuseField(at, MVFX_TAG_EMITTER_DEF_KILL_RULES,
                                 mvfxWithinBound(def->killRuleCount <= MVFX_MAX_KILL_RULES));
}

// The field of an emitter's orientation, frames, forces or kill rules out
// of a range that no named bound covers, 0 for none; the frame's field
// with *inFrame.
static uint32_t RefusedValue(const mvfxEmitterDef* def, bool* inFrame)
{
    *inFrame = false;
    if (!mvfxIsKind(def->orientationMode, mvfx_orientationRandom))
    {
        return MVFX_TAG_EMITTER_DEF_ORIENTATION_MODE;
    }
    if (def->orientationMode == mvfx_orientationEuler && def->orientation.kind == mvfx_sourceNone)
    {
        return MVFX_TAG_EMITTER_DEF_ORIENTATION;
    }
    if (!mvfxIsKind(def->frame.mode, mvfx_frameRandom) ||
        (def->frame.first < 0 || def->frame.first > def->frame.last))
    {
        *inFrame = true;
        return !mvfxIsKind(def->frame.mode, mvfx_frameRandom) ? MVFX_TAG_FRAME_START_MODE
                                                              : MVFX_TAG_FRAME_START_FIRST;
    }
    if (def->forceCount < 0 || (def->forceCount > 0 && def->forces == nullptr))
    {
        return MVFX_TAG_EMITTER_DEF_FORCES;
    }
    return def->killRuleCount < 0 || (def->killRuleCount > 0 && def->killRules == nullptr)
               ? MVFX_TAG_EMITTER_DEF_KILL_RULES
               : 0;
}

// Whether an emitter's custom attributes are valid: one to four components
// each, an initial value within its cap.
static mvfxResult CheckCustom(const mvfxFieldPath* at, const mvfxEmitterDef* def,
                              int32_t parameterCount, const mvfxSourceCaps* caps)
{
    bool d = def->duration > 0.0f;
    for (int32_t i = 0; i < def->customCount; ++i)
    {
        const mvfxCustomAttribute* custom = &def->custom[i];
        mvfxFieldPath item = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_CUSTOM, i);
        if (custom->components < 1 || custom->components > 4)
        {
            return mvfxRefuseField(&item, MVFX_TAG_CUSTOM_ATTRIBUTE_COMPONENTS, mvfx_errorInvalid);
        }
        mvfxFieldPath initialAt = mvfxFieldIn(&item, MVFX_TAG_CUSTOM_ATTRIBUTE_INITIAL, -1);
        mvfxResult initial = mvfxCheckVectorSource(&initialAt, &custom->initial, custom->components,
                                                   d, parameterCount, caps->custom[i]);
        if (initial != mvfx_success)
        {
            return initial;
        }
    }
    return mvfx_success;
}

// Whether an emitter's initial values are valid against its effect's
// parameters.
static mvfxResult CheckValues(const mvfxFieldPath* at, const mvfxEmitterDef* def,
                              int32_t parameterCount)
{
    if (def->customCount < 0 || (def->customCount > 0 && def->custom == nullptr))
    {
        return mvfxRefuseField(at, MVFX_TAG_EMITTER_DEF_CUSTOM, mvfx_errorInvalid);
    }
    if (def->customCount > MVFX_MAX_CUSTOM_ATTRIBUTES)
    {
        return mvfxRefuseField(at, MVFX_TAG_EMITTER_DEF_CUSTOM, mvfx_errorLimit);
    }
    mvfxFieldPath modulesAt = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_VELOCITY_MODULES, -1);
    mvfxResult modules = mvfxCheckVelocityModules(&modulesAt, &def->velocityModules);
    if (modules != mvfx_success)
    {
        return modules;
    }
    mvfxResult overLife = mvfxCheckOverLife(at, &def->overLife, def->custom, def->customCount);
    if (overLife != mvfx_success)
    {
        return overLife;
    }
    bool inFrame = false;
    uint32_t refused = RefusedValue(def, &inFrame);
    if (refused != 0)
    {
        mvfxFieldPath in = inFrame ? mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_FRAME, -1) : *at;
        return mvfxRefuseField(&in, refused, mvfx_errorInvalid);
    }
    // The sources' caps follow from the values over life, checked first.
    mvfxSourceCaps caps;
    mvfxSourceCapsOf(&def->overLife, def->custom, def->customCount, &caps);
    mvfxResult valid = CheckSources(at, def, parameterCount, &caps);
    for (int32_t f = 0; valid == mvfx_success && f < def->forceCount; ++f)
    {
        mvfxFieldPath force = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_FORCES, f);
        valid = mvfxCheckForce(&force, &def->forces[f], parameterCount, def->duration > 0.0f);
    }
    for (int32_t r = 0; valid == mvfx_success && r < def->killRuleCount; ++r)
    {
        mvfxFieldPath rule = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_KILL_RULES, r);
        valid = mvfxCheckKillRule(&rule, &def->killRules[r]);
    }
    valid = valid != mvfx_success ? valid
                                  : CheckHostModules(at, def->hostModules, def->hostModuleCount);
    valid = valid != mvfx_success ? valid : CheckCollisions(at, def);
    return valid != mvfx_success ? valid : CheckCustom(at, def, parameterCount, &caps);
}

// Whether a beam's point is finite and in range, its parameter one of the
// effect's.
static mvfxResult CheckBeamPoint(const mvfxFieldPath* at, const mvfxBeamPoint* point,
                                 int32_t parameterCount)
{
    if (point->fromParameter && (point->parameter < 0 || point->parameter >= parameterCount))
    {
        return mvfxRefuseField(at, MVFX_TAG_BEAM_POINT_PARAMETER, mvfx_errorInvalid);
    }
    return mvfxRefuseVector(at, MVFX_TAG_BEAM_POINT_POINT, point->point, MVFX_MAX_EXTENT);
}

// The field of a beam out of a range that no named bound covers, 0 for
// none: the emitter's capacity with *inEmitter, the beam's enable when
// anything else would spawn or move its points.
static uint32_t RefusedBeamField(const mvfxEmitterDef* def, bool* inEmitter)
{
    const mvfxBeamDef* beam = &def->beam;
    bool alone = def->rate == 0.0f && def->burstCount == 0 && def->ratePerDistance == 0.0f &&
                 !def->eventSpawn.enable && def->hostSpawnCapacity == 0 && !def->collision.enable &&
                 def->forceCount == 0 && !def->contacts.enable && !def->steering.enable &&
                 !def->pairForce.enable && (def->events & mvfx_eventMaskBirth) == 0;
    *inEmitter = false;
    if (beam->segments < 1)
    {
        return MVFX_TAG_BEAM_DEF_SEGMENTS;
    }
    if (beam->controlCount < 0 || beam->controlCount > 2)
    {
        return MVFX_TAG_BEAM_DEF_CONTROL_COUNT;
    }
    if (!IsWithin(beam->noiseFrequency, 0.0f, 1000.0f))
    {
        return MVFX_TAG_BEAM_DEF_NOISE_FREQUENCY;
    }
    if (!IsWithin(beam->noiseSpeed, -1000.0f, 1000.0f))
    {
        return MVFX_TAG_BEAM_DEF_NOISE_SPEED;
    }
    if (!alone)
    {
        return MVFX_TAG_BEAM_DEF_ENABLE;
    }
    *inEmitter = true;
    return def->capacity < beam->segments + 1 ? MVFX_TAG_EMITTER_DEF_CAPACITY : 0;
}

// Whether a beam is valid: its shape in range, and nothing that would
// spawn or move its points but the beam.
static mvfxResult CheckBeam(const mvfxFieldPath* at, const mvfxEmitterDef* def,
                            int32_t parameterCount)
{
    const mvfxBeamDef* beam = &def->beam;
    if (!beam->enable)
    {
        return mvfx_success;
    }
    mvfxFieldPath beamAt = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_BEAM, -1);
    // The count first: it bounds the sum below.
    if (beam->segments > MVFX_MAX_BEAM_SEGMENTS)
    {
        return mvfxRefuseField(&beamAt, MVFX_TAG_BEAM_DEF_SEGMENTS, mvfx_errorLimit);
    }
    bool inEmitter = false;
    uint32_t refused = RefusedBeamField(def, &inEmitter);
    if (refused != 0)
    {
        return mvfxRefuseField(inEmitter ? at : &beamAt, refused, mvfx_errorInvalid);
    }
    // One ribbon through its points, its ends placed: as the ribbon's own
    // fields say.
    mvfxFieldPath ribbon = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_RIBBON, -1);
    if (def->ribbon.ids != mvfx_ribbonOne || def->ribbon.pinHead)
    {
        return mvfxRefuseField(&ribbon,
                               def->ribbon.ids != mvfx_ribbonOne ? MVFX_TAG_RIBBON_DEF_IDS
                                                                 : MVFX_TAG_RIBBON_DEF_PIN_HEAD,
                               mvfx_errorInvalid);
    }
    mvfxResult shape = mvfxRefuseField(&beamAt, MVFX_TAG_BEAM_DEF_NOISE_AMPLITUDE,
                                       mvfxUpToBound(beam->noiseAmplitude, 0.0f, MVFX_MAX_EXTENT));
    mvfxFieldPath start = mvfxFieldIn(&beamAt, MVFX_TAG_BEAM_DEF_START, -1);
    shape = shape != mvfx_success ? shape : CheckBeamPoint(&start, &beam->start, parameterCount);
    mvfxFieldPath end = mvfxFieldIn(&beamAt, MVFX_TAG_BEAM_DEF_END, -1);
    shape = shape != mvfx_success ? shape : CheckBeamPoint(&end, &beam->end, parameterCount);
    for (int32_t c = 0; shape == mvfx_success && c < beam->controlCount; ++c)
    {
        mvfxFieldPath control = mvfxFieldIn(&beamAt, MVFX_TAG_BEAM_DEF_CONTROLS, c);
        shape = CheckBeamPoint(&control, &beam->controls[c], parameterCount);
    }
    return shape;
}

// The field of an emitter's spawning from events out of a range that no
// named bound covers, 0 for none.
static uint32_t RefusedSpawnField(const mvfxEffectDef* def, int32_t i)
{
    const mvfxEventSpawn* spawn = &def->emitters[i].eventSpawn;
    if (spawn->source < 0 || spawn->source >= def->emitterCount || spawn->source == i)
    {
        return MVFX_TAG_EVENT_SPAWN_SOURCE;
    }
    if (!mvfxIsKind(spawn->kind, mvfx_eventWater))
    {
        return MVFX_TAG_EVENT_SPAWN_KIND;
    }
    // A source that does not write the kind: the reference refused.
    if ((def->emitters[spawn->source].events & (1u << spawn->kind)) == 0)
    {
        return MVFX_TAG_EVENT_SPAWN_SOURCE;
    }
    if (spawn->minCount < 0 || spawn->minCount > spawn->maxCount)
    {
        return MVFX_TAG_EVENT_SPAWN_MIN_COUNT;
    }
    if (!(spawn->inheritVelocity >= 0.0f && spawn->inheritVelocity <= 1.0f))
    {
        return MVFX_TAG_EVENT_SPAWN_INHERIT_VELOCITY;
    }
    return spawn->capacity < 1 ? MVFX_TAG_EVENT_SPAWN_CAPACITY : 0;
}

// Whether an emitter's spawning from events is valid against its effect:
// a source not its own that writes the kind, counts and a share in range.
static mvfxResult CheckEventSpawn(const mvfxFieldPath* at, const mvfxEffectDef* def, int32_t i)
{
    const mvfxEventSpawn* spawn = &def->emitters[i].eventSpawn;
    if (!spawn->enable)
    {
        return mvfx_success;
    }
    mvfxFieldPath spawnAt = mvfxFieldIn(at, MVFX_TAG_EMITTER_DEF_EVENT_SPAWN, -1);
    uint32_t refused = RefusedSpawnField(def, i);
    if (refused != 0)
    {
        return mvfxRefuseField(&spawnAt, refused, mvfx_errorInvalid);
    }
    mvfxResult valid = mvfxRefuseField(&spawnAt, MVFX_TAG_EVENT_SPAWN_MAX_COUNT,
                                       mvfxWithinBound(spawn->maxCount <= MVFX_MAX_EVENT_SPAWN));
    return valid != mvfx_success
               ? valid
               : mvfxRefuseField(&spawnAt, MVFX_TAG_EVENT_SPAWN_CAPACITY,
                                 mvfxWithinBound(spawn->capacity <= MVFX_MAX_EMITTER_CAPACITY));
}

// Whether an effect's quality levels are valid: a count in range, scales
// in range, and only its own emitters left out.
static mvfxResult CheckQualityLevels(const mvfxFieldPath* root, const mvfxEffectDef* def)
{
    if (def->qualityLevelCount < 0 || (def->qualityLevelCount > 0 && def->qualityLevels == nullptr))
    {
        return mvfxRefuseField(root, MVFX_TAG_EFFECT_DEF_QUALITY_LEVELS, mvfx_errorInvalid);
    }
    if (def->qualityLevelCount > MVFX_MAX_QUALITY_LEVELS)
    {
        return mvfxRefuseField(root, MVFX_TAG_EFFECT_DEF_QUALITY_LEVELS, mvfx_errorLimit);
    }
    uint64_t own =
        def->emitterCount >= 64 ? ~(uint64_t)0 : ((uint64_t)1 << (uint32_t)def->emitterCount) - 1u;
    for (int32_t q = 0; q < def->qualityLevelCount; ++q)
    {
        const mvfxQualityLevel* level = &def->qualityLevels[q];
        uint32_t refused = !(level->capacityScale > 0.0f && level->capacityScale <= 1.0f)
                               ? MVFX_TAG_QUALITY_LEVEL_CAPACITY_SCALE
                           : !IsWithin(level->rateScale, 0.0f, 1.0f)
                               ? MVFX_TAG_QUALITY_LEVEL_RATE_SCALE
                           : (level->omitted & ~own) != 0 ? MVFX_TAG_QUALITY_LEVEL_OMITTED
                                                          : 0;
        if (refused != 0)
        {
            mvfxFieldPath at = mvfxFieldIn(root, MVFX_TAG_EFFECT_DEF_QUALITY_LEVELS, q);
            return mvfxRefuseField(&at, refused, mvfx_errorInvalid);
        }
    }
    return mvfx_success;
}

// Whether an effect's parameters are finite and its gases and water
// surfaces valid, for counts within their bounds.
static mvfxResult CheckFields(const mvfxFieldPath* root, const mvfxEffectDef* def)
{
    for (int32_t i = 0; i < 4 * def->parameterCount; ++i)
    {
        if (!isfinite(def->parameters[i]))
        {
            return mvfxRefuseItem(root, MVFX_TAG_EFFECT_DEF_PARAMETERS, i / 4, mvfx_errorInvalid);
        }
    }
    mvfxResult valid = mvfx_success;
    for (int32_t g = 0; valid == mvfx_success && g < def->gasCount; ++g)
    {
        mvfxFieldPath gas = mvfxFieldIn(root, MVFX_TAG_EFFECT_DEF_GASES, g);
        valid = mvfxCheckGasDef(&gas, &def->gases[g]);
    }
    for (int32_t w = 0; valid == mvfx_success && w < def->waterCount; ++w)
    {
        mvfxFieldPath water = mvfxFieldIn(root, MVFX_TAG_EFFECT_DEF_WATERS, w);
        valid = mvfxCheckWaterDef(&water, &def->waters[w]);
    }
    return valid != mvfx_success ? valid : CheckQualityLevels(root, def);
}

// Whether emitter i of an effect is valid against the effect.
static mvfxResult CheckEmitter(const mvfxFieldPath* root, const mvfxEffectDef* def, int32_t i)
{
    const mvfxEmitterDef* emitter = &def->emitters[i];
    mvfxFieldPath at = mvfxFieldIn(root, MVFX_TAG_EFFECT_DEF_EMITTERS, i);
    mvfxFieldPath water = mvfxFieldIn(&at, MVFX_TAG_EMITTER_DEF_WATER, -1);
    mvfxResult checked = CheckEmitterDef(&at, emitter);
    checked = checked != mvfx_success ? checked : CheckValues(&at, emitter, def->parameterCount);
    checked = checked != mvfx_success ? checked : CheckEventSpawn(&at, def, i);
    checked = checked != mvfx_success ? checked : CheckBeam(&at, emitter, def->parameterCount);
    return checked != mvfx_success
               ? checked
               : mvfxCheckWaterCoupling(&water, &emitter->water, def->waters, def->waterCount);
}

// The field of an effect def whose array is missing or of a negative
// count, 0 for none.
static uint32_t RefusedArray(const mvfxEffectDef* def)
{
    if (def->emitterCount < 0 || (def->emitterCount > 0 && def->emitters == nullptr))
    {
        return MVFX_TAG_EFFECT_DEF_EMITTERS;
    }
    if (def->gasCount < 0 || (def->gasCount > 0 && def->gases == nullptr))
    {
        return MVFX_TAG_EFFECT_DEF_GASES;
    }
    if (def->waterCount < 0 || (def->waterCount > 0 && def->waters == nullptr))
    {
        return MVFX_TAG_EFFECT_DEF_WATERS;
    }
    return def->parameterCount < 0 || (def->parameterCount > 0 && def->parameters == nullptr)
               ? MVFX_TAG_EFFECT_DEF_PARAMETERS
               : 0;
}

// The field of an effect def whose count is past its named bound, 0 for
// none.
static uint32_t OverCount(const mvfxEffectDef* def)
{
    return def->emitterCount > MVFX_MAX_EMITTERS       ? MVFX_TAG_EFFECT_DEF_EMITTERS
           : def->gasCount > MVFX_MAX_GASES            ? MVFX_TAG_EFFECT_DEF_GASES
           : def->waterCount > MVFX_MAX_WATERS         ? MVFX_TAG_EFFECT_DEF_WATERS
           : def->parameterCount > MVFX_MAX_PARAMETERS ? MVFX_TAG_EFFECT_DEF_PARAMETERS
                                                       : 0;
}

mvfxResult mvfxCheckEffectDef(const mvfxFieldPath* root, const mvfxEffectDef* def)
{
    if (def == nullptr || def->cookie != MVFX_EFFECT_DEF_COOKIE)
    {
        return mvfxRefuseHere(root, mvfx_errorInvalid);
    }
    uint32_t array = RefusedArray(def);
    if (array != 0)
    {
        return mvfxRefuseField(root, array, mvfx_errorInvalid);
    }
    // The counts bound the sum below and the arrays the checks fill.
    uint32_t over = OverCount(def);
    if (over != 0)
    {
        return mvfxRefuseField(root, over, mvfx_errorLimit);
    }
    if (def->emitterCount + def->gasCount + def->waterCount < 1)
    {
        return mvfxRefuseHere(root, mvfx_errorInvalid);
    }
    mvfxResult valid = CheckFields(root, def);
    for (int32_t i = 0; valid == mvfx_success && i < def->emitterCount; ++i)
    {
        valid = CheckEmitter(root, def, i);
    }
    valid = valid != mvfx_success ? valid : CheckDefaultsReach(root, def);
    valid = valid != mvfx_success ? valid : mvfxCheckEffectGases(root, def);
    int32_t order[MVFX_MAX_EMITTERS];
    // Emitters that wait on each other (event spawns' and neighbors'
    // sources in a cycle): the emitters as a whole.
    return valid != mvfx_success ? valid
                                 : mvfxRefuseField(root, MVFX_TAG_EFFECT_DEF_EMITTERS,
                                                   mvfxRequire(mvfxOrderEmitters(def, order)));
}
