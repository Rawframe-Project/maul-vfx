// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "rigid_contact.h"

#include "depth.h"
#include "direction.h"

#include <math.h>
#include <string.h>

static float Dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void ToBody(const float q[4], const float v[3], float out[3])
{
    const float inverse[4] = {-q[0], -q[1], -q[2], q[3]};
    mvfxRotate(inverse, v, out);
}

mvfxBodyShape mvfxBodyShapeOf(const mvfxRigidDef* rigid, const float size[3], float* inverseMass,
                              float inverseInertia[3])
{
    mvfxBodyShape s = {.kind = rigid->shape, .radius = 0.5f * fabsf(size[0])};
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
    *inverseMass = mass > 0.0 ? (float)(1.0 / mass) : 1.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        inverseInertia[k] = inertia[k] > 0.0 ? (float)(1.0 / inertia[k]) : 0.0f;
    }
    return s;
}

// Adds a contact deeper than 0; past the cap the shallowest is dropped
// and counted.
static void Add(mvfxBodyContacts* list, const mvfxBodyContact* c)
{
    if (!(c->depth < 0.0f))
    {
        return;
    }
    mvfxBodyContact added = *c;
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
static mvfxBodyContact ContactAt(const float point[3], const float world[3], const float n[3],
                                 float depth)
{
    mvfxBodyContact c = {.depth = depth};
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
static void SurfacePoint(const mvfxBodyPose* b, const mvfxBodyShape* s, const float point[3],
                         const float way[3], float body[3], float world[3])
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

static void PlaneContacts(const mvfxBodyPose* b, const mvfxBodyShape* s,
                          const mvfxResolvedCollider* c, mvfxBodyContacts* list)
{
    const float down[3] = {-c->normal[0], -c->normal[1], -c->normal[2]};
    for (int32_t i = 0; i < s->count; ++i)
    {
        float body[3];
        float world[3];
        SurfacePoint(b, s, s->points[i], down, body, world);
        float gap[3] = {world[0] - c->center[0], world[1] - c->center[1], world[2] - c->center[2]};
        mvfxBodyContact contact = ContactAt(body, world, c->normal, Dot(gap, c->normal));
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
static void InsideContacts(const mvfxBodyPose* b, const mvfxBodyShape* s,
                           const mvfxResolvedCollider* c, mvfxBodyContacts* list)
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
        mvfxBodyContact contact = ContactAt(body, world, in, c->radius - distance - s->radius);
        Add(list, &contact);
    }
}

// To a box collider's frame and back.
static void ToBox(const mvfxResolvedCollider* c, const float p[3], float out[3])
{
    const float d[3] = {p[0] - c->center[0], p[1] - c->center[1], p[2] - c->center[2]};
    ToBody(c->rotation, d, out);
}

static void FromBox(const mvfxResolvedCollider* c, const float p[3], float out[3])
{
    mvfxRotate(c->rotation, p, out);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] += c->center[k];
    }
}

// A box's signed distance at a point in its frame (negative within), and
// the unit way out of it there: from its nearest point, or for a point
// within, its nearest face's (the first of a tie).
static float BoxDistance(const float h[3], const float p[3], float way[3])
{
    float near[3];
    bool inside = true;
    for (int32_t k = 0; k < 3; ++k)
    {
        near[k] = fminf(fmaxf(p[k], -h[k]), h[k]);
        inside = inside && near[k] == p[k];
    }
    if (!inside)
    {
        return WayFrom(near, p, way);
    }
    int32_t face = 0;
    for (int32_t k = 1; k < 3; ++k)
    {
        face = h[k] - fabsf(p[k]) < h[face] - fabsf(p[face]) ? k : face;
    }
    way[0] = 0.0f;
    way[1] = 0.0f;
    way[2] = 0.0f;
    way[face] = p[face] >= 0.0f ? 1.0f : -1.0f;
    return -(h[face] - fabsf(p[face]));
}

