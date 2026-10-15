// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "rigid.h"

#include "appearance.h"
#include "collide.h"
#include "direction.h"
#include "event.h"
#include "integrate.h"

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

// A body's shape as spheres about points in its frame (a sphere's center,
// a capsule's two ends, a box's eight corners at radius 0), with a box's
// half sizes and a capsule's half length.
typedef struct Shape
{
    mvfxRigidShape kind;
    float points[8][3];
    int32_t count;
    float radius;
    float half[3];
    float reach;
} Shape;

// A contact: the body's point in its frame, the surface point it meets
// and the surface's normal (toward the body's side), its depth when found,
// the multiplier its normal corrections summed and its normal speed then.
typedef struct Contact
{
    float body[3];
    float surface[3];
    float normal[3];
    float depth;
    float lambda;
    float speed;
    // The collider it meets, by its index.
    int32_t collider;
} Contact;

typedef struct Contacts
{
    Contact items[MVFX_MAX_RIGID_CONTACTS];
    int32_t count;
    int32_t capped;
    // The collider the contacts being added meet.
    int32_t collider;
} Contacts;

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

// Moves a pose of the body (x, q) by a positional impulse p at offset r:
// its place by p over its mass, its orientation by half the turn it gives.
static void PushPose(const Body* b, float x[3], float q[4], const float r[3], const float p[3])
{
    float c[3];
    float turn[3];
    Cross(r, p, c);
    InverseInertiaAt(b, q, c, turn);
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
    for (int32_t k = 0; k < 3; ++k)
    {
        x[k] += p[k] * b->inverseMass;
    }
}

static void Push(Body* b, const float r[3], const float p[3])
{
    PushPose(b, b->x, b->q, r, p);
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

// The shape a size gives (in the particles' space): spheres about its
// points, and its mass and inertias from the density.
static Shape ShapeOf(const mvfxRigidDef* rigid, const float size[3], Body* body)
{
    Shape s = {.kind = rigid->shape, .radius = 0.5f * fabsf(size[0])};
    double r = (double)s.radius;
    double pi = 3.14159265358979;
    double mass = (double)rigid->density * 4.0 / 3.0 * pi * r * r * r;
    double inertia[3] = {0.4 * mass * r * r, 0.4 * mass * r * r, 0.4 * mass * r * r};
    s.count = 1;
    if (s.kind == mvfx_rigidCapsule)
    {
        double length = fmax((double)fabsf(size[1]) - 2.0 * r, 0.0);
        double tube = (double)rigid->density * pi * r * r * length;
        double ends = mass;
        mass = tube + ends;
        inertia[1] = tube * r * r / 2.0 + ends * 0.4 * r * r;
        inertia[0] = tube * (length * length / 12.0 + r * r / 4.0) +
                     ends * (0.4 * r * r + length * length / 4.0 + 3.0 * length * r / 8.0);
        inertia[2] = inertia[0];
        s.half[1] = (float)(0.5 * length);
        s.points[0][1] = s.half[1];
        s.points[1][1] = -s.half[1];
        s.count = 2;
    }
    else if (s.kind == mvfx_rigidBox)
    {
        double a = (double)fabsf(size[0]);
        double b = (double)fabsf(size[1]);
        double c = (double)fabsf(size[2]);
        mass = (double)rigid->density * a * b * c;
        inertia[0] = mass * (b * b + c * c) / 12.0;
        inertia[1] = mass * (a * a + c * c) / 12.0;
        inertia[2] = mass * (a * a + b * b) / 12.0;
        s.radius = 0.0f;
        s.count = 8;
        for (int32_t k = 0; k < 3; ++k)
        {
            s.half[k] = 0.5f * fabsf(size[k]);
        }
        for (int32_t i = 0; i < 8; ++i)
        {
            for (int32_t k = 0; k < 3; ++k)
            {
                s.points[i][k] = (i >> k & 1) != 0 ? s.half[k] : -s.half[k];
            }
        }
    }
    // A shape of no volume is a point of unit mass that does not turn.
    body->inverseMass = mass > 0.0 ? (float)(1.0 / mass) : 1.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        body->inverseInertia[k] = inertia[k] > 0.0 ? (float)(1.0 / inertia[k]) : 0.0f;
    }
    return s;
}

// Adds a contact deeper than 0; past the cap the shallowest is dropped
// and counted.
static void Add(Contacts* list, const Contact* c)
{
    if (!(c->depth < 0.0f))
    {
        return;
    }
    Contact added = *c;
    added.collider = list->collider;
    if (list->count < MVFX_MAX_RIGID_CONTACTS)
    {
        list->items[list->count++] = added;
        return;
    }
    list->capped += 1;
    int32_t shallow = 0;
    for (int32_t j = 1; j < list->count; ++j)
    {
        shallow = list->items[j].depth > list->items[shallow].depth ? j : shallow;
    }
    if (c->depth < list->items[shallow].depth)
    {
        list->items[shallow] = added;
    }
}

