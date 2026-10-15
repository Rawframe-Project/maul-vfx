// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "rigid.h"

#include "appearance.h"
#include "collide.h"
#include "depth.h"
#include "direction.h"
#include "integrate.h"
#include "rigid_body.h"
#include "rigid_contact.h"
#include "rigid_pair.h"
#include "rigid_sleep.h"

#include <math.h>
#include <string.h>

static float Dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void Cross(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static void ToBody(const float q[4], const float v[3], float out[3])
{
    const float inverse[4] = {-q[0], -q[1], -q[2], q[3]};
    mvfxRotate(inverse, v, out);
}

// A contact's depth at the body's start pose.
static float StartDepthOf(const mvfxBody* b, const mvfxBodyContact* c)
{
    float r[3];
    mvfxRotate(b->q0, c->body, r);
    float gap[3] = {b->x0[0] + r[0] - c->surface[0], b->x0[1] + r[1] - c->surface[1],
                    b->x0[2] + r[2] - c->surface[2]};
    return Dot(gap, c->normal);
}

// A contact's offset from the body's center in the world, and its depth
// at the body's pose.
static float DepthOf(const mvfxBody* b, const mvfxBodyContact* c, float r[3])
{
    mvfxRotate(b->q, c->body, r);
    float gap[3] = {b->x[0] + r[0] - c->surface[0], b->x[1] + r[1] - c->surface[1],
                    b->x[2] + r[2] - c->surface[2]};
    return Dot(gap, c->normal);
}

// A body's patch of contacts with the collider of contact first: their
// center on the body (in its frame) and normal weighted by their normal
// multipliers, the multipliers' sum and their normal impulses' over the
// substep of length h, and the patch's radius; false when none pushed.
typedef struct Patch
{
    float point[3];
    float n[3];
    float load;
    float pressed;
    float radius;
} Patch;

// The contacts' mean distance from the patch's center across its normal,
// weighted by their multipliers, the body at orientation q.
static float RadiusOf(const mvfxBodyContact* items, int32_t count, int32_t first,
                      const Patch* patch, const float q[4])
{
    float n[3];
    ToBody(q, patch->n, n);
    float radius = 0.0f;
    for (int32_t i = first; i < count; ++i)
    {
        const mvfxBodyContact* c = &items[i];
        float lambda = c->collider == items[first].collider && c->lambda > 0.0f ? c->lambda : 0.0f;
        float d[3] = {c->body[0] - patch->point[0], c->body[1] - patch->point[1],
                      c->body[2] - patch->point[2]};
        float along = Dot(d, n);
        float across = Dot(d, d) - along * along;
        radius += lambda * sqrtf(fmaxf(across, 0.0f));
    }
    return radius / patch->load;
}

static bool PatchOf(const mvfxBodyContact* items, int32_t count, int32_t first, float h,
                    const float q[4], Patch* patch)
{
    float* point = patch->point;
    float* n = patch->n;
    float* load = &patch->load;
    float* pressed = &patch->pressed;
    int32_t collider = items[first].collider;
    *load = 0.0f;
    *pressed = 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        point[k] = 0.0f;
        n[k] = 0.0f;
    }
    for (int32_t i = first; i < count; ++i)
    {
        const mvfxBodyContact* c = &items[i];
        float lambda = c->collider == collider && c->lambda > 0.0f ? c->lambda : 0.0f;
        *load += lambda;
        *pressed += lambda > 0.0f ? lambda / h + c->applied : 0.0f;
        for (int32_t k = 0; k < 3; ++k)
        {
            point[k] += lambda * c->body[k];
            n[k] += lambda * c->normal[k];
        }
    }
    if (!(*load > 0.0f) || !mvfxNormalize(n))
    {
        return false;
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        point[k] /= *load;
    }
    patch->radius = RadiusOf(items, count, first, patch, q);
    return true;
}

// Whether contact i is the first of its collider's.
static bool FirstOf(const mvfxBodyContact* items, int32_t i)
{
    bool first = true;
    for (int32_t j = 0; first && j < i; ++j)
    {
        first = items[j].collider != items[i].collider;
    }
    return first;
}

