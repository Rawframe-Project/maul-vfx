// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "rigid.h"

#include "appearance.h"
#include "collide.h"
#include "depth.h"
#include "direction.h"
#include "event.h"
#include "integrate.h"
#include "rigid_contact.h"

#include <math.h>
#include <string.h>

// A body as a substep solves it: its pose and velocities, the pose it
// started the substep at, its inverse mass and its inverse inertias about
// its own axes.
typedef struct Body
{
    float x[3];
    float q[4];
    float v[3];
    float w[3];
    float x0[3];
    float q0[4];
    float inverseMass;
    float inverseInertia[3];
} Body;

// An emitter's rigid particles in a substep.
typedef struct Solve
{
    mvfxInstance* instance;
    int32_t emitter;
    const mvfxRigidDef* rigid;
    float* x[3];
    float* v[3];
    float* q[4];
    float* w[3];
    float* start;
    size_t stride;
    // Whether they collide, and gravity's size.
    bool collides;
    float gravity;
    float h;
    // Below this speed into a surface a contact does not bounce (twice
    // gravity's speed over a substep).
    float still;
    int64_t tick;
} Solve;

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

// The inverse inertia of the body turned by q times v, in the world.
static void InverseInertiaAt(const Body* b, const float q[4], const float v[3], float out[3])
{
    float local[3];
    ToBody(q, v, local);
    for (int32_t k = 0; k < 3; ++k)
    {
        local[k] *= b->inverseInertia[k];
    }
    mvfxRotate(q, local, out);
}

static void InverseInertiaTimes(const Body* b, const float v[3], float out[3])
{
    InverseInertiaAt(b, b->q, v, out);
}

// The generalized inverse mass at offset r along unit n.
static float WeightAt(const Body* b, const float r[3], const float n[3])
{
    float c[3];
    float turned[3];
    Cross(r, n, c);
    InverseInertiaTimes(b, c, turned);
    return b->inverseMass + Dot(c, turned);
}

// Turns q by a turn vector to first order (q + [turn, 0] q / 2),
// normalized.
static void TurnBy(float q[4], const float turn[3])
{
    const float spin[4] = {turn[0], turn[1], turn[2], 0.0f};
    float dq[4];
    mvfxMultiplyRotations(spin, q, dq);
    float length = 0.0f;
    for (int32_t k = 0; k < 4; ++k)
    {
        q[k] += 0.5f * dq[k];
        length += q[k] * q[k];
    }
    length = sqrtf(length);
    for (int32_t k = 0; k < 4; ++k)
    {
        q[k] /= length;
    }
}

// Moves the body by a positional impulse p at offset r: its place by p
// over its mass, its orientation by the turn it gives. With both, its
// start pose by the very same step too, so the change between them (its
// velocity to be) is left as it was.
static void Push(Body* b, const float r[3], const float p[3], bool both)
{
    float c[3];
    float turn[3];
    Cross(r, p, c);
    InverseInertiaTimes(b, c, turn);
    TurnBy(b->q, turn);
    if (both)
    {
        TurnBy(b->q0, turn);
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        b->x[k] += p[k] * b->inverseMass;
        b->x0[k] += both ? p[k] * b->inverseMass : 0.0f;
    }
}

// Changes the body's velocities by an impulse j at offset r.
static void Kick(Body* b, const float r[3], const float j[3])
{
    float c[3];
    float turn[3];
    Cross(r, j, c);
    InverseInertiaTimes(b, c, turn);
    for (int32_t k = 0; k < 3; ++k)
    {
        b->v[k] += j[k] * b->inverseMass;
        b->w[k] += turn[k];
    }
}

// A contact's depth at the body's start pose.
static float StartDepthOf(const Body* b, const mvfxBodyContact* c)
{
    float r[3];
    mvfxRotate(b->q0, c->body, r);
    float gap[3] = {b->x0[0] + r[0] - c->surface[0], b->x0[1] + r[1] - c->surface[1],
                    b->x0[2] + r[2] - c->surface[2]};
    return Dot(gap, c->normal);
}

// A contact's offset from the body's center in the world, and its depth
// at the body's pose.
static float DepthOf(const Body* b, const mvfxBodyContact* c, float r[3])
{
    mvfxRotate(b->q, c->body, r);
    float gap[3] = {b->x[0] + r[0] - c->surface[0], b->x[1] + r[1] - c->surface[1],
                    b->x[2] + r[2] - c->surface[2]};
    return Dot(gap, c->normal);
}

