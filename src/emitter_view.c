// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An effect's emitters as compiled, for a simulation elsewhere: what each
// uses, read from its compiled parts and the attributes it stores, and
// the numbers of its spawning and motion.

#include "emitter_view.h"

#include "effect_api.h"

// A bit when a part is used.
static uint64_t If(bool used, uint64_t bit)
{
    return used ? bit : 0;
}

// The bits of the attributes the emitter stores beyond the plain sprite's.
static uint64_t StoredFeatures(const mvfxCompiledEmitter* e)
{
    const mvfxAttributePlan* plan = &e->plan;
    return If(mvfxHasAttribute(plan, mvfx_attributeSizeX), MVFX_FEATURE_SIZE) |
           If(mvfxHasAttribute(plan, mvfx_attributeColorR), MVFX_FEATURE_COLOR) |
           If(mvfxHasAttribute(plan, mvfx_attributeAngle), MVFX_FEATURE_ANGLE) |
           If(mvfxHasAttribute(plan, mvfx_attributeOrientationX), MVFX_FEATURE_ORIENTATION) |
           If(mvfxHasAttribute(plan, mvfx_attributeDrag), MVFX_FEATURE_DRAG_SCALE) |
           If(mvfxHasAttribute(plan, mvfx_attributeFrame), MVFX_FEATURE_FRAME) |
           If(mvfxHasAttribute(plan, mvfx_attributeMesh), MVFX_FEATURE_MESH_INDEX) |
           If(e->customCount > 0, MVFX_FEATURE_CUSTOM);
}

// The bits of how its particles move and what they meet.
static uint64_t MotionFeatures(const mvfxCompiledEmitter* e)
{
    const mvfxCompiledVelocityModules* m = &e->velocityModules;
    bool modules = m->moves || m->limitSpeed || m->motionScale.curve != nullptr ||
                   m->motionScale.value != 1.0f;
    const mvfxCompiledOverLife* o = &e->overLife;
    bool overLife = o->color || o->size || o->rotation;
    for (int32_t i = 0; i < e->customCount; ++i)
    {
        overLife = overLife || o->customScaled[i];
    }
    return If(e->forceCount > 0, MVFX_FEATURE_FORCES) | If(modules, MVFX_FEATURE_VELOCITY_MODULES) |
           If(overLife, MVFX_FEATURE_OVER_LIFE) |
           If(e->killRuleCount > 0, MVFX_FEATURE_KILL_RULES) |
           If(e->collision.enabled, MVFX_FEATURE_COLLISION) |
           If(e->hostModuleCount > 0, MVFX_FEATURE_HOST_MODULES) |
           If(e->neighbors.enabled, MVFX_FEATURE_NEIGHBORS) |
           If(e->steering.enabled, MVFX_FEATURE_STEERING) |
           If(e->contacts.enabled, MVFX_FEATURE_CONTACTS) |
           If(e->pairRadius > 0.0f, MVFX_FEATURE_PAIR_FORCE) |
           If(e->rigid.enabled, MVFX_FEATURE_RIGID) | If(e->liquid.enabled, MVFX_FEATURE_LIQUID) |
           If(e->gasSplat.enabled, MVFX_FEATURE_GAS_SPLAT) |
           If(e->water.enabled, MVFX_FEATURE_WATER);
}

uint64_t mvfxFeaturesOf(const mvfxCompiledEmitter* e)
{
    uint64_t spawning = If(e->rateCurve != nullptr, MVFX_FEATURE_RATE_CURVE) |
                        If(e->ratePerDistance > 0.0f, MVFX_FEATURE_DISTANCE) |
                        If(e->hostSpawnCapacity > 0, MVFX_FEATURE_HOST_SPAWN) |
                        If(e->inheritVelocity != 0.0f, MVFX_FEATURE_INHERIT_VELOCITY) |
                        If(e->eventSpawn.enabled, MVFX_FEATURE_EVENT_SPAWN);
    uint64_t output =
        If(e->events != 0, MVFX_FEATURE_EVENTS) | If(e->plexus.enabled, MVFX_FEATURE_PLEXUS) |
        If(e->ribbonIds != mvfx_ribbonNone, MVFX_FEATURE_RIBBON) |
        If(e->beam.enabled, MVFX_FEATURE_BEAM) | If(e->unitPoints > 1, MVFX_FEATURE_UNITS) |
        If(e->light.enabled, MVFX_FEATURE_LIGHT) |
        If(e->sortMode != mvfx_sortNone, MVFX_FEATURE_SORT);
    return spawning | StoredFeatures(e) | MotionFeatures(e) | output;
}