// The static friction of each of the body's patches, in the order first
// met.
static void Rub(const mvfxRigidSolve* s, mvfxBody* b, mvfxBodyContact* items, int32_t count)
{
    for (int32_t i = 0; i < count; ++i)
    {
        Patch patch;
        if (FirstOf(items, i) && PatchOf(items, count, i, s->h, b->q, &patch))
        {
            float r[3];
            float r0[3];
            mvfxRotate(b->q, patch.point, r);
            mvfxRotate(b->q0, patch.point, r0);
            mvfxRubPatch(s, b, r, r0, nullptr, nullptr, nullptr, patch.n, patch.load, patch.radius,
                         &items[i].rubbed);
            mvfxTwistPatch(s, b, nullptr, patch.n, patch.load * patch.radius, &items[i].twisted);
        }
    }
}

// The dynamic friction of each of the body's patches.
static void Slow(const mvfxRigidSolve* s, mvfxBody* b, mvfxBodyContact* items, int32_t count)
{
    for (int32_t i = 0; i < count; ++i)
    {
        Patch patch;
        if (FirstOf(items, i) && PatchOf(items, count, i, s->h, b->q, &patch))
        {
            float r[3];
            mvfxRotate(b->q, patch.point, r);
            mvfxSlowPatch(b, r, nullptr, nullptr, patch.n, patch.pressed, patch.radius,
                          s->rigid->dynamicFriction, &items[i].slowed);
            mvfxUnspinPatch(b, nullptr, patch.n,
                            s->rigid->dynamicFriction * patch.pressed * patch.radius,
                            &items[i].spun);
        }
    }
}

// Pushes the body's start pose out of the depth its contacts had there,
// moving its pose by the very same steps, so pushing out of it gives no
// velocity (post-stabilization, Cline and Pai 2003): left over from
// before, born into or met by a moving collider, it does not pop out
// however short the substep. What is left to push out is the substep's
// own, the contacts' impulses.
static void Stabilize(mvfxBody* b, const mvfxBodyContact* items, int32_t count, bool backward)
{
    for (int32_t step = 0; step < count; ++step)
    {
        const mvfxBodyContact* c = &items[mvfxPassIndex(0, count, step, backward)];
        float depth = StartDepthOf(b, c);
        if (depth < 0.0f)
        {
            float r[3];
            mvfxRotate(b->q0, c->body, r);
            float lambda = -depth / mvfxBodyWeight(b, r, c->normal);
            const float p[3] = {lambda * c->normal[0], lambda * c->normal[1],
                                lambda * c->normal[2]};
            mvfxBodyPush(b, r, p, true);
        }
    }
}

// A pass pushing the body out of its contacts, then with friction each
// collider's patch's static friction, in the order first met.
static void SolvePositions(const mvfxRigidSolve* s, mvfxBody* b, mvfxBodyContact* items,
                           int32_t count, bool friction, bool backward)
{
    for (int32_t step = 0; step < count; ++step)
    {
        mvfxBodyContact* c = &items[mvfxPassIndex(0, count, step, backward)];
        float r[3];
        float depth = DepthOf(b, c, r);
        if (depth < 0.0f)
        {
            float lambda = -depth / mvfxBodyWeight(b, r, c->normal);
            c->lambda += lambda;
            const float p[3] = {lambda * c->normal[0], lambda * c->normal[1],
                                lambda * c->normal[2]};
            mvfxBodyPush(b, r, p, false);
        }
    }
    if (friction)
    {
        Rub(s, b, items, count);
    }
}

// Passes of the restitution's sequential impulses.
#define RESTITUTION_PASSES 8

// Passes of the contacts by position: the first finds their loads, the
// rest add the static friction, so its test (holding the slide at most
// the static coefficient times the load) is of the whole load. Tested on
// the first pass's share of it, a box held on a heightfield's slope slid.
#define POSITION_PASSES 3