// The friction of a body's patch of contacts with one collider (PhysX's
// patch friction): at the contacts' center weighted by their normal
// corrections, along their weighted normal, the patch's slide over the
// substep is cancelled while static friction holds it (the correction at
// most the static coefficient times the load), else slowed by the dynamic
// coefficient times the load.
static void Rub(Body* b, const mvfxBodyContacts* list, int32_t collider, const mvfxRigidDef* rigid)
{
    float load = 0.0f;
    float point[3] = {0.0f, 0.0f, 0.0f};
    float n[3] = {0.0f, 0.0f, 0.0f};
    for (int32_t i = 0; i < list->count; ++i)
    {
        const mvfxBodyContact* c = &list->items[i];
        float lambda = c->collider == collider && c->lambda > 0.0f ? c->lambda : 0.0f;
        load += lambda;
        for (int32_t k = 0; k < 3; ++k)
        {
            point[k] += lambda * c->body[k];
            n[k] += lambda * c->normal[k];
        }
    }
    if (!(load > 0.0f) || !mvfxNormalize(n))
    {
        return;
    }
    float r[3];
    float before[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        point[k] /= load;
    }
    mvfxRotate(b->q, point, r);
    mvfxRotate(b->q0, point, before);
    float t[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        t[k] = b->x[k] + r[k] - (b->x0[k] + before[k]);
    }
    float along = Dot(t, n);
    for (int32_t k = 0; k < 3; ++k)
    {
        t[k] -= along * n[k];
    }
    float length = sqrtf(Dot(t, t));
    if (!(length > 0.0f))
    {
        return;
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        t[k] /= length;
    }
    float needed = length / WeightAt(b, r, t);
    float correction = needed <= rigid->staticFriction * load
                           ? needed
                           : fminf(needed, rigid->dynamicFriction * load);
    const float p[3] = {-correction * t[0], -correction * t[1], -correction * t[2]};
    Push(b, r, p, false);
}

static void SolvePositions(Body* b, mvfxBodyContacts* list, const mvfxRigidDef* rigid)
{
    for (int32_t i = 0; i < list->count; ++i)
    {
        mvfxBodyContact* c = &list->items[i];
        float r[3];
        float depth = DepthOf(b, c, r);
        if (depth < 0.0f)
        {
            float lambda = -depth / WeightAt(b, r, c->normal);
            c->lambda += lambda;
            // The share of the depth there at the substep's start moves the
            // start pose by the same step, so pushing out of it gives no
            // velocity (post-stabilization, Cline and Pai 2003): left over
            // from before, born into or met by a moving collider, it does
            // not pop out however short the substep.
            float before = fmaxf(fminf(StartDepthOf(b, c), 0.0f), depth);
            float share = before < 0.0f ? before / depth : 0.0f;
            const float old[3] = {share * lambda * c->normal[0], share * lambda * c->normal[1],
                                  share * lambda * c->normal[2]};
            const float fresh[3] = {(1.0f - share) * lambda * c->normal[0],
                                    (1.0f - share) * lambda * c->normal[1],
                                    (1.0f - share) * lambda * c->normal[2]};
            Push(b, r, old, true);
            Push(b, r, fresh, false);
        }
    }
    // Each collider's patch once, in the order first met.
    for (int32_t i = 0; i < list->count; ++i)
    {
        bool first = true;
        for (int32_t j = 0; first && j < i; ++j)
        {
            first = list->items[j].collider != list->items[i].collider;
        }
        if (first)
        {
            Rub(b, list, list->items[i].collider, rigid);
        }
    }
}

// The velocity of the body's point at offset r.
static void PointVelocity(const Body* b, const float r[3], float out[3])
{
    Cross(b->w, r, out);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] += b->v[k];
    }
}

// Passes of the restitution's sequential impulses.
#define RESTITUTION_PASSES 4

// Restitution, for each contact its positions pushed: its speed into the
// surface turned back at the share, none below still. By sequential
// impulses (Catto, "Iterative Dynamics with Temporal Coherence", GDC
// 2005): each contact's impulses summed and held to 0 or more over the
// passes, so contacts struck at once (an edge, a face) share the bounce
// rather than each raising the others' past their own.
static void SolveVelocities(Body* b, const mvfxBodyContacts* list, const Solve* s)
{
    float applied[MVFX_MAX_RIGID_CONTACTS] = {0.0f};
    for (int32_t pass = 0; pass < RESTITUTION_PASSES; ++pass)
    {
        for (int32_t i = 0; i < list->count; ++i)
        {
            const mvfxBodyContact* c = &list->items[i];
            if (!(c->lambda > 0.0f))
            {
                continue;
            }
            float r[3];
            float v[3];
            mvfxRotate(b->q, c->body, r);
            PointVelocity(b, r, v);
            float normal = Dot(v, c->normal);
            float bounce = c->speed < -s->still ? s->rigid->restitution : 0.0f;
            float target = -bounce * fminf(c->speed, 0.0f);
            float total = fmaxf(applied[i] + (target - normal) / WeightAt(b, r, c->normal), 0.0f);
            float j = total - applied[i];
            applied[i] = total;
            const float impulse[3] = {j * c->normal[0], j * c->normal[1], j * c->normal[2]};
            Kick(b, r, impulse);
        }
    }
}