// A sphere's center or a capsule's core at t, 0 at its end down its Y and
// 1 at the other, in its frame.
static void CoreAt(const mvfxBodyShape* s, float t, float out[3])
{
    out[0] = 0.0f;
    out[1] = s->half[1] * (2.0f * t - 1.0f);
    out[2] = 0.0f;
}

// The signed distance of the core's point at t from a box collider, its
// world place and the way out in the world.
static float CoreDistance(const mvfxBodyPose* b, const mvfxBodyShape* s,
                          const mvfxResolvedCollider* c, float t, float world[3], float n[3])
{
    float core[3];
    CoreAt(s, t, core);
    mvfxRotate(b->q, core, world);
    for (int32_t k = 0; k < 3; ++k)
    {
        world[k] += b->x[k];
    }
    float local[3];
    float way[3];
    ToBox(c, world, local);
    float distance = BoxDistance(c->halfExtents, local, way);
    mvfxRotate(c->rotation, way, n);
    return distance;
}

static void CoreContact(const mvfxBodyPose* b, const mvfxBodyShape* s,
                        const mvfxResolvedCollider* c, float t, mvfxBodyContacts* list)
{
    float world[3];
    float n[3];
    float distance = CoreDistance(b, s, c, t, world, n);
    float core[3];
    float down[3];
    const float in[3] = {-s->radius * n[0], -s->radius * n[1], -s->radius * n[2]};
    CoreAt(s, t, core);
    ToBody(b->q, in, down);
    float point[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        point[k] = core[k] + down[k];
        world[k] += in[k];
    }
    mvfxBodyContact contact = ContactAt(point, world, n, distance - s->radius);
    Add(list, &contact);
}

// A sphere or a capsule kept out of a box: its core's ends and its
// deepest point between (the box's signed distance is convex along the
// core, so a ternary search finds it).
static void CoreBoxContacts(const mvfxBodyPose* b, const mvfxBodyShape* s,
                            const mvfxResolvedCollider* c, mvfxBodyContacts* list)
{
    if (s->kind == mvfx_rigidSphere)
    {
        CoreContact(b, s, c, 0.5f, list);
        return;
    }
    float low = 0.0f;
    float high = 1.0f;
    for (int32_t pass = 0; pass < 32; ++pass)
    {
        float t0 = low + (high - low) / 3.0f;
        float t1 = high - (high - low) / 3.0f;
        float world[3];
        float n[3];
        bool lower = CoreDistance(b, s, c, t0, world, n) <= CoreDistance(b, s, c, t1, world, n);
        high = lower ? t1 : high;
        low = lower ? low : t0;
    }
    float deepest = 0.5f * (low + high);
    CoreContact(b, s, c, 0.0f, list);
    CoreContact(b, s, c, 1.0f, list);
    if (deepest > 1e-3f && deepest < 1.0f - 1e-3f)
    {
        CoreContact(b, s, c, deepest, list);
    }
    // Where the core crosses the box's faces' planes, so a capsule lying
    // on a box rests on a line, not a point.
    float ends[2][3];
    for (int32_t e = 0; e < 2; ++e)
    {
        float world[3];
        float n[3];
        (void)CoreDistance(b, s, c, (float)e, world, n);
        ToBox(c, world, ends[e]);
    }
    for (int32_t f = 0; f < 6; ++f)
    {
        int32_t k = f / 2;
        float span = ends[1][k] - ends[0][k];
        float plane = f % 2 == 0 ? c->halfExtents[k] : -c->halfExtents[k];
        float at = span != 0.0f ? (plane - ends[0][k]) / span : -1.0f;
        if (at > 1e-3f && at < 1.0f - 1e-3f)
        {
            CoreContact(b, s, c, at, list);
        }
    }
}

