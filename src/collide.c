// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "collide.h"

#include "direction.h"

#include <math.h>

static bool IsVectorWithin(const float v[3], float bound)
{
    return fabsf(v[0]) <= bound && fabsf(v[1]) <= bound && fabsf(v[2]) <= bound;
}

static bool IsPositive(float x)
{
    return x > 0.0f && x <= MVFX_MAX_EXTENT;
}

static bool IsShare(float x)
{
    return x >= 0.0f && x <= 1.0f;
}

static bool IsRotation(const float q[4])
{
    double square = 0.0;
    for (int32_t k = 0; k < 4; ++k)
    {
        if (!(fabsf(q[k]) <= 1.001f))
        {
            return false;
        }
        square += (double)q[k] * (double)q[k];
    }
    return fabs(sqrt(square) - 1.0) <= 0.001;
}

bool mvfxIsColliderValid(const mvfxCollider* c)
{
    if (c->space > mvfx_spaceLocal || !IsVectorWithin(c->center, MVFX_MAX_EXTENT))
    {
        return false;
    }
    float normal[3] = {c->normal[0], c->normal[1], c->normal[2]};
    switch (c->kind)
    {
    case mvfx_colliderPlane:
        return IsVectorWithin(c->normal, MVFX_MAX_EXTENT) && mvfxNormalize(normal);
    case mvfx_colliderSphere:
        return IsPositive(c->radius);
    case mvfx_colliderBox:
        return IsPositive(c->halfExtents[0]) && IsPositive(c->halfExtents[1]) &&
               IsPositive(c->halfExtents[2]) && IsRotation(c->rotation);
    default:
        return false;
    }
}

bool mvfxIsCollisionValid(const mvfxCollision* c)
{
    return !c->enabled ||
           (IsShare(c->restitution) && IsShare(c->friction) && IsShare(c->lifeLoss) &&
            c->restSpeed >= 0.0f && c->restSpeed <= MVFX_MAX_EXTENT && c->radius >= 0.0f &&
            c->radius <= MVFX_MAX_EXTENT);
}

mvfxCollider mvfxCompileCollider(const mvfxCollider* collider)
{
    mvfxCollider out = *collider;
    if (out.kind == mvfx_colliderPlane)
    {
        (void)mvfxNormalize(out.normal);
    }
    if (out.kind == mvfx_colliderBox)
    {
        double square = 0.0;
        for (int32_t k = 0; k < 4; ++k)
        {
            square += (double)out.rotation[k] * (double)out.rotation[k];
        }
        double inverse = 1.0 / sqrt(square);
        for (int32_t k = 0; k < 4; ++k)
        {
            out.rotation[k] = (float)((double)out.rotation[k] * inverse);
        }
    }
    return out;
}

mvfxResolvedCollider mvfxResolveCollider(const mvfxCollider* collider, const mvfxSpaceMap* map,
                                         float radius)
{
    float sigma = map->sigma;
    // Growing by the radius: a plane moves out along its normal, a sphere
    // outside grows and one inside shrinks (never below 0), a box grows
    // or shrinks by it on every axis.
    float grow = collider->inside ? -radius : radius;
    mvfxResolvedCollider out = {
        .kind = collider->kind,
        .inside = collider->inside,
        .radius = fmaxf(collider->radius * sigma + grow, 0.0f),
    };
    for (int32_t k = 0; k < 3; ++k)
    {
        out.halfExtents[k] = fmaxf(collider->halfExtents[k] * sigma + grow, 0.0f);
    }
    mvfxMapPoint(map, collider->center, out.center);
    mvfxMapVector(map, collider->normal, 1.0f, out.normal);
    mvfxMultiplyRotations(map->rotation, collider->rotation, out.rotation);
    if (collider->kind == mvfx_colliderPlane)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            out.center[k] += radius * out.normal[k];
        }
    }
    return out;
}

static float Dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// A contact: how far along the segment (0 for a start on the wrong side),
// where, and the surface's normal toward the allowed side.
typedef struct Hit
{
    float t;
    float point[3];
    float normal[3];
} Hit;

static void PointAt(const float s[3], const float e[3], float t, float out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = s[k] + t * (e[k] - s[k]);
    }
}