// A contact of the body's point (in its frame, its place world) meeting
// a surface along unit n at depth.
static Contact ContactAt(const float point[3], const float world[3], const float n[3], float depth)
{
    Contact c = {.depth = depth};
    for (int32_t k = 0; k < 3; ++k)
    {
        c.body[k] = point[k];
        c.normal[k] = n[k];
        c.surface[k] = world[k] - depth * n[k];
    }
    return c;
}

// The world place of the body's point at the shape's surface along way
// (a sphere's point out by its radius).
static void SurfacePoint(const Body* b, const Shape* s, const float point[3], const float way[3],
                         float body[3], float world[3])
{
    float turned[3];
    mvfxRotate(b->q, point, turned);
    float local[3];
    ToBody(b->q, way, local);
    for (int32_t k = 0; k < 3; ++k)
    {
        world[k] = b->x[k] + turned[k] + s->radius * way[k];
        body[k] = point[k] + s->radius * local[k];
    }
}

static void PlaneContacts(const Body* b, const Shape* s, const mvfxResolvedCollider* c,
                          Contacts* list)
{
    const float down[3] = {-c->normal[0], -c->normal[1], -c->normal[2]};
    for (int32_t i = 0; i < s->count; ++i)
    {
        float body[3];
        float world[3];
        SurfacePoint(b, s, s->points[i], down, body, world);
        float gap[3] = {world[0] - c->center[0], world[1] - c->center[1], world[2] - c->center[2]};
        Contact contact = ContactAt(body, world, c->normal, Dot(gap, c->normal));
        Add(list, &contact);
    }
}

// The unit way from a to b, or up where they meet; and their distance.
static float WayFrom(const float a[3], const float b[3], float out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = b[k] - a[k];
    }
    float distance = sqrtf(Dot(out, out));
    if (!(distance > 0.0f))
    {
        out[0] = 0.0f;
        out[1] = 1.0f;
        out[2] = 0.0f;
        return 0.0f;
    }
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] /= distance;
    }
    return distance;
}

// Keeping the shape's points within a sphere.
static void InsideContacts(const Body* b, const Shape* s, const mvfxResolvedCollider* c,
                           Contacts* list)
{
    for (int32_t i = 0; i < s->count; ++i)
    {
        float turned[3];
        mvfxRotate(b->q, s->points[i], turned);
        float center[3] = {b->x[0] + turned[0], b->x[1] + turned[1], b->x[2] + turned[2]};
        float out[3];
        float distance = WayFrom(c->center, center, out);
        float body[3];
        float world[3];
        SurfacePoint(b, s, s->points[i], out, body, world);
        const float in[3] = {-out[0], -out[1], -out[2]};
        Contact contact = ContactAt(body, world, in, c->radius - distance - s->radius);
        Add(list, &contact);
    }
}

// The box's point nearest a sphere's center (in its frame) and the way out
// it is pushed; for a center inside, the nearest face's.
static float BoxNearest(const Shape* s, const float center[3], float point[3], float way[3])
{
    bool inside = true;
    for (int32_t k = 0; k < 3; ++k)
    {
        point[k] = fminf(fmaxf(center[k], -s->half[k]), s->half[k]);
        inside = inside && point[k] == center[k];
    }
    if (!inside)
    {
        return WayFrom(center, point, way);
    }
    int32_t face = 0;
    for (int32_t k = 1; k < 3; ++k)
    {
        face = s->half[k] - fabsf(center[k]) < s->half[face] - fabsf(center[face]) ? k : face;
    }
    float side = center[face] >= 0.0f ? 1.0f : -1.0f;
    point[face] = side * s->half[face];
    way[0] = 0.0f;
    way[1] = 0.0f;
    way[2] = 0.0f;
    way[face] = -side;
    return -(s->half[face] - fabsf(center[face]));
}

// Keeping the shape out of a sphere: by the nearest point of a box, or of
// a sphere's or a capsule's core.
static void OutsideContacts(const Body* b, const Shape* s, const mvfxResolvedCollider* c,
                            Contacts* list)
{
    float gap[3] = {c->center[0] - b->x[0], c->center[1] - b->x[1], c->center[2] - b->x[2]};
    float center[3];
    ToBody(b->q, gap, center);
    float point[3] = {0.0f, 0.0f, 0.0f};
    float local[3];
    float distance = 0.0f;
    if (s->kind == mvfx_rigidBox)
    {
        distance = BoxNearest(s, center, point, local);
    }
    else
    {
        point[1] = fminf(fmaxf(center[1], -s->half[1]), s->half[1]);
        distance = WayFrom(center, point, local) - s->radius;
        for (int32_t k = 0; k < 3; ++k)
        {
            point[k] -= s->radius * local[k];
        }
    }
    float n[3];
    float turned[3];
    mvfxRotate(b->q, local, n);
    mvfxRotate(b->q, point, turned);
    float world[3] = {b->x[0] + turned[0], b->x[1] + turned[1], b->x[2] + turned[2]};
    Contact contact = ContactAt(point, world, n, distance - c->radius);
    Add(list, &contact);
}

