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
    }
    for (int32_t f = 0; f < e->forceCount; ++f)
    {
        viewOut->forceKinds |= 1u << e->forces[f].kind;
        if (e->forces[f].kind == mvfx_forceVectorField)
        {
            viewOut->forceGrids[f] = e->forces[f].grid;
        }
    }
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