// One pass of restitution, for each contact its positions pushed: its
// speed into the surface turned back at the share, none below still; then
// the patches' dynamic friction. By sequential impulses (Catto, "Iterative
// Dynamics with Temporal Coherence", GDC 2005): each contact's impulses
// summed over the passes, so contacts struck at once (an edge, a face, a
// body on another) share the bounce rather than each raising the others'
// past their own, and with its push's (its multiplier over h) held to 0 or
// more: a contact cannot pull, but the restitution can take back what the
// push gave too much. Held to 0 alone, that was left as the speed of a
// corner pushed out, and a box at rest on a slope rocked between its edge
// and its face, creeping down it.
static void SolveVelocities(mvfxBody* b, mvfxBodyContact* items, int32_t count,
                            const mvfxRigidSolve* s, bool backward)
{
    for (int32_t step = 0; step < count; ++step)
    {
        mvfxBodyContact* c = &items[mvfxPassIndex(0, count, step, backward)];
        if (!(c->lambda > 0.0f))
        {
            continue;
        }
        float r[3];
        float v[3];
        mvfxRotate(b->q, c->body, r);
        mvfxBodyPointVelocity(b, r, v);
        float normal = Dot(v, c->normal);
        float target = mvfxBounceTarget(s, c->speed);
        float total = fmaxf(c->applied + (target - normal) / mvfxBodyWeight(b, r, c->normal),
                            -c->lambda / s->h);
        float j = total - c->applied;
        c->applied = total;
        const float impulse[3] = {j * c->normal[0], j * c->normal[1], j * c->normal[2]};
        mvfxBodyKick(b, r, impulse);
    }
    Slow(s, b, items, count);
}

// Each contact's normal speed at the predicted pose, before it is met.
static void SpeedsOf(const mvfxBody* b, mvfxBodyContact* items, int32_t count)
{
    for (int32_t i = 0; i < count; ++i)
    {
        mvfxBodyContact* c = &items[i];
        float r[3];
        float v[3];
        mvfxRotate(b->q, c->body, r);
        mvfxBodyPointVelocity(b, r, v);
        c->speed = Dot(v, c->normal);
    }
}

// Body i's scene plane, the host's last answer to its query; false for
// none.
static bool ScenePlaneOf(const mvfxRigidSolve* s, int32_t i, mvfxResolvedCollider* out)
{
    mvfxInstance* instance = s->instance;
    if (!mvfxHasAttribute(&instance->effect->emitters[s->emitter].plan, mvfx_attributeScenePointX))
    {
        return false;
    }
    *out = (mvfxResolvedCollider){.kind = mvfx_colliderPlane};
    for (int32_t k = 0; k < 3; ++k)
    {
        out->center[k] = ((const float*)mvfxAttributeArray(
            instance, s->emitter, (mvfxAttribute)(mvfx_attributeScenePointX + k)))[i];
        out->normal[k] = ((const float*)mvfxAttributeArray(
            instance, s->emitter, (mvfxAttribute)(mvfx_attributeSceneNormalX + k)))[i];
    }
    return out->normal[0] != 0.0f || out->normal[1] != 0.0f || out->normal[2] != 0.0f;
}