// A source as compiled, as the view gives it.
static mvfxSourceView SourceOf(const mvfxCompiledVector* source)
{
    mvfxSourceView view = {
        .kind = source->kind,
        .uniform = source->uniform,
        .components = source->components,
        .parameter = source->parameter,
        .curve = source->curve != nullptr ? source->curve->samples : nullptr,
        .gradient = source->gradient != nullptr ? source->gradient->rgba[0] : nullptr,
    };
    for (int32_t c = 0; c < 4; ++c)
    {
        view.min[c] = source->min[c];
        view.max[c] = source->max[c];
    }
    return view;
}

// A scalar source as the view gives it: one component.
static mvfxSourceView ScalarSourceOf(const mvfxCompiledScalar* source)
{
    return (mvfxSourceView){
        .kind = source->kind,
        .components = 1,
        .min = {source->min},
        .max = {source->max},
        .parameter = source->parameter,
        .curve = source->curve != nullptr ? source->curve->samples : nullptr,
    };
}

// A value over life as the view gives it.
// Whether an emitter's particles may be killed within a step.
static bool Kills(const mvfxCompiledEmitter* e)
{
    bool kills =
        e->killRuleCount > 0 ||
        (e->collision.enabled && (e->collision.killOnCollision || e->collision.killOutsideView));
    for (int32_t f = 0; f < e->forceCount; ++f)
    {
        kills =
            kills || (e->forces[f].kind == mvfx_forceAttractor && e->forces[f].killRadius > 0.0f);
    }
    return kills;
}

static mvfxLifeView LifeOf(const mvfxCompiledLife* life)
{
    return (mvfxLifeView){.value = life->value,
                          .curve = life->curve != nullptr ? life->curve->samples : nullptr};
}

// Velocity modules as the view gives them.
static mvfxModulesView ModulesOf(const mvfxCompiledVelocityModules* m)
{
    return (mvfxModulesView){
        .limitSpeed = m->limitSpeed,
        .limitDampen = m->limitDampen,
        .moves = m->moves,
        .speedLimit = LifeOf(&m->speedLimit),
        .motionScale = LifeOf(&m->motionScale),
        .addedSpeed = LifeOf(&m->addedSpeed),
        .radialSpeed = LifeOf(&m->radialSpeed),
        .orbitRate = LifeOf(&m->orbitRate),
    };
}

// Values over life as records show them, as the view gives them.
static mvfxOverLifeView OverLifeOf(const mvfxCompiledOverLife* o)
{
    mvfxOverLifeView view = {
        .color = o->color,
        .colorInput = o->colorInput,
        .colorRange = {o->colorRange[0], o->colorRange[1]},
        .gradient = o->gradient != nullptr ? o->gradient->rgba[0] : nullptr,
        .scaleAlpha = o->scaleAlpha,
        .alpha = LifeOf(&o->alpha),
        .size = o->size,
        .sizeInput = o->sizeInput,
        .sizeRange = {o->sizeRange[0], o->sizeRange[1]},
        .perAxis = o->perAxis,
        .frame = o->frame,
    };
    for (int32_t k = 0; k < 3; ++k)
    {
        view.sizes[k] = LifeOf(&o->sizes[k]);
    }
    for (int32_t i = 0; i < MVFX_MAX_CUSTOM_ATTRIBUTES; ++i)
    {
        view.customScaled[i] = o->customScaled[i];
        view.custom[i] = LifeOf(&o->custom[i]);
    }
    return view;
}

// An index's draw at birth, as the view gives it.
static mvfxIndexView IndexOf(const mvfxFrameStart* start)
{
    return (mvfxIndexView){
        .first = start->first, .last = start->last, .random = start->mode == mvfx_frameRandom};
}

// An attribute's first array in a plan, -1 when it stores none.
static int16_t ArrayOf(const mvfxAttributePlan* plan, int32_t attribute)
{
    return mvfxHasAttribute(plan, attribute) ? (int16_t)plan->slots[attribute] : (int16_t)-1;
}