// A box kept inside a box: each face a plane, each its own patch.
static void InsideBoxContacts(const mvfxBodyPose* b, const mvfxBodyShape* s,
                              const mvfxResolvedCollider* c, mvfxBodyContacts* list)
{
    int32_t patch = list->collider;
    for (int32_t f = 0; f < 6; ++f)
    {
        int32_t k = f / 2;
        float axis[3] = {0.0f, 0.0f, 0.0f};
        axis[k] = f % 2 == 0 ? 1.0f : -1.0f;
        float out[3];
        mvfxRotate(c->rotation, axis, out);
        mvfxResolvedCollider wall = {.kind = mvfx_colliderPlane};
        for (int32_t j = 0; j < 3; ++j)
        {
            wall.normal[j] = -out[j];
            wall.center[j] = c->center[j] + c->halfExtents[k] * out[j];
        }
        list->collider = patch + f;
        PlaneContacts(b, s, &wall, list);
    }
    list->collider = patch;
}

// The box's point nearest a sphere's center (in its frame) and the way out
// it is pushed; for a center inside, the nearest face's.
static float BoxNearest(const mvfxBodyShape* s, const float center[3], float point[3], float way[3])
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
static void OutsideContacts(const mvfxBodyPose* b, const mvfxBodyShape* s,
                            const mvfxResolvedCollider* c, mvfxBodyContacts* list)
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
    mvfxBodyContact contact = ContactAt(point, world, n, distance - c->radius);
    Add(list, &contact);
}

// A box body in a box collider's frame: the collider's half sizes, the
// body's, its center and its axes.
typedef struct BoxPair
{
    const float* a;
    const float* h;
    float t[3];
    float axes[3][3];
} BoxPair;

static void Cross(const float a[3], const float b[3], float out[3])
{
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

// The gap between the boxes along unit l (negative where they overlap).
static float Separation(const BoxPair* p, const float l[3])
{
    float reach = 0.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        reach += p->a[k] * fabsf(l[k]) + p->h[k] * fabsf(Dot(p->axes[k], l));
    }
    return fabsf(Dot(p->t, l)) - reach;
}

// The axis the boxes overlap least along (the collider's faces 0 to 2,
// the body's 3 to 5, edge pairs 6 on), unit and from the collider toward
// the body; false when one parts them. Edges win only by a clear margin,
// so resting faces keep face contacts.
static bool LeastAxis(const BoxPair* p, float n[3], int32_t* axis)
{
    float best = -INFINITY;
    for (int32_t i = 0; i < 6; ++i)
    {
        float l[3] = {0.0f, 0.0f, 0.0f};
        l[i % 3] = 1.0f;
        const float* way = i < 3 ? l : p->axes[i - 3];
        float gap = Separation(p, way);
        if (gap > 0.0f)
        {
            return false;
        }
        if (gap > best)
        {
            best = gap;
            *axis = i;
            memcpy(n, way, 3 * sizeof(float));
        }
    }
    float face = best;
    for (int32_t i = 0; i < 9; ++i)
    {
        float e[3] = {0.0f, 0.0f, 0.0f};
        float l[3];
        e[i / 3] = 1.0f;
        Cross(e, p->axes[i % 3], l);
        if (!(Dot(l, l) > 1e-10f) || !mvfxNormalize(l))
        {
            continue;
        }
        float gap = Separation(p, l);
        if (gap > 0.0f)
        {
            return false;
        }
        if (gap > 0.95f * face && gap > best)
        {
            best = gap;
            *axis = 6 + i;
            memcpy(n, l, sizeof l);
        }
    }
    float sign = Dot(p->t, n) < 0.0f ? -1.0f : 1.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        n[k] *= sign;
    }
    return true;
}

// Clips a polygon of count points (at most 8) to dot(x, l) <= limit.
static int32_t Clip(float polygon[8][3], int32_t count, const float l[3], float limit)
{
    float out[8][3];
    int32_t kept = 0;
    for (int32_t i = 0; i < count; ++i)
    {
        const float* a = polygon[i];
        const float* b = polygon[(i + 1) % count];
        float da = Dot(a, l) - limit;
        float db = Dot(b, l) - limit;
        if (da <= 0.0f && kept < 8)
        {
            memcpy(out[kept++], a, 3 * sizeof(float));
        }
        if ((da < 0.0f) != (db < 0.0f) && da != db && kept < 8)
        {
            float f = da / (da - db);
            for (int32_t k = 0; k < 3; ++k)
            {
                out[kept][k] = a[k] + f * (b[k] - a[k]);
            }
            ++kept;
        }
    }
    memcpy(polygon, out, (size_t)kept * sizeof out[0]);
    return kept;
}