// Body i's mass from its shape, its contacts with the world at its
// predicted pose and their speeds; outside the depth image it is killed
// when the collision def says.
static void Prepare(const mvfxRigidSolve* s, int32_t i)
{
    mvfxInstance* instance = s->instance;
    mvfxEmitterState* state = &instance->emitters[s->emitter];
    float size[3];
    mvfxBodyMass mass;
    mvfxSizeOf(instance, s->emitter, i, size);
    mvfxBodyShape shape = mvfxBodyShapeOf(s->rigid, s->hull, size, &mass);
    // Asleep, immovable to the bodies meeting it, and meeting nothing.
    bool asleep = mvfxBodyAsleep(s, i);
    if (asleep)
    {
        mass = (mvfxBodyMass){0};
    }
    s->mass[i] = mass.inverseMass;
    for (int32_t k = 0; k < 6; ++k)
    {
        s->mass[(size_t)(1 + k) * s->stride + (size_t)i] = mass.inverseInertia[k];
    }
    s->mass[MVFX_RIGID_REACH * s->stride + (size_t)i] = mvfxBodyReach(&shape);
    s->flags[i] &= (uint8_t)~(MVFX_RIGID_TOUCHED | MVFX_RIGID_STRUCK);
    mvfxBody b = mvfxLoadBody(s, i);
    const mvfxCollision* collision = &instance->effect->emitters[s->emitter].collision;
    if (state->hasDepth && collision->killOutsideView && !mvfxDepthSees(&state->depth, b.x))
    {
        instance->killScratch[i] = 1;
    }
    state->rigidCounts[i] = 0;
    if (asleep)
    {
        return;
    }
    const mvfxBodyPose pose = {.x = {b.x[0], b.x[1], b.x[2]},
                               .q = {b.q[0], b.q[1], b.q[2], b.q[3]}};
    const mvfxBodyPose from = {.x = {b.x0[0], b.x0[1], b.x0[2]},
                               .q = {b.q0[0], b.q0[1], b.q0[2], b.q0[3]}};
    mvfxResolvedCollider plane;
    const mvfxBodySurfaces surfaces = {
        .colliders = state->colliders,
        .count = state->colliderCount,
        .plane = ScenePlaneOf(s, i, &plane) ? &plane : nullptr,
        .depth = state->hasDepth ? &state->depth : nullptr,
        .from = &from,
    };
    mvfxBodyContacts list = {.count = 0};
    mvfxGatherBodyContacts(&surfaces, &pose, &shape, &list);
    state->rigidContactsCapped += (uint64_t)list.capped;
    SpeedsOf(&b, list.items, list.count);
    mvfxBodyContact* items = state->rigidContacts + (size_t)i * MVFX_MAX_RIGID_CONTACTS;
    memcpy(items, list.items, (size_t)list.count * sizeof list.items[0]);
    state->rigidCounts[i] = list.count;
    s->flags[i] |= list.count > 0 ? MVFX_RIGID_TOUCHED : 0;
}

// Body i's contacts with the world and their count.
static mvfxBodyContact* WorldOf(const mvfxRigidSolve* s, int32_t i, int32_t* count)
{
    mvfxEmitterState* state = &s->instance->emitters[s->emitter];
    *count = state->rigidCounts[i];
    return state->rigidContacts + (size_t)i * MVFX_MAX_RIGID_CONTACTS;
}

// A pass of a stage over bodies first to end's contacts with the world.
static void WorldPass(const mvfxRigidSolve* s, int32_t first, int32_t end, mvfxContactStage stage,
                      bool backward)
{
    for (int32_t step = first; step < end; ++step)
    {
        int32_t i = mvfxPassIndex(first, end, step, backward);
        int32_t count = 0;
        mvfxBodyContact* items = WorldOf(s, i, &count);
        if (count == 0)
        {
            continue;
        }
        mvfxBody b = mvfxLoadBody(s, i);
        if (stage == mvfx_stageStabilize)
        {
            Stabilize(&b, items, count, backward);
        }
        else if (stage == mvfx_stageBounce)
        {
            SolveVelocities(&b, items, count, s, backward);
        }
        else
        {
            SolvePositions(s, &b, items, count, stage == mvfx_stagePush, backward);
        }
        mvfxStoreBody(s, i, &b);
    }
}

// The passes of a stage over the world's contacts and the pairs', the
// world's first in forward passes and last in backward ones. Pressing,
// the passes after the first add the static friction.
static void Passes(const mvfxRigidSolve* s, int32_t first, int32_t end, mvfxContactStage stage,
                   int32_t passes)
{
    for (int32_t pass = 0; pass < passes; ++pass)
    {
        bool backward = pass % 2 == 1;
        mvfxContactStage now = stage == mvfx_stagePress && pass > 0 ? mvfx_stagePush : stage;
        if (!backward)
        {
            WorldPass(s, first, end, now, false);
        }
        mvfxPairPass(s, now, backward);
        if (backward)
        {
            WorldPass(s, first, end, now, true);
        }
    }
}

