// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "plan_resolved.h"

#include "collide.h"
#include "force.h"

// A resolved collider as a plan hands it over.
static mvfxPlannedCollider PlannedCollider(const mvfxResolvedCollider* c)
{
    mvfxPlannedCollider out = {.kind = c->kind, .inside = c->inside, .radius = c->radius};
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
    for (int32_t c = 0; c < plan->colliderCount; ++c)
    {
        plan->colliders[c] = PlannedCollider(&state->colliders[c]);
    }
    plan->restitution = collision->restitution;
    plan->friction = collision->friction;
    plan->lifeLoss = collision->lifeLoss;
    plan->restSpeed = collision->restSpeed * state->velocityModules.sigma;
    plan->killOnCollision = collision->killOnCollision;
}
