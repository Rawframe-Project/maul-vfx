// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "plan_resolved.h"

#include "beam.h"
#include "collide.h"
#include "depth.h"
#include "force.h"
#include "kill.h"
#include "steer.h"
#include "velocity_modules.h"

#include <math.h>
#include <string.h>

mvfxPlannedCollider mvfxPlannedColliderOf(const mvfxResolvedCollider* c, mvfxGridId grid)
{
    mvfxPlannedCollider out = {.kind = c->kind,
                               .inside = c->inside,
                               .radius = c->radius,
                               .grid = c->grid != nullptr ? grid : (mvfxGridId){0},
                               .scale = c->scale,
                               .grow = c->grow};
    for (int32_t k = 0; k < 3; ++k)
    {
        out.center[k] = c->center[k];
        out.normal[k] = c->normal[k];
        out.halfExtents[k] = c->halfExtents[k];
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        out.rotation[k] = c->rotation[k];
    }
    return out;
}

// A resolved kill rule as a plan hands it over, its least age in ticks
// rounded up (exact: the age in seconds times 2^20 in binary64).
static mvfxPlannedKillRule PlannedKillRule(const mvfxResolvedKillRule* r)
{
    mvfxPlannedKillRule out = {
        .kind = r->kind,
        .radius = r->radius,
        .speed = r->speed,
        .minimumAgeTicks = (uint64_t)ceil((double)r->minimumAge * (double)MVFX_TICKS_PER_SECOND),
        .collisions = r->collisions,
    };
    for (int32_t k = 0; k < 3; ++k)
    {
        out.center[k] = r->center[k];
        out.halfExtents[k] = r->halfExtents[k];
        out.normal[k] = r->normal[k];
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        out.rotation[k] = r->rotation[k];
    }
    return out;
}

// Resolved velocity modules as a plan hands them over.
static mvfxPlannedModules PlannedModules(const mvfxResolvedVelocityModules* m)
{
    mvfxPlannedModules out = {.sigma = m->sigma};
    for (int32_t k = 0; k < 3; ++k)
    {
        out.addedDirection[k] = m->addedDirection[k];
        out.orbitAxis[k] = m->orbitAxis[k];
        out.orbitCenter[k] = m->orbitCenter[k];
        out.radialCenter[k] = m->radialCenter[k];
    }
    return out;
}

void mvfxPlanResolved(const mvfxInstance* instance, int32_t emitter, mvfxEmitterPlan* plan)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    const mvfxEmitterState* state = &instance->emitters[emitter];
    double time = (double)instance->planStart / (double)MVFX_TICKS_PER_SECOND;
    plan->forceCount = compiled->forceCount;
    for (int32_t f = 0; f < plan->forceCount; ++f)
    {
        plan->forces[f] = mvfxPlannedForceOf(&state->forces[f], time);
    }
    const mvfxCollision* collision = &compiled->collision;
    plan->colliderCount = state->colliderCount;
    int32_t own = compiled->colliderCount < state->colliderCount ? compiled->colliderCount
                                                                 : state->colliderCount;
    plan->ownColliderCount = own;
    for (int32_t c = 0; c < plan->colliderCount; ++c)
    {
        // The emitter's own, then the instance's.
        mvfxGridId grid =
            c < own ? compiled->colliders[c].grid : instance->hostColliders[c - own].grid;
        plan->colliders[c] = mvfxPlannedColliderOf(&state->colliders[c], grid);
    }
    plan->restitution = collision->restitution;
    plan->friction = collision->friction;
    plan->lifeLoss = collision->lifeLoss;
    plan->restSpeed = collision->restSpeed * state->velocityModules.sigma;
    plan->killOnCollision = collision->killOnCollision;
    memcpy(plan->parameters, instance->parameters, sizeof plan->parameters);
    mvfxResolveBeam(instance, emitter, &plan->beam);
    // At the plan's start, as its step began.
    mvfxResolveSteering(instance, emitter, &instance->planFrom, &plan->steering);
    plan->depth = (mvfxPlannedDepth){0};
    if (state->hasDepth)
    {
        mvfxPlanDepth(&state->depth, collision, &plan->depth);
    }
    plan->modules = PlannedModules(&state->velocityModules);
    plan->killRuleCount = compiled->killRuleCount;
    for (int32_t r = 0; r < plan->killRuleCount; ++r)
    {
        plan->killRules[r] = PlannedKillRule(&state->killRules[r]);
    }
}