// Each contact's normal speed at the predicted pose, before it is met.
static void SpeedsOf(const Body* b, mvfxBodyContacts* list)
{
    for (int32_t i = 0; i < list->count; ++i)
    {
        mvfxBodyContact* c = &list->items[i];
        float r[3];
        float v[3];
        mvfxRotate(b->q, c->body, r);
        PointVelocity(b, r, v);
        c->speed = Dot(v, c->normal);
    }
}

// A body in contact's velocities from its pose's change over the
// substep, so one held still is still.
static void Correct(Body* b, float h)
{
    const float inverse[4] = {-b->q0[0], -b->q0[1], -b->q0[2], b->q0[3]};
    float dq[4];
    mvfxMultiplyRotations(b->q, inverse, dq);
    float sign = dq[3] < 0.0f ? -1.0f : 1.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        b->v[k] = (b->x[k] - b->x0[k]) / h;
        b->w[k] = sign * 2.0f * dq[k] / h;
    }
}

static Body Load(const Solve* s, int32_t i)
{
    Body b = {0};
    for (int32_t k = 0; k < 3; ++k)
    {
        b.x[k] = s->x[k][i];
        b.v[k] = s->v[k][i];
        b.w[k] = s->w[k][i];
        b.x0[k] = s->start[(size_t)k * s->stride + (size_t)i];
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        b.q[k] = s->q[k][i];
        b.q0[k] = s->start[(size_t)(3 + k) * s->stride + (size_t)i];
    }
    return b;
}

static void Store(const Solve* s, int32_t i, const Body* b)
{
    for (int32_t k = 0; k < 3; ++k)
    {
        s->x[k][i] = b->x[k];
        s->v[k][i] = b->v[k];
        s->w[k][i] = b->w[k];
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        s->q[k][i] = b->q[k];
    }
}

// The life a collision takes: its share of the life left, the particle
// killed past its age.
static bool LoseLife(const Solve* s, int32_t i, float loss)
{
    mvfxInstance* instance = s->instance;
    float* inverseLife = mvfxAttributeArray(instance, s->emitter, mvfx_attributeInverseLife);
    const uint32_t* low = mvfxAttributeArray(instance, s->emitter, mvfx_attributeBirthLow);
    const uint32_t* high = mvfxAttributeArray(instance, s->emitter, mvfx_attributeBirthHigh);
    inverseLife[i] /= 1.0f - loss;
    int64_t birth = (int64_t)(((uint64_t)high[i] << 32u) | low[i]);
    double age = (double)(s->tick - birth) / (double)MVFX_TICKS_PER_SECOND;
    return age * (double)inverseLife[i] >= 1.0;
}

// A body that struck a surface (a contact met, faster into it than
// still): counted, its step's event, its kills and life loss.
static void Strike(const Solve* s, int32_t i, const mvfxBodyContacts* list)
{
    int32_t first = -1;
    for (int32_t j = 0; first < 0 && j < list->count; ++j)
    {
        first = list->items[j].lambda > 0.0f && list->items[j].speed < -s->still ? j : -1;
    }
    if (first < 0)
    {
        return;
    }
    mvfxInstance* instance = s->instance;
    mvfxEmitterState* state = &instance->emitters[s->emitter];
    const mvfxCollision* collision = &instance->effect->emitters[s->emitter].collision;
    uint32_t* count = mvfxAttributeArray(instance, s->emitter, mvfx_attributeCollisions);
    count[i] = count[i] == UINT32_MAX ? UINT32_MAX : count[i] + 1u;
    if (state->rigidTouched[i] == 0)
    {
        state->rigidTouched[i] = 1;
        const mvfxBodyContact* c = &list->items[first];
        mvfxRecordEvent(instance, s->emitter,
                        &(mvfxEventFact){.kind = mvfx_eventCollision,
                                         .particle = i,
                                         .tick = s->tick,
                                         .placeTick = s->tick,
                                         .point = c->surface,
                                         .normal = c->normal});
    }
    bool killed = collision->killOnCollision || collision->lifeLoss >= 1.0f ||
                  (collision->lifeLoss > 0.0f && LoseLife(s, i, collision->lifeLoss));
    instance->killScratch[i] |= killed ? 1 : 0;
}

// Body i's scene plane, the host's last answer to its query; false for
// none.
static bool ScenePlaneOf(const Solve* s, int32_t i, mvfxResolvedCollider* out)
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