static bool HitPlane(const mvfxResolvedCollider* c, const float s[3], const float e[3], Hit* hit)
{
    float a[3] = {s[0] - c->center[0], s[1] - c->center[1], s[2] - c->center[2]};
    float b[3] = {e[0] - c->center[0], e[1] - c->center[1], e[2] - c->center[2]};
    float d0 = Dot(a, c->normal);
    float d1 = Dot(b, c->normal);
    for (int32_t k = 0; k < 3; ++k)
    {
        hit->normal[k] = c->normal[k];
    }
    if (d0 < 0.0f)
    {
        hit->t = 0.0f;
        for (int32_t k = 0; k < 3; ++k)
        {
            hit->point[k] = s[k] - d0 * c->normal[k];
        }
        return true;
    }
    if (!(d1 < 0.0f))
    {
        return false;
    }
    hit->t = d0 / (d0 - d1);
    PointAt(s, e, hit->t, hit->point);
    return true;
}

// A sphere's surface point along a direction from its center (up when
// the direction is 0), the normal outward or inward.
static void SpherePoint(const mvfxResolvedCollider* c, const float from[3], Hit* hit)
{
    float dir[3] = {from[0] - c->center[0], from[1] - c->center[1], from[2] - c->center[2]};
    if (!mvfxNormalize(dir))
    {
        dir[0] = 0.0f;
        dir[1] = 0.0f;
        dir[2] = 1.0f;
    }
    float sign = c->inside ? -1.0f : 1.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        hit->point[k] = c->center[k] + c->radius * dir[k];
        hit->normal[k] = sign * dir[k];
    }
}

static bool HitSphere(const mvfxResolvedCollider* c, const float s[3], const float e[3], Hit* hit)
{
    float f[3] = {s[0] - c->center[0], s[1] - c->center[1], s[2] - c->center[2]};
    float g[3] = {e[0] - c->center[0], e[1] - c->center[1], e[2] - c->center[2]};
    float d[3] = {e[0] - s[0], e[1] - s[1], e[2] - s[2]};
    float r2 = c->radius * c->radius;
    float ff = Dot(f, f);
    bool wrong = c->inside ? ff > r2 : ff < r2;
    if (wrong)
    {
        hit->t = 0.0f;
        SpherePoint(c, s, hit);
        return true;
    }
    bool crosses = c->inside ? Dot(g, g) > r2 : true;
    float a = Dot(d, d);
    if (!crosses || !(a > 0.0f))
    {
        return false;
    }
    float b = Dot(f, d);
    float disc = b * b - a * (ff - r2);
    if (!(disc >= 0.0f))
    {
        return false;
    }
    float root = sqrtf(disc);
    float t = c->inside ? (-b + root) / a : (-b - root) / a;
    if (!(t >= 0.0f && t <= 1.0f))
    {
        return false;
    }
    hit->t = t;
    float at[3];
    PointAt(s, e, t, at);
    SpherePoint(c, at, hit);
    return true;
}

// A box's contact in its own frame, from local start a to local end b.
static bool HitBoxLocal(const float h[3], bool inside, const float a[3], const float b[3],
                        Hit* hit);

static bool HitBox(const mvfxResolvedCollider* c, const float s[3], const float e[3], Hit* hit)
{
    const float* q = c->rotation;
    const float inverse[4] = {-q[0], -q[1], -q[2], q[3]};
    float ds[3] = {s[0] - c->center[0], s[1] - c->center[1], s[2] - c->center[2]};
    float de[3] = {e[0] - c->center[0], e[1] - c->center[1], e[2] - c->center[2]};
    float a[3];
    float b[3];
    mvfxRotate(inverse, ds, a);
    mvfxRotate(inverse, de, b);
    Hit local;
    if (!HitBoxLocal(c->halfExtents, c->inside, a, b, &local))
    {
        return false;
    }
    hit->t = local.t;
    mvfxRotate(q, local.point, hit->point);
    mvfxRotate(q, local.normal, hit->normal);
    for (int32_t k = 0; k < 3; ++k)
    {
        hit->point[k] += c->center[k];
    }
    return true;
}

// The axis a start inside a box is nearest a face on, or outside it
// farthest past one.
static int32_t PushAxis(const float h[3], const float a[3], bool inside)
{
    int32_t axis = 0;
    float best = inside ? -INFINITY : INFINITY;
    for (int32_t k = 0; k < 3; ++k)
    {
        float room = h[k] - fabsf(a[k]);
        bool better = inside ? -room > best : room < best;
        if (better)
        {
            best = inside ? -room : room;
            axis = k;
        }
    }
    return axis;
}

static bool IsInside(const float h[3], const float a[3])
{
    return fabsf(a[0]) <= h[0] && fabsf(a[1]) <= h[1] && fabsf(a[2]) <= h[2];
}