// A box's face (center, two half spans) as four corners.
static void FaceOf(const float center[3], const float u[3], const float v[3], float out[8][3])
{
    static const float signs[4][2] = {{1, 1}, {-1, 1}, {-1, -1}, {1, -1}};
    for (int32_t i = 0; i < 4; ++i)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            out[i][k] = center[k] + signs[i][0] * u[k] + signs[i][1] * v[k];
        }
    }
}

// Adds the contact of the body's point at p and the collider's surface
// point s (both in the collider's frame) along n there.
static void PairContact(const mvfxBodyPose* b, const mvfxResolvedCollider* c, const float p[3],
                        const float s[3], const float n[3], mvfxBodyContacts* list)
{
    float world[3];
    float n0[3];
    float gap[3] = {p[0] - s[0], p[1] - s[1], p[2] - s[2]};
    FromBox(c, p, world);
    mvfxRotate(c->rotation, n, n0);
    float offset[3] = {world[0] - b->x[0], world[1] - b->x[1], world[2] - b->x[2]};
    float point[3];
    ToBody(b->q, offset, point);
    mvfxBodyContact contact = ContactAt(point, world, n0, Dot(gap, n));
    Add(list, &contact);
}

// The body's face most against n clipped by the collider's face k.
static void BodyFaceOnCollider(const mvfxBodyPose* b, const mvfxResolvedCollider* c,
                               const BoxPair* p, const float n[3], int32_t k,
                               mvfxBodyContacts* list)
{
    int32_t j = 0;
    for (int32_t m = 1; m < 3; ++m)
    {
        j = fabsf(Dot(p->axes[m], n)) > fabsf(Dot(p->axes[j], n)) ? m : j;
    }
    float side = Dot(p->axes[j], n) > 0.0f ? -1.0f : 1.0f;
    float center[3];
    float u[3];
    float v[3];
    for (int32_t i = 0; i < 3; ++i)
    {
        center[i] = p->t[i] + side * p->h[j] * p->axes[j][i];
        u[i] = p->h[(j + 1) % 3] * p->axes[(j + 1) % 3][i];
        v[i] = p->h[(j + 2) % 3] * p->axes[(j + 2) % 3][i];
    }
    float polygon[8][3];
    FaceOf(center, u, v, polygon);
    int32_t count = 4;
    for (int32_t m = 1; m < 3; ++m)
    {
        float l[3] = {0.0f, 0.0f, 0.0f};
        l[(k + m) % 3] = 1.0f;
        count = Clip(polygon, count, l, p->a[(k + m) % 3]);
        l[(k + m) % 3] = -1.0f;
        count = Clip(polygon, count, l, p->a[(k + m) % 3]);
    }
    for (int32_t i = 0; i < count; ++i)
    {
        float depth = Dot(polygon[i], n) - p->a[k];
        const float s[3] = {polygon[i][0] - depth * n[0], polygon[i][1] - depth * n[1],
                            polygon[i][2] - depth * n[2]};
        PairContact(b, c, polygon[i], s, n, list);
    }
}