// Meets body i's contacts at its predicted pose; outside the depth image
// it is killed when the collision def says.
static void SolveBody(const Solve* s, int32_t i)
{
    mvfxInstance* instance = s->instance;
    mvfxEmitterState* state = &instance->emitters[s->emitter];
    Body b = Load(s, i);
    const mvfxCollision* collision = &instance->effect->emitters[s->emitter].collision;
    if (state->hasDepth && collision->killOutsideView && !mvfxDepthSees(&state->depth, b.x))
    {
        instance->killScratch[i] = 1;
    }
    float size[3];
    mvfxSizeOf(instance, s->emitter, i, size);
    mvfxBodyShape shape = mvfxBodyShapeOf(s->rigid, size, &b.inverseMass, b.inverseInertia);
    mvfxBodyContacts list = {.count = 0};
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
    mvfxGatherBodyContacts(&surfaces, &pose, &shape, &list);
    state->rigidContactsCapped += (uint64_t)list.capped;
    if (list.count == 0)
    {
        return;
    }
    SpeedsOf(&b, &list);
    SolvePositions(&b, &list, s->rigid);
    Correct(&b, s->h);
    SolveVelocities(&b, &list, s);
    Store(s, i, &b);
    Strike(s, i, &list);
}

// Turns each body's angular velocity by the gyroscopic term over h, about
// its own axes: w += h I^-1 (I w x w).
static void Spin(const Solve* s, int32_t first, int32_t end)
{
    for (int32_t i = first; i < end; ++i)
    {
        Body b = Load(s, i);
        float size[3];
        mvfxSizeOf(s->instance, s->emitter, i, size);
        (void)mvfxBodyShapeOf(s->rigid, size, &b.inverseMass, b.inverseInertia);
        float w[3];
        ToBody(b.q, b.w, w);
        float momentum[3];
        for (int32_t k = 0; k < 3; ++k)
        {
            momentum[k] = b.inverseInertia[k] > 0.0f ? w[k] / b.inverseInertia[k] : 0.0f;
        }
        float torque[3];
        Cross(momentum, w, torque);
        float before = sqrtf(Dot(momentum, momentum));
        for (int32_t k = 0; k < 3; ++k)
        {
            w[k] += s->h * b.inverseInertia[k] * torque[k];
            momentum[k] = b.inverseInertia[k] > 0.0f ? w[k] / b.inverseInertia[k] : 0.0f;
        }
        // The term turns the momentum without changing its size: held to
        // it, the explicit step cannot grow however fast the spin.
        float after = sqrtf(Dot(momentum, momentum));
        for (int32_t k = 0; after > 0.0f && k < 3; ++k)
        {
            w[k] *= before / after;
        }
        float turned[3];
        mvfxRotate(b.q, w, turned);
        for (int32_t k = 0; k < 3; ++k)
        {
            s->w[k][i] = turned[k];
        }
    }
}

static Solve SolveOf(mvfxInstance* instance, int32_t emitter)
{
    const mvfxCompiledEmitter* compiled = &instance->effect->emitters[emitter];
    mvfxEmitterState* state = &instance->emitters[emitter];
    Solve s = {.instance = instance,
               .emitter = emitter,
               .rigid = &compiled->rigid,
               .start = state->rigidStart,
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
    return s;
}

// Moves bodies first to end over a substep from tick from to to.
static void Substep(Solve* s, int32_t first, int32_t end, int64_t from, int64_t to)
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
    for (int32_t i = first; s->collides && i < end; ++i)
    {
        SolveBody(s, i);
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
    Solve s = SolveOf(instance, emitter);
    memset(state->rigidTouched, 0, (size_t)count);
    for (int32_t k = 0; k < s.rigid->substeps; ++k)
    {
        int64_t from = SubstepTick(instance, s.rigid->substeps, k);
        int64_t to = SubstepTick(instance, s.rigid->substeps, k + 1);
        if (to > from)
        {
            Substep(&s, 0, count, from, to);
        }
    }
}

void mvfxIntegrateRigidNewborn(mvfxInstance* instance, int32_t emitter, int32_t at,
                               int64_t birthTick)
{
    instance->emitters[emitter].rigidTouched[at] = 0;
    Solve s = SolveOf(instance, emitter);
    bool moved = false;
    for (int32_t k = 0; k < s.rigid->substeps; ++k)
    {
        int64_t from = SubstepTick(instance, s.rigid->substeps, k);
        int64_t to = SubstepTick(instance, s.rigid->substeps, k + 1);
        from = from > birthTick ? from : birthTick;
        if (to > from)
        {
            Substep(&s, at, at + 1, from, to);
            moved = true;
        }
    }
    if (!moved)
    {
        // Born at the step's end: still tested by the kill rules there.
        mvfxIntegrateNewborn(instance, emitter, at, birthTick);
    }
}