// Meets the bodies' contacts at their predicted poses: the world's and
// each other's by position, velocities from the poses' change for those
// touched, then restitution, then strikes.
static void Meet(const mvfxRigidSolve* s, int32_t first, int32_t end, bool pairs)
{
    for (int32_t i = first; i < end; ++i)
    {
        Prepare(s, i);
    }
    mvfxEmitterState* state = &s->instance->emitters[s->emitter];
    state->rigidPairCount = 0;
    if (pairs)
    {
        mvfxFindBodyPairs(s, end);
    }
    // A touched body's prediction as symplectic Euler's (Muller's): its
    // place its start's plus its velocity times h. The integrator's exact
    // place is a half acceleration short of it, and velocities from the
    // pose's change then lost half of each substep's gain (a body could
    // not start sliding down a slope) and multipliers half their impulse.
    for (int32_t i = first; i < end; ++i)
    {
        for (int32_t k = 0; k < 3 && (s->flags[i] & MVFX_RIGID_TOUCHED) != 0; ++k)
        {
            s->x[k][i] = s->start[(size_t)k * s->stride + (size_t)i] + s->v[k][i] * s->h;
        }
    }
    Passes(s, first, end, mvfx_stageStabilize, POSITION_PASSES);
    Passes(s, first, end, mvfx_stagePress, POSITION_PASSES);
    for (int32_t i = first; i < end; ++i)
    {
        if ((s->flags[i] & MVFX_RIGID_TOUCHED) != 0)
        {
            mvfxBody b = mvfxLoadBody(s, i);
            mvfxCorrectBody(&b, s->h);
            mvfxStoreBody(s, i, &b);
        }
    }
    Passes(s, first, end, mvfx_stageBounce, RESTITUTION_PASSES);
    for (int32_t i = first; i < end; ++i)
    {
        int32_t count = 0;
        const mvfxBodyContact* items = WorldOf(s, i, &count);
        for (int32_t j = 0; j < count; ++j)
        {
            if (items[j].lambda > 0.0f && items[j].speed < -s->still)
            {
                mvfxStrikeBody(s, i, items[j].surface, items[j].normal);
                break;
            }
        }
    }
    mvfxStrikePairs(s);
}

// A symmetric matrix (xx, yy, zz, xy, xz, yz) times v.
static void Times(const float m[6], const float v[3], float out[3])
{
    out[0] = m[0] * v[0] + m[3] * v[1] + m[4] * v[2];
    out[1] = m[3] * v[0] + m[1] * v[1] + m[5] * v[2];
    out[2] = m[4] * v[0] + m[5] * v[1] + m[2] * v[2];
}

// Turns each body's angular velocity by the gyroscopic term over h, about
// its own axes: w += h I^-1 (I w x w).
static void Spin(const mvfxRigidSolve* s, int32_t first, int32_t end)
{
    for (int32_t i = first; i < end; ++i)
    {
        if (mvfxBodyAsleep(s, i))
        {
            continue;
        }
        float size[3];
        mvfxBodyMass mass;
        mvfxSizeOf(s->instance, s->emitter, i, size);
        (void)mvfxBodyShapeOf(s->rigid, s->hull, size, &mass);
        float q[4] = {s->q[0][i], s->q[1][i], s->q[2][i], s->q[3][i]};
        const float world[3] = {s->w[0][i], s->w[1][i], s->w[2][i]};
        float w[3];
        ToBody(q, world, w);
        float momentum[3];
        Times(mass.inertia, w, momentum);
        float torque[3];
        Cross(momentum, w, torque);
        float before = sqrtf(Dot(momentum, momentum));
        float turn[3];
        Times(mass.inverseInertia, torque, turn);
        for (int32_t k = 0; k < 3; ++k)
        {
            w[k] += s->h * turn[k];
        }
        Times(mass.inertia, w, momentum);
        // The term turns the momentum without changing its size: held to
        // it, the explicit step cannot grow however fast the spin.
        float after = sqrtf(Dot(momentum, momentum));
        for (int32_t k = 0; after > 0.0f && k < 3; ++k)
        {
            w[k] *= before / after;
        }
        float turned[3];
        mvfxRotate(q, w, turned);
        for (int32_t k = 0; k < 3; ++k)
        {
            s->w[k][i] = turned[k];
        }
    }
}