// The collider's face most along n clipped by the body's face j.
static void ColliderFaceOnBody(const mvfxBodyPose* b, const mvfxResolvedCollider* c,
                               const BoxPair* p, const float n[3], int32_t j,
                               mvfxBodyContacts* list)
{
    int32_t k = 0;
    for (int32_t m = 1; m < 3; ++m)
    {
        k = fabsf(n[m]) > fabsf(n[k]) ? m : k;
    }
    float center[3] = {0.0f, 0.0f, 0.0f};
    float u[3] = {0.0f, 0.0f, 0.0f};
    float v[3] = {0.0f, 0.0f, 0.0f};
    center[k] = n[k] >= 0.0f ? p->a[k] : -p->a[k];
    u[(k + 1) % 3] = p->a[(k + 1) % 3];
    v[(k + 2) % 3] = p->a[(k + 2) % 3];
    float polygon[8][3];
    FaceOf(center, u, v, polygon);
    int32_t count = 4;
    for (int32_t m = 1; m < 3; ++m)
    {
        const float* l = p->axes[(j + m) % 3];
        float reach = p->h[(j + m) % 3];
        const float back[3] = {-l[0], -l[1], -l[2]};
        count = Clip(polygon, count, l, Dot(p->t, l) + reach);
        count = Clip(polygon, count, back, Dot(p->t, back) + reach);
    }
    for (int32_t i = 0; i < count; ++i)
    {
        const float r[3] = {polygon[i][0] - p->t[0], polygon[i][1] - p->t[1],
                            polygon[i][2] - p->t[2]};
        float g = Dot(r, n) + p->h[j];
        const float onBody[3] = {polygon[i][0] - g * n[0], polygon[i][1] - g * n[1],
                                 polygon[i][2] - g * n[2]};
        PairContact(b, c, onBody, polygon[i], n, list);
    }
}

// The nearest points of the collider's edge along its axis i and the
// body's along its axis j that reach furthest toward each other along n.
static void EdgeContact(const mvfxBodyPose* b, const mvfxResolvedCollider* c, const BoxPair* p,
                        const float n[3], int32_t i, int32_t j, mvfxBodyContacts* list)
{
    float pa[3];
    float pb[3] = {p->t[0], p->t[1], p->t[2]};
    float da[3] = {0.0f, 0.0f, 0.0f};
    da[i] = 1.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        pa[k] = k == i ? 0.0f : (n[k] >= 0.0f ? p->a[k] : -p->a[k]);
    }
    for (int32_t m = 0; m < 3; ++m)
    {
        float side = m == j ? 0.0f : (Dot(p->axes[m], n) >= 0.0f ? -p->h[m] : p->h[m]);
        for (int32_t k = 0; k < 3; ++k)
        {
            pb[k] += side * p->axes[m][k];
        }
    }
    const float* db = p->axes[j];
    float r[3] = {pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]};
    float e = Dot(da, db);
    float denominator = 1.0f - e * e;
    float s = denominator > 1e-6f ? (Dot(r, da) - e * Dot(r, db)) / denominator : 0.0f;
    s = fminf(fmaxf(s, -p->a[i]), p->a[i]);
    float u = fminf(fmaxf(s * e - Dot(r, db), -p->h[j]), p->h[j]);
    s = fminf(fmaxf(Dot(r, da) + u * e, -p->a[i]), p->a[i]);
    float onCollider[3];
    float onBody[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        onCollider[k] = pa[k] + s * da[k];
        onBody[k] = pb[k] + u * db[k];
    }
    PairContact(b, c, onBody, onCollider, n, list);
}

// A box kept out of a box: the separating axes (Gottschalk, Lin and
// Manocha, "OBBTree", SIGGRAPH 1996), then a face clipped by the other's
// (as ODE's dBoxBox) or two edges' nearest points.
static void BoxBoxContacts(const mvfxBodyPose* b, const mvfxBodyShape* s,
                           const mvfxResolvedCollider* c, mvfxBodyContacts* list)
{
    BoxPair p = {.a = c->halfExtents, .h = s->half};
    ToBox(c, b->x, p.t);
    for (int32_t j = 0; j < 3; ++j)
    {
        float e[3] = {0.0f, 0.0f, 0.0f};
        float turned[3];
        e[j] = 1.0f;
        mvfxRotate(b->q, e, turned);
        ToBody(c->rotation, turned, p.axes[j]);
    }
    float n[3];
    int32_t axis = 0;
    if (!LeastAxis(&p, n, &axis))
    {
        return;
    }
    if (axis < 3)
    {
        BodyFaceOnCollider(b, c, &p, n, axis, list);
    }
    else if (axis < 6)
    {
        ColliderFaceOnBody(b, c, &p, n, axis - 3, list);
    }
    else
    {
        EdgeContact(b, c, &p, n, (axis - 6) / 3, (axis - 6) % 3, list);
    }
}