// A start on the wrong side: put on the surface.
static void PushOut(const float h[3], bool inside, const float a[3], Hit* hit)
{
    hit->t = 0.0f;
    hit->normal[0] = 0.0f;
    hit->normal[1] = 0.0f;
    hit->normal[2] = 0.0f;
    int32_t axis = PushAxis(h, a, inside);
    float sign = a[axis] < 0.0f ? -1.0f : 1.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        hit->point[k] = inside ? fminf(fmaxf(a[k], -h[k]), h[k]) : a[k];
    }
    hit->point[axis] = sign * h[axis];
    hit->normal[axis] = inside ? -sign : sign;
}

static bool HitBoxLocal(const float h[3], bool inside, const float a[3], const float b[3], Hit* hit)
{
    if (IsInside(h, a) != inside)
    {
        PushOut(h, inside, a, hit);
        return true;
    }
    if (inside && IsInside(h, b))
    {
        return false;
    }
    float low = 0.0f;
    float high = 1.0f;
    int32_t lowAxis = -1;
    int32_t highAxis = -1;
    for (int32_t k = 0; k < 3; ++k)
    {
        float d = b[k] - a[k];
        if (d == 0.0f)
        {
            if (fabsf(a[k]) > h[k])
            {
                return false;
            }
            continue;
        }
        float t1 = (-h[k] - a[k]) / d;
        float t2 = (h[k] - a[k]) / d;
        float near = fminf(t1, t2);
        float far = fmaxf(t1, t2);
        lowAxis = near > low ? k : lowAxis;
        low = fmaxf(low, near);
        highAxis = far < high ? k : highAxis;
        high = fminf(high, far);
    }
    int32_t axis = inside ? highAxis : lowAxis;
    if (low > high || axis < 0)
    {
        return false;
    }
    hit->t = inside ? high : low;
    PointAt(a, b, hit->t, hit->point);
    float d = b[axis] - a[axis];
    hit->normal[0] = 0.0f;
    hit->normal[1] = 0.0f;
    hit->normal[2] = 0.0f;
    hit->normal[axis] = d > 0.0f ? -1.0f : 1.0f;
    return true;
}

// The segment's earliest contact with any collider.
static bool FirstHit(const mvfxResolvedCollider* colliders, int32_t count, const float s[3],
                     const float e[3], Hit* best)
{
    bool found = false;
    for (int32_t i = 0; i < count; ++i)
    {
        Hit hit;
        const mvfxResolvedCollider* c = &colliders[i];
        bool met = c->kind == mvfx_colliderPlane    ? HitPlane(c, s, e, &hit)
                   : c->kind == mvfx_colliderSphere ? HitSphere(c, s, e, &hit)
                                                    : HitBox(c, s, e, &hit);
        if (met && (!found || hit.t < best->t))
        {
            *best = hit;
            found = true;
        }
    }
    return found;
}

// A vector bounced off a surface: its part into it reversed by
// restitution, its part along it cut by friction.
static void Bounce(const mvfxCollision* response, const float n[3], float v[3])
{
    float into = Dot(v, n);
    float normal = into < 0.0f ? -response->restitution * into : into;
    float keep = 1.0f - response->friction;
    for (int32_t k = 0; k < 3; ++k)
    {
        float along = v[k] - into * n[k];
        v[k] = normal * n[k] + keep * along;
    }
}

int32_t mvfxCollide(const mvfxResolvedCollider* colliders, int32_t count,
                    const mvfxCollision* response, float restSpeed, const float start[3],
                    float place[3], float velocity[3])
{
    float s[3] = {start[0], start[1], start[2]};
    int32_t contacts = 0;
    Hit hit;
    while (contacts < MVFX_MAX_CONTACTS && FirstHit(colliders, count, s, place, &hit))
    {
        contacts += 1;
        float rest[3];
        for (int32_t k = 0; k < 3; ++k)
        {
            rest[k] = (place[k] - s[k]) * (1.0f - hit.t);
        }
        Bounce(response, hit.normal, velocity);
        Bounce(response, hit.normal, rest);
        // Off the surface by a hair, so the next test does not meet it again.
        float hair =
            1e-6f * (1.0f + fabsf(hit.point[0]) + fabsf(hit.point[1]) + fabsf(hit.point[2]));
        bool stops = Dot(velocity, velocity) < restSpeed * restSpeed;
        for (int32_t k = 0; k < 3; ++k)
        {
            s[k] = hit.point[k] + hair * hit.normal[k];
            velocity[k] = stops ? 0.0f : velocity[k];
            place[k] = stops ? s[k] : s[k] + rest[k];
        }
        if (stops)
        {
            return contacts;
        }
    }
    if (contacts == MVFX_MAX_CONTACTS && FirstHit(colliders, count, s, place, &hit))
    {
        // Out of contacts with a path still into a surface: stop at it.
        for (int32_t k = 0; k < 3; ++k)
        {
            place[k] = hit.point[k];
        }
    }
    return contacts;
}
