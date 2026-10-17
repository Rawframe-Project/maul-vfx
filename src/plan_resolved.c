// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "plan_resolved.h"

#include "collide.h"
#include "depth.h"
#include "force.h"

// A resolved collider as a plan hands it over, with its def's grid.
static mvfxPlannedCollider PlannedCollider(const mvfxResolvedCollider* c, mvfxGridId grid)
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
        plan->colliders[c] = PlannedCollider(&state->colliders[c], grid);
    }
    plan->restitution = collision->restitution;
    plan->friction = collision->friction;
    plan->lifeLoss = collision->lifeLoss;
    plan->restSpeed = collision->restSpeed * state->velocityModules.sigma;
    plan->killOnCollision = collision->killOnCollision;
    plan->depth = (mvfxPlannedDepth){0};
    if (state->hasDepth)
    {
        mvfxPlanDepth(&state->depth, collision, &plan->depth);
    }
}