// The most points a shape is sampled at against a grid or a depth image.
#define MOST_SAMPLES 14

// The points (in its frame) a shape meets a grid or a depth image by, as
// spheres of its radius: a sphere's center; a capsule's core, its points
// at most its radius apart (at most nine); a box's eight corners (bit k
// of i set for +X, +Y, +Z), then its faces' centers (8 + 2k for +k, 9 +
// 2k for -k).
static int32_t BoxSamples(const mvfxBodyShape* s, float out[MOST_SAMPLES][3])
{
    for (int32_t i = 0; i < 14; ++i)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            float corner = (i >> k & 1) != 0 ? s->half[k] : -s->half[k];
            float side = (i - 8) % 2 == 0 ? s->half[k] : -s->half[k];
            out[i][k] = i < 8 ? corner : ((i - 8) / 2 == k ? side : 0.0f);
        }
    }
    return 14;
}

static int32_t SamplesOf(const mvfxBodyShape* s, float out[MOST_SAMPLES][3])
{
    if (s->kind == mvfx_rigidBox)
    {
        return BoxSamples(s, out);
    }
    int32_t spans = s->kind == mvfx_rigidCapsule && s->radius > 0.0f
                        ? (int32_t)ceilf(fminf(2.0f * s->half[1] / s->radius, 8.0f))
                        : 0;
    for (int32_t i = 0; i <= spans; ++i)
    {
        CoreAt(s, spans > 0 ? (float)i / (float)spans : 0.5f, out[i]);
    }
    return spans + 1;
}

// A sample's place in the world.
static void SampleAt(const mvfxBodyPose* b, const float point[3], float out[3])
{
    mvfxRotate(b->q, point, out);
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] += b->x[k];
    }
}

// The contact of a sample at world, of the shape's radius, with a surface
// along unit n at the sample's center's distance.
static mvfxBodyContact SampleContact(const mvfxBodyPose* b, const mvfxBodyShape* s,
                                     const float point[3], const float world[3], const float n[3],
                                     float distance)
{
    const float in[3] = {-s->radius * n[0], -s->radius * n[1], -s->radius * n[2]};
    float down[3];
    ToBody(b->q, in, down);
    const float body[3] = {point[0] + down[0], point[1] + down[1], point[2] + down[2]};
    const float place[3] = {world[0] + in[0], world[1] + in[1], world[2] + in[2]};
    return ContactAt(body, place, n, distance - s->radius);
}

// Adds the samples' contacts that are met: all but a box's faces'
// centers, each met only where the surface bulges under it, deeper than
// each corner of its face. On a flat surface the corners carry a face; a
// fifth contact amid them shifts the load the one pass gives each and
// unsettled a box held on a slope.
static void AddSamples(const mvfxBodyShape* s, const mvfxBodyContact* contacts, const bool* found,
                       int32_t count, mvfxBodyContacts* list)
{
    for (int32_t i = 0; i < count; ++i)
    {
        bool met = found[i];
        if (s->kind == mvfx_rigidBox && i >= 8)
        {
            int32_t k = (i - 8) / 2;
            int32_t side = (i - 8) % 2 == 0 ? 1 : 0;
            for (int32_t c = 0; met && c < 8; ++c)
            {
                bool onFace = (c >> k & 1) == side;
                met = !onFace || !found[c] || contacts[i].depth < contacts[c].depth;
            }
        }
        if (met)
        {
            Add(list, &contacts[i]);
        }
    }
}