// Where a plan keeps an emitter's values.
static mvfxAttributeArrays ArraysOf(const mvfxAttributePlan* plan)
{
    mvfxAttributeArrays arrays = {
        .position = ArrayOf(plan, mvfx_attributePositionX),
        .birth = ArrayOf(plan, mvfx_attributeBirthLow),
        .inverseLife = ArrayOf(plan, mvfx_attributeInverseLife),
        .serial = ArrayOf(plan, mvfx_attributeSerial),
        .velocity = ArrayOf(plan, mvfx_attributeVelocityX),
        .size = ArrayOf(plan, mvfx_attributeSizeX),
        .color = ArrayOf(plan, mvfx_attributeColorR),
        .angle = ArrayOf(plan, mvfx_attributeAngle),
        .spin = ArrayOf(plan, mvfx_attributeSpin),
        .orientation = ArrayOf(plan, mvfx_attributeOrientationX),
        .angular = ArrayOf(plan, mvfx_attributeAngularX),
        .frame = ArrayOf(plan, mvfx_attributeFrame),
        .drag = ArrayOf(plan, mvfx_attributeDrag),
        .collisions = ArrayOf(plan, mvfx_attributeCollisions),
        .mesh = ArrayOf(plan, mvfx_attributeMesh),
        .ribbon = ArrayOf(plan, mvfx_attributeRibbon),
        .ribbonDistance = ArrayOf(plan, mvfx_attributeRibbonDistance),
        .neighborCount = ArrayOf(plan, mvfx_attributeNeighborCount),
        .neighborCenter = ArrayOf(plan, mvfx_attributeNeighborCenterX),
        .neighborVelocity = ArrayOf(plan, mvfx_attributeNeighborVelocityX),
        .count = plan->arrayCount,
    };
    for (int32_t i = 0; i < MVFX_MAX_CUSTOM_ATTRIBUTES; ++i)
    {
        arrays.custom[i] = ArrayOf(plan, mvfx_attributeCustom + 4 * i);
    }
    return arrays;
}

// Where an emitter's particles keep their values, how its births draw
// them, how their sizes grow its bounds, how they turn and how its
// velocity modules move them.
static void ValuesOf(const mvfxCompiledEmitter* e, mvfxEmitterView* viewOut)
{
    viewOut->arrays = ArraysOf(&e->plan);
    viewOut->size = SourceOf(&e->size);
    viewOut->color = SourceOf(&e->color);
    for (int32_t i = 0; i < e->customCount; ++i)
    {
        viewOut->custom[i] = SourceOf(&e->custom[i]);
    }
    viewOut->frame = IndexOf(&e->frame);
    viewOut->meshIndex = IndexOf(&e->meshIndex);
    viewOut->angle = ScalarSourceOf(&e->angle);
    viewOut->spin = ScalarSourceOf(&e->spin);
    viewOut->orientationMode = e->orientationMode;
    viewOut->orientation = SourceOf(&e->orientation);
    viewOut->angularVelocity = SourceOf(&e->angularVelocity);
    viewOut->dragScale = ScalarSourceOf(&e->dragScale);
    viewOut->angularDrag = e->angularDrag;
    viewOut->rotationSpeed = LifeOf(&e->overLife.rotationScale);
    viewOut->modules = ModulesOf(&e->velocityModules);
    viewOut->hostSpawnCapacity = e->hostSpawnCapacity;
    viewOut->overLife = OverLifeOf(&e->overLife);
    viewOut->sortMode = e->sortMode;
    viewOut->sortMeasure = e->sortMeasure;
    viewOut->events = e->events;
    viewOut->eventCapacity = e->eventCapacity;
    viewOut->lifetime = ScalarSourceOf(&e->lifetime);
    viewOut->eventSpawn = e->eventSpawn;
    viewOut->delayTicks = e->delay;
    viewOut->loopTicks = e->duration;
    viewOut->beam = e->beam;
    viewOut->light = e->light;
    viewOut->sprite = e->sprite;
    viewOut->neighbors = e->neighbors;
    viewOut->gridCell = e->gridCell;
    viewOut->steering = e->steering;
    viewOut->steering.path.points = e->steering.enabled ? &e->pathPoints[0][0] : nullptr;
    viewOut->pairRadius = e->pairRadius;
    viewOut->pairStrength = LifeOf(&e->pairStrength);
    viewOut->contacts = e->contacts;
    viewOut->kills = Kills(e);
    viewOut->ribbon = (mvfxRibbonView){
        .ids = e->ribbonIds,
        .breakDistance = e->ribbonBreakDistance,
        .pieces = e->ribbonPieces,
        .texture = e->ribbonTexture,
        .tileLength = e->ribbonTileLength,
        .scroll = e->ribbonScroll,
        .facing = e->ribbonFacing,
        .normal = {e->ribbonNormal[0], e->ribbonNormal[1], e->ribbonNormal[2]},
        .pinHead = e->ribbonPinHead,
        .width = LifeOf(&e->ribbonWidth),
        .color = e->ribbonColor != nullptr ? e->ribbonColor->rgba[0] : nullptr,
    };
    for (int32_t k = 0; k < MVFX_MAX_RIBBON_PIECES; ++k)
    {
        viewOut->ribbon.bendCosines[k] = e->ribbonCosLimits[k];
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        viewOut->sizeBounds[k] = e->overLife.sizeBounds[k];
    }
    viewOut->sizeMargin = e->overLife.sizeMargin;
}