static void Gather(const mvfxEmitterState* state, const Body* b, const Shape* s, Contacts* list)
{
    for (int32_t i = 0; i < state->colliderCount; ++i)
    {
        const mvfxResolvedCollider* c = &state->colliders[i];
        list->collider = i;
        if (c->kind == mvfx_colliderPlane)
        {
            PlaneContacts(b, s, c, list);
        }
        else if (c->kind == mvfx_colliderSphere && c->inside)
        {
            InsideContacts(b, s, c, list);
        }
        else if (c->kind == mvfx_colliderSphere)
        {
            OutsideContacts(b, s, c, list);
        }
    }
}

// A contact's offset from the body's center at its start pose, and its
// depth there.
static float StartDepthOf(const Body* b, const Contact* c, float r[3])
{
    mvfxRotate(b->q0, c->body, r);
    float gap[3] = {b->x0[0] + r[0] - c->surface[0], b->x0[1] + r[1] - c->surface[1],
                    b->x0[2] + r[2] - c->surface[2]};
    return Dot(gap, c->normal);
}

// A contact's offset from the body's center in the world, and its depth
// at the body's pose.
static float DepthOf(const Body* b, const Contact* c, float r[3])
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
static void Rub(Body* b, const Contacts* list, int32_t collider, const mvfxRigidDef* rigid)
{
    float load = 0.0f;
    float point[3] = {0.0f, 0.0f, 0.0f};
    float n[3] = {0.0f, 0.0f, 0.0f};
    for (int32_t i = 0; i < list->count; ++i)
    {
        const Contact* c = &list->items[i];
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
    Push(b, r, p);
}

static void SolvePositions(Body* b, Contacts* list, const mvfxRigidDef* rigid)
{
    for (int32_t i = 0; i < list->count; ++i)
    {
        Contact* c = &list->items[i];
        float r[3];
        float depth = DepthOf(b, c, r);
        if (depth < 0.0f)
        {
            float lambda = -depth / WeightAt(b, r, c->normal);
            c->lambda += lambda;
            // The share of the depth there at the substep's start moves its
            // start pose as well, so pushing out of it gives no velocity
            // (post-stabilization, Cline and Pai 2003): left over from
            // before, or born into, it does not pop out however short the
            // substep.
            float r0[3];
            float before = fmaxf(fminf(StartDepthOf(b, c, r0), 0.0f), depth);
            if (before < 0.0f)
            {
                float share = lambda * before / depth;
                const float p0[3] = {share * c->normal[0], share * c->normal[1],
                                     share * c->normal[2]};
                PushPose(b, b->x0, b->q0, r0, p0);
            }
            const float p[3] = {lambda * c->normal[0], lambda * c->normal[1],
                                lambda * c->normal[2]};
            Push(b, r, p);
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

// Restitution, for each contact its positions pushed: its speed into the
// surface turned back at the share, none below still.
static void SolveVelocities(Body* b, const Contacts* list, const Solve* s)
{
    for (int32_t i = 0; i < list->count; ++i)
    {
        const Contact* c = &list->items[i];
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
        if (normal < target)
        {
            float j = (target - normal) / WeightAt(b, r, c->normal);
            const float impulse[3] = {j * c->normal[0], j * c->normal[1], j * c->normal[2]};
            Kick(b, r, impulse);
        }
    }
}

// Each contact's normal speed at the predicted pose, before it is met.
static void SpeedsOf(const Body* b, Contacts* list)
{
    for (int32_t i = 0; i < list->count; ++i)
    {
        Contact* c = &list->items[i];
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
static void Strike(const Solve* s, int32_t i, const Contacts* list)
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
        const Contact* c = &list->items[first];
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

// Meets body i's contacts at its predicted pose.
static void SolveBody(const Solve* s, int32_t i)
{
    mvfxInstance* instance = s->instance;
    mvfxEmitterState* state = &instance->emitters[s->emitter];
    Body b = Load(s, i);
    float size[3];
    mvfxSizeOf(instance, s->emitter, i, size);
    Shape shape = ShapeOf(s->rigid, size, &b);
    Contacts list = {.count = 0};
    Gather(state, &b, &shape, &list);
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
        (void)ShapeOf(s->rigid, size, &b);
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