// A heightfield or a distance grid, by its field at the samples.
static void GridContacts(const mvfxBodyPose* b, const mvfxBodyShape* s,
                         const mvfxResolvedCollider* c, mvfxBodyContacts* list)
{
    float samples[MOST_SAMPLES][3];
    mvfxBodyContact contacts[MOST_SAMPLES];
    bool found[MOST_SAMPLES];
    int32_t count = SamplesOf(s, samples);
    for (int32_t i = 0; i < count; ++i)
    {
        float world[3];
        float n[3];
        SampleAt(b, samples[i], world);
        float distance = mvfxFieldDistance(c, world, n);
        found[i] = distance < INFINITY;
        contacts[i] = found[i] ? SampleContact(b, s, samples[i], world, n, distance)
                               : (mvfxBodyContact){.depth = INFINITY};
    }
    AddSamples(s, contacts, found, count, list);
}

// The depth image, by each sample's way over the substep: its first place
// within twice the shape's radius of the surface or behind it (within the
// thickness), its depth then across the surface at its normal. The image
// measures how far behind along the camera's ray, which off its axis is
// longer than across the surface: by the radius alone a sphere sank 1.6
// mm before it was seen. Twice it sees rays up to 60 degrees from the
// normal, and samples not touching are dropped by their depth.
static void DepthContacts(const mvfxBodySurfaces* surfaces, const mvfxBodyPose* b,
                          const mvfxBodyShape* s, mvfxBodyContacts* list)
{
    mvfxResolvedDepth depth = *surfaces->depth;
    depth.radius = 2.0f * s->radius;
    float samples[MOST_SAMPLES][3];
    mvfxBodyContact contacts[MOST_SAMPLES];
    bool found[MOST_SAMPLES];
    int32_t count = SamplesOf(s, samples);
    for (int32_t i = 0; i < count; ++i)
    {
        float from[3];
        float world[3];
        float t = 0.0f;
        float point[3] = {0.0f, 0.0f, 0.0f};
        float n[3] = {0.0f, 1.0f, 0.0f};
        SampleAt(surfaces->from, samples[i], from);
        SampleAt(b, samples[i], world);
        found[i] = mvfxDepthHit(&depth, from, world, &t, point, n);
        const float gap[3] = {world[0] - point[0], world[1] - point[1], world[2] - point[2]};
        contacts[i] = found[i]
                          ? SampleContact(b, s, samples[i], world, n, Dot(gap, n) + depth.radius)
                          : (mvfxBodyContact){.depth = INFINITY};
    }
    AddSamples(s, contacts, found, count, list);
}

// Patches by collider, eight apart for a box's faces.
static void ColliderContacts(const mvfxResolvedCollider* c, const mvfxBodyPose* b,
                             const mvfxBodyShape* s, mvfxBodyContacts* list)
{
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
    else if (c->kind == mvfx_colliderBox && c->inside)
    {
        InsideBoxContacts(b, s, c, list);
    }
    else if (c->kind == mvfx_colliderBox && s->kind == mvfx_rigidBox)
    {
        BoxBoxContacts(b, s, c, list);
    }
    else if (c->kind == mvfx_colliderBox)
    {
        CoreBoxContacts(b, s, c, list);
    }
    else
    {
        GridContacts(b, s, c, list);
    }
}

void mvfxGatherBodyContacts(const mvfxBodySurfaces* surfaces, const mvfxBodyPose* b,
                            const mvfxBodyShape* s, mvfxBodyContacts* list)
{
    for (int32_t i = 0; i < surfaces->count; ++i)
    {
        list->collider = 8 * i;
        ColliderContacts(&surfaces->colliders[i], b, s, list);
    }
    list->collider = 8 * surfaces->count;
    if (surfaces->plane != nullptr)
    {
        PlaneContacts(b, s, surfaces->plane, list);
    }
    list->collider = 8 * surfaces->count + 8;
    if (surfaces->depth != nullptr)
    {
        DepthContacts(surfaces, b, s, list);
    }
}