mvfxResult mvfxEffect_GetEmitterView(const mvfxContext* context, mvfxEffectId effect,
                                     int32_t emitter, mvfxEmitterView* viewOut)
{
    if (viewOut != nullptr)
    {
        *viewOut = (mvfxEmitterView){0};
    }
    if (context == nullptr || viewOut == nullptr)
    {
        return mvfx_errorInvalid;
    }
    const mvfxEffect* live = mvfxContextEffect(context, effect);
    if (live == nullptr)
    {
        return mvfx_errorStale;
    }
    if (emitter < 0 || emitter >= live->emitterCount)
    {
        return mvfx_errorInvalid;
    }
    const mvfxCompiledEmitter* e = &live->emitters[emitter];
    const mvfxCompiledShape* s = &e->shape;
    const mvfxCompiledVelocity* v = &e->velocity;
    *viewOut = (mvfxEmitterView){
        .features = mvfxFeaturesOf(e),
        .space = e->space,
        .inheritVelocity = e->inheritVelocity,
        .shape = {.kind = s->kind,
                  .arcMode = s->arcMode,
                  .coneVolume = s->coneVolume,
                  .meshMode = s->meshMode,
                  .radius = s->radius,
                  .length = s->length,
                  .tangent = s->tangent,
                  .thickness = s->thickness,
                  .arc = s->arc,
                  .arcSpeed = s->arcSpeed},
        .velocity = {.mode = v->mode,
                     .coneCosine = v->coneCosine,
                     .randomize = v->randomize,
                     .speedMin = v->speed.min,
                     .speedMax = v->speed.max},
        .gravitySpace = e->gravitySpace,
        .drag = e->drag,
        .fixedBounds = e->fixedBounds,
        .sceneQueries = e->collision.enabled && e->collision.sceneQueries,
        .depthImage = e->collision.enabled && e->collision.depthImage,
    };
    for (int32_t c = 0; e->collision.enabled && c < e->colliderCount; ++c)
    {
        viewOut->colliderKinds |= 1u << e->colliders[c].kind;
        viewOut->colliderGrids[c] = e->colliders[c].grid;
    }
    for (int32_t f = 0; f < e->forceCount; ++f)
    {
        viewOut->forceKinds |= 1u << e->forces[f].kind;
        if (e->forces[f].kind == mvfx_forceVectorField)
        {
            viewOut->forceGrids[f] = e->forces[f].grid;
        }
    }
    ValuesOf(e, viewOut);
    for (int32_t k = 0; k < 9; ++k)
    {
        viewOut->shape.matrix[k] = s->matrix[k];
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        viewOut->shape.offset[k] = s->offset[k];
        viewOut->shape.halfExtents[k] = s->halfExtents[k];
        viewOut->velocity.direction[k] = v->direction[k];
        viewOut->velocity.tangentA[k] = v->tangentA[k];
        viewOut->velocity.tangentB[k] = v->tangentB[k];
        viewOut->gravity[k] = e->gravity[k];
        viewOut->boundsMin[k] = e->boundsMin[k];
        viewOut->boundsMax[k] = e->boundsMax[k];
    }
    return mvfx_success;
}