static mvfxRigidSolve SolveOf(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    mvfxEmitterState* state = &instance->emitters[emitter];
    mvfxRigidSolve s = {.instance = instance,
                        .emitter = emitter,
                        .rigid = &compiled->rigid,
                        .hull = compiled->hull,
                        .start = state->rigidStart,
                        .mass = state->rigidMass,
                        .flags = state->rigidFlags,
                        .stride = (size_t)state->capacity,
                        .collides = compiled->collision.enabled};
    float gravity[3];
    mvfxGravityOf(instance, compiled, gravity);
    s.gravity = sqrtf(Dot(gravity, gravity));
    for (int32_t k = 0; k < 3; ++k)
    {
        s.x[k] =
            mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributePositionX + k));
        s.v[k] =
            mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeVelocityX + k));
        s.w[k] = mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeAngularX + k));
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        s.q[k] =
            mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeOrientationX + k));
    }
    if (mvfxHasAttribute(&compiled->plan, mvfx_attributeSleep))
    {
        s.sleep = mvfxAttributeArray(instance, emitter, mvfx_attributeSleep);
        for (int32_t k = 0; k < 3; ++k)
        {
            s.rest[k] =
                mvfxAttributeArray(instance, emitter, (mvfxAttribute)(mvfx_attributeRestX + k));
        }
    }
    return s;
}

// Moves bodies first to end over a substep from tick from to to, meeting
// each other when pairs.
static void Substep(mvfxRigidSolve* s, int32_t first, int32_t end, int64_t from, int64_t to,
                    bool pairs)
{
    double seconds = (double)(to - from) / (double)MVFX_TICKS_PER_SECOND;
    s->h = (float)seconds;
    s->still = 2.0f * s->gravity * s->h;
    s->tick = to;
    size_t count = (size_t)(end - first);
    for (int32_t k = 0; k < 3; ++k)
    {
        memcpy(s->start + (size_t)k * s->stride + (size_t)first, s->x[k] + first,
               count * sizeof(float));
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        memcpy(s->start + (size_t)(3 + k) * s->stride + (size_t)first, s->q[k] + first,
               count * sizeof(float));
    }
    Spin(s, first, end);
    mvfxIntegrateRange(s->instance, s->emitter, first, end, from, seconds);
    if (s->sleep != nullptr)
    {
        mvfxHoldSleeping(s, first, end);
    }
    if (s->collides)
    {
        Meet(s, first, end, pairs);
    }
}

// The step's substep k: from its tick to the next.
static int64_t SubstepTick(const mvfxInstance* instance, int32_t substeps, int32_t k)
{
    int64_t ticks = instance->clock - instance->stepStart;
    return instance->stepStart + ticks * k / substeps;
}

void mvfxStepRigid(mvfxInstance* instance, int32_t emitter)
{
    mvfxEmitterState* state = &instance->emitters[emitter];
    int32_t count = state->count;
    mvfxRigidSolve s = SolveOf(instance, emitter);
    memset(state->rigidFlags, 0, (size_t)count);
    if (s.sleep != nullptr && state->worldMoved)
    {
        mvfxWakeAll(&s, count);
    }
    for (int32_t k = 0; k < s.rigid->substeps; ++k)
    {
        int64_t from = SubstepTick(instance, s.rigid->substeps, k);
        int64_t to = SubstepTick(instance, s.rigid->substeps, k + 1);
        if (to > from)
        {
            Substep(&s, 0, count, from, to, true);
        }
    }
    if (s.sleep != nullptr)
    {
        int64_t ticks = instance->clock - instance->stepStart;
        mvfxSettleBodies(&s, count, (float)((double)ticks / (double)MVFX_TICKS_PER_SECOND));
    }
}

void mvfxIntegrateRigidNewborn(mvfxInstance* instance, int32_t emitter, int32_t at,
                               int64_t birthTick)
{
    instance->emitters[emitter].rigidFlags[at] = 0;
    mvfxRigidSolve s = SolveOf(instance, emitter);
    bool moved = false;
    for (int32_t k = 0; k < s.rigid->substeps; ++k)
    {
        int64_t from = SubstepTick(instance, s.rigid->substeps, k);
        int64_t to = SubstepTick(instance, s.rigid->substeps, k + 1);
        from = from > birthTick ? from : birthTick;
        if (to > from)
        {
            Substep(&s, at, at + 1, from, to, false);
            moved = true;
        }
    }
    if (!moved)
    {
        // Born at the step's end: still tested by the kill rules there.
        mvfxIntegrateNewborn(instance, emitter, at, birthTick);
    }
}
