// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "collide.h"

#include "depth.h"
#include "direction.h"
#include "grid.h"
#include "refusal.h"

#include <math.h>

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

// A half extent's status: above 0, and at most MVFX_MAX_EXTENT.
static mvfxResult CheckHalf(float x)
{
    return mvfxPositiveUpTo(x, MVFX_MAX_EXTENT);
}

mvfxResult mvfxCheckCollider(const mvfxCollider* c)
{
    if (c->space > mvfx_spaceLocal)
    {
        return mvfx_errorInvalid;
    }
    mvfxResult center = mvfxBoundedVector(c->center, MVFX_MAX_EXTENT);
    if (center != mvfx_success)
    {
        return center;
    }
    float normal[3] = {c->normal[0], c->normal[1], c->normal[2]};
    switch (c->kind)
    {
    case mvfx_colliderPlane:
    {
        mvfxResult within = mvfxBoundedVector(c->normal, MVFX_MAX_EXTENT);
        return within != mvfx_success ? within : mvfxRequire(mvfxNormalize(normal));
    }
    case mvfx_colliderSphere:
        return mvfxPositiveUpTo(c->radius, MVFX_MAX_EXTENT);
    case mvfx_colliderBox:
    case mvfx_colliderDistance:
        if (!IsRotation(c->rotation))
        {
            return mvfx_errorInvalid;
        }
        return mvfxFirst(CheckHalf(c->halfExtents[0]),
                         mvfxFirst(CheckHalf(c->halfExtents[1]), CheckHalf(c->halfExtents[2])));
    case mvfx_colliderHeightfield:
        if (!IsRotation(c->rotation))
        {
            return mvfx_errorInvalid;
        }
        return mvfxFirst(CheckHalf(c->halfExtents[0]), CheckHalf(c->halfExtents[2]));
    default:
        return mvfx_errorInvalid;
    }
}

// Whether the scene queries and the depth image an emitter's collision
// asks for are in range.
static mvfxResult CheckAsks(const mvfxCollision* c)
{
    if ((c->enableSceneQueries &&
         (c->queryCapacity < 1 || !(c->lookahead > 0.0f && c->lookahead <= 1.0f))) ||
        (c->enableDepthImage && c->depthSamples < 1))
    {
        return mvfx_errorInvalid;
    }
    mvfxResult scene = c->enableSceneQueries
                           ? mvfxWithinBound(c->queryCapacity <= MVFX_MAX_SCENE_QUERIES)
                           : mvfx_success;
    mvfxResult depth = c->enableDepthImage
                           ? mvfxFirst(mvfxPositiveUpTo(c->depthThickness, MVFX_MAX_EXTENT),
                                       mvfxWithinBound(c->depthSamples <= MVFX_MAX_DEPTH_SAMPLES))
                           : mvfx_success;
    return mvfxFirst(scene, depth);
}

mvfxResult mvfxCheckCollision(const mvfxCollision* c)
{
    if (!c->enable)
    {
        return mvfx_success;
    }
    if (!IsShare(c->restitution) || !IsShare(c->friction) || !IsShare(c->lifeLoss))
    {
        return mvfx_errorInvalid;
    }
    return mvfxFirst(mvfxFirst(mvfxUpToBound(c->restSpeed, 0.0f, MVFX_MAX_EXTENT),
                               mvfxUpToBound(c->radius, 0.0f, MVFX_MAX_EXTENT)),
                     CheckAsks(c));
}

mvfxCollider mvfxCompileCollider(const mvfxCollider* collider)
{
    mvfxCollider out = *collider;
    if (out.kind == mvfx_colliderPlane)
    {
        (void)mvfxNormalize(out.normal);
    }
    if (out.kind >= mvfx_colliderBox)
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

mvfxResolvedCollider mvfxResolveCollider(const mvfxCollider* collider, const struct mvfxGrid* grid,
                                         const mvfxSpaceMap* map, float radius)
{
    float sigma = map->sigma;
    if (grid != nullptr)
    {
        // A grid keeps its box; the radius is kept off in its field.
        mvfxResolvedCollider out = {
            .kind = collider->kind,
            .inside = collider->inside,
            .grid = grid,
            .scale = sigma,
            .grow = radius,
            .gridSerial = grid->serial,
        };
        for (int32_t k = 0; k < 3; ++k)
        {
            out.halfExtents[k] = collider->halfExtents[k] * sigma;
        }
        mvfxMapPoint(map, collider->center, out.center);
        mvfxMultiplyRotations(map->rotation, collider->rotation, out.rotation);
        return out;
    }
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

// The most samples a grid collider takes along a segment, and the
// halvings that find a crossing between two.
#define FIELD_SAMPLES  256
#define FIELD_HALVINGS 10

// A grid collider's field at a place in its box's frame: at or above 0 on
// the allowed side; above 0 (allowed) outside its box.
static float FieldAt(const mvfxResolvedCollider* c, const float local[3])
{
    float u[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        u[k] = local[k] / (2.0f * c->halfExtents[k]) + 0.5f;
    }
    bool height = c->kind == mvfx_colliderHeightfield;
    if (height)
    {
        u[1] = 0.5f;
    }
    float value[4];
    if (!mvfxSampleGrid(c->grid, u, mvfx_borderNone, value))
    {
        return INFINITY;
    }
    float v = height ? local[1] - value[0] * c->scale : value[0] * c->scale;
    return (c->inside ? -v : v) - c->grow;
}

// The span between a grid's points along the axes it varies on, the least
// of them; 0 for a grid of one point.
static float CellOf(const mvfxResolvedCollider* c)
{
    float cell = INFINITY;
    for (int32_t k = 0; k < 3; ++k)
    {
        int32_t n = c->grid->size[k];
        cell = n > 1 ? fminf(cell, 2.0f * c->halfExtents[k] / (float)(n - 1)) : cell;
    }
    return cell < INFINITY ? cell : 0.0f;
}

// The field's gradient at a local place, unit length, toward the allowed
// side; the box's up where it is flat (only a distance grid can be: a
// heightfield's always has its Y).
static void GradientAt(const mvfxResolvedCollider* c, const float local[3], float out[3])
{
    float cell = CellOf(c);
    float h = cell > 0.0f ? 0.5f * cell : 0.5f * c->halfExtents[0];
    for (int32_t k = 0; k < 3; ++k)
    {
        float a[3] = {local[0], local[1], local[2]};
        float b[3] = {local[0], local[1], local[2]};
        a[k] += h;
        b[k] -= h;
        float fa = FieldAt(c, a);
        float fb = FieldAt(c, b);
        // At the box's edge one side is outside: a one-sided difference.
        float f0 = FieldAt(c, local);
        out[k] = fa < INFINITY && fb < INFINITY ? (fa - fb) / (2.0f * h)
                 : fa < INFINITY                ? (fa - f0) / h
                 : fb < INFINITY                ? (f0 - fb) / h
                                                : 0.0f;
    }
    if (!mvfxNormalize(out))
    {
        out[0] = 0.0f;
        out[1] = 1.0f;
        out[2] = 0.0f;
    }
}

// Puts a local place on the wrong side back on the surface: straight up
// or down onto a heightfield, along the gradient out of a distance grid.
static void PushOutOfField(const mvfxResolvedCollider* c, float local[3])
{
    for (int32_t pass = 0; pass < 4; ++pass)
    {
        float f = FieldAt(c, local);
        if (!(f < 0.0f))
        {
            return;
        }
        if (c->kind == mvfx_colliderHeightfield)
        {
            local[1] += c->inside ? f : -f;
            continue;
        }
        float g[3];
        GradientAt(c, local, g);
        for (int32_t k = 0; k < 3; ++k)
        {
            local[k] -= f * g[k];
        }
    }
}

float mvfxFieldDistance(const mvfxResolvedCollider* c, const float point[3], float normal[3])
{
    const float* q = c->rotation;
    const float inverse[4] = {-q[0], -q[1], -q[2], q[3]};
    const float d[3] = {point[0] - c->center[0], point[1] - c->center[1], point[2] - c->center[2]};
    float local[3];
    mvfxRotate(inverse, d, local);
    float f = FieldAt(c, local);
    if (!(f < INFINITY))
    {
        return INFINITY;
    }
    float g[3];
    GradientAt(c, local, g);
    mvfxRotate(q, g, normal);
    // A heightfield's gap is along its Y: across the surface it is that
    // times the slope's cosine.
    return c->kind == mvfx_colliderHeightfield ? f * fabsf(g[1]) : f;
}

// The part [low, high] of the local segment a to b within the grid's box
// (a heightfield's X and Z); false when it misses it.
static bool ClipToBox(const mvfxResolvedCollider* c, const float a[3], const float b[3], float* low,
                      float* high)
{
    *low = 0.0f;
    *high = 1.0f;
    for (int32_t k = 0; k < 3; ++k)
    {
        if (k == 1 && c->kind == mvfx_colliderHeightfield)
        {
            continue;
        }
        float h = c->halfExtents[k];
        float d = b[k] - a[k];
        if (d == 0.0f)
        {
            if (fabsf(a[k]) > h)
            {
                return false;
            }
            continue;
        }
        float t1 = (-h - a[k]) / d;
        float t2 = (h - a[k]) / d;
        *low = fmaxf(*low, fminf(t1, t2));
        *high = fminf(*high, fmaxf(t1, t2));
    }
    return *low <= *high;
}

// The local segment's first crossing into a grid's wrong side, sampled
// every half cell: its share of the segment, on the allowed side.
static bool CrossField(const mvfxResolvedCollider* c, const float a[3], const float b[3], float* t)
{
    float low = 0.0f;
    float high = 1.0f;
    if (!ClipToBox(c, a, b, &low, &high))
    {
        return false;
    }
    float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    float length = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) * (high - low);
    float cell = CellOf(c);
    float wanted = cell > 0.0f ? ceilf(length / (0.5f * cell)) : 1.0f;
    int32_t n = (int32_t)fminf(fmaxf(wanted, 1.0f), (float)FIELD_SAMPLES);
    float before = low;
    for (int32_t i = 0; i <= n; ++i)
    {
        float at = low + (high - low) * (float)i / (float)n;
        float p[3];
        PointAt(a, b, at, p);
        if (!(FieldAt(c, p) < 0.0f))
        {
            before = at;
            continue;
        }
        // Halve [before, at] toward the crossing, keeping the allowed end.
        for (int32_t halving = 0; halving < FIELD_HALVINGS && i > 0; ++halving)
        {
            float middle = 0.5f * (before + at);
            PointAt(a, b, middle, p);
            if (FieldAt(c, p) < 0.0f)
            {
                at = middle;
            }
            else
            {
                before = middle;
            }
        }
        *t = before;
        return true;
    }
    return false;
}

static bool HitField(const mvfxResolvedCollider* c, const float s[3], const float e[3], Hit* hit)
{
    const float* q = c->rotation;
    const float inverse[4] = {-q[0], -q[1], -q[2], q[3]};
    float ds[3] = {s[0] - c->center[0], s[1] - c->center[1], s[2] - c->center[2]};
    float de[3] = {e[0] - c->center[0], e[1] - c->center[1], e[2] - c->center[2]};
    float a[3];
    float b[3];
    mvfxRotate(inverse, ds, a);
    mvfxRotate(inverse, de, b);
    float local[3];
    if (FieldAt(c, a) < 0.0f)
    {
        hit->t = 0.0f;
        for (int32_t k = 0; k < 3; ++k)
        {
            local[k] = a[k];
        }
        PushOutOfField(c, local);
    }
    else if (CrossField(c, a, b, &hit->t))
    {
        PointAt(a, b, hit->t, local);
    }
    else
    {
        return false;
    }
    float normal[3];
    GradientAt(c, local, normal);
    mvfxRotate(q, local, hit->point);
    mvfxRotate(q, normal, hit->normal);
    for (int32_t k = 0; k < 3; ++k)
    {
        hit->point[k] += c->center[k];
    }
    return true;
}

// The segment's earliest contact with any collider, or the particle's own.
static bool FirstHit(const mvfxResolvedCollider* colliders, int32_t count,
                     const mvfxOwnColliders* own, const float s[3], const float e[3], Hit* best)
{
    bool found = own->plane != nullptr && HitPlane(own->plane, s, e, best);
    Hit seen;
    if (own->depth != nullptr && mvfxDepthHit(own->depth, s, e, &seen.t, seen.point, seen.normal) &&
        (!found || seen.t < best->t))
    {
        *best = seen;
        found = true;
    }
    for (int32_t i = 0; i < count; ++i)
    {
        Hit hit;
        const mvfxResolvedCollider* c = &colliders[i];
        bool met = c->kind == mvfx_colliderPlane    ? HitPlane(c, s, e, &hit)
                   : c->kind == mvfx_colliderSphere ? HitSphere(c, s, e, &hit)
                   : c->kind == mvfx_colliderBox    ? HitBox(c, s, e, &hit)
                                                    : HitField(c, s, e, &hit);
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

// Whether a segment in the box from lo to hi cannot meet a collider: in
// binary64, the box's nearest point to a plane in front of it, or to an
// outside sphere farther than its radius, each by a margin. A plane's test
// is a dot product, its float rounding below 2^-20 of the coordinates'
// sum; a sphere's discriminant cancels, so its margin is 2^-8 of it (more
// than the square root of the rounding's share). NaN keeps a collider.
static bool IsOutOfReach(const mvfxResolvedCollider* c, const float lo[3], const float hi[3])
{
    double size = 0.0;
    for (int32_t k = 0; k < 3; ++k)
    {
        size += fmax(fabs((double)lo[k]), fabs((double)hi[k])) + fabs((double)c->center[k]);
    }
    if (c->kind == mvfx_colliderPlane)
    {
        double nearest = 0.0;
        for (int32_t k = 0; k < 3; ++k)
        {
            double n = (double)c->normal[k];
            nearest += n * ((n > 0.0 ? (double)lo[k] : (double)hi[k]) - (double)c->center[k]);
        }
        return nearest > 0x1p-20 * size;
    }
    if (c->kind == mvfx_colliderSphere && !c->inside)
    {
        double squared = 0.0;
        for (int32_t k = 0; k < 3; ++k)
        {
            double x = (double)c->center[k];
            double d = fmax(fmax((double)lo[k] - x, x - (double)hi[k]), 0.0);
            squared += d * d;
        }
        double clear = (double)c->radius + 0x1p-8 * (size + (double)c->radius);
        return squared > clear * clear;
    }
    return false;
}

int32_t mvfxReachable(const mvfxResolvedCollider* colliders, int32_t count, const float lo[3],
                      const float hi[3], mvfxResolvedCollider* out)
{
    int32_t kept = 0;
    for (int32_t i = 0; i < count; ++i)
    {
        if (!IsOutOfReach(&colliders[i], lo, hi))
        {
            out[kept] = colliders[i];
            kept += 1;
        }
    }
    return kept;
}

// Marks the segments that may meet a plane: HitPlane's own test, either
// end behind it, in its operations.
static void MayMeetPlane(const mvfxResolvedCollider* c, const float* const s[3],
                         const float* const e[3], int32_t n, uint8_t* marks)
{
    for (int32_t j = 0; j < n; ++j)
    {
        float a[3] = {s[0][j] - c->center[0], s[1][j] - c->center[1], s[2][j] - c->center[2]};
        float b[3] = {e[0][j] - c->center[0], e[1][j] - c->center[1], e[2][j] - c->center[2]};
        float d0 = Dot(a, c->normal);
        float d1 = Dot(b, c->normal);
        marks[j] |= d0 < 0.0f || d1 < 0.0f ? 1 : 0;
    }
}

// Marks the segments that may meet an outside sphere: HitSphere's start
// inside, or its discriminant not below 0, in its operations.
static void MayMeetSphere(const mvfxResolvedCollider* c, const float* const s[3],
                          const float* const e[3], int32_t n, uint8_t* marks)
{
    float r2 = c->radius * c->radius;
    for (int32_t j = 0; j < n; ++j)
    {
        float f[3] = {s[0][j] - c->center[0], s[1][j] - c->center[1], s[2][j] - c->center[2]};
        float d[3] = {e[0][j] - s[0][j], e[1][j] - s[1][j], e[2][j] - s[2][j]};
        float ff = Dot(f, f);
        float a = Dot(d, d);
        float b = Dot(f, d);
        float disc = b * b - a * (ff - r2);
        marks[j] |= ff < r2 || (a > 0.0f && disc >= 0.0f) ? 1 : 0;
    }
}

void mvfxMayMeet(const mvfxResolvedCollider* colliders, int32_t count, const float* const starts[3],
                 const float* const ends[3], int32_t n, uint8_t* marks)
{
    for (int32_t j = 0; j < n; ++j)
    {
        marks[j] = 0;
    }
    for (int32_t i = 0; i < count; ++i)
    {
        const mvfxResolvedCollider* c = &colliders[i];
        if (c->kind == mvfx_colliderPlane)
        {
            MayMeetPlane(c, starts, ends, n, marks);
        }
        else if (c->kind == mvfx_colliderSphere && !c->inside)
        {
            MayMeetSphere(c, starts, ends, n, marks);
        }
        else
        {
            for (int32_t j = 0; j < n; ++j)
            {
                marks[j] = 1;
            }
        }
    }
}

int32_t mvfxCollide(const mvfxResolvedCollider* colliders, int32_t count,
                    const mvfxOwnColliders* own, const mvfxCollision* response, float restSpeed,
                    const float start[3], float place[3], float velocity[3], mvfxContact* first)
{
    float s[3] = {start[0], start[1], start[2]};
    int32_t contacts = 0;
    Hit hit;
    while (contacts < MVFX_MAX_CONTACTS && FirstHit(colliders, count, own, s, place, &hit))
    {
        contacts += 1;
        if (contacts == 1)
        {
            *first = (mvfxContact){.t = hit.t,
                                   .point = {hit.point[0], hit.point[1], hit.point[2]},
                                   .normal = {hit.normal[0], hit.normal[1], hit.normal[2]}};
        }
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
    if (contacts == MVFX_MAX_CONTACTS && FirstHit(colliders, count, own, s, place, &hit))
    {
        // Out of contacts with a path still into a surface: stop at it.
        for (int32_t k = 0; k < 3; ++k)
        {
            place[k] = hit.point[k];
        }
    }
    return contacts;
}

// Whether n floats are the same, value by value.
static bool SameFloats(const float* a, const float* b, int32_t n)
{
    bool same = true;
    for (int32_t k = 0; k < n; ++k)
    {
        same = same && a[k] == b[k];
    }
    return same;
}

bool mvfxSameCollider(const mvfxResolvedCollider* a, const mvfxResolvedCollider* b)
{
    return a->gridSerial == b->gridSerial && mvfxSameColliderShape(a, b);
}

bool mvfxSameColliderShape(const mvfxResolvedCollider* a, const mvfxResolvedCollider* b)
{
    return a->kind == b->kind && a->inside == b->inside && a->grid == b->grid &&
           SameFloats(a->center, b->center, 3) && SameFloats(a->normal, b->normal, 3) &&
           SameFloats(&a->radius, &b->radius, 1) && SameFloats(a->halfExtents, b->halfExtents, 3) &&
           SameFloats(a->rotation, b->rotation, 4) && SameFloats(&a->scale, &b->scale, 1) &&
           SameFloats(&a->grow, &b->grow, 1);
}

// A point's signed distance to a box at the origin of half sizes half,
// below 0 inside.
static float BoxDistance(const float local[3], const float half[3])
{
    float outside = 0.0f;
    float inside = -INFINITY;
    for (int32_t k = 0; k < 3; ++k)
    {
        float d = fabsf(local[k]) - half[k];
        outside += d > 0.0f ? d * d : 0.0f;
        inside = fmaxf(inside, d);
    }
    return inside > 0.0f ? sqrtf(outside) : inside;
}

bool mvfxOnlyPointsChanged(const mvfxResolvedCollider* before, const mvfxResolvedCollider* after)
{
    return before != nullptr && after != nullptr && after->grid != nullptr &&
           mvfxSameColliderShape(before, after) && after->grid->since <= before->gridSerial &&
           after->grid->first[0] < after->grid->end[0];
}

float mvfxChangedGap(const mvfxResolvedCollider* c, const float point[3])
{
    const float d[3] = {point[0] - c->center[0], point[1] - c->center[1], point[2] - c->center[2]};
    const float back[4] = {-c->rotation[0], -c->rotation[1], -c->rotation[2], c->rotation[3]};
    float local[3];
    mvfxRotate(back, d, local);
    float outside = 0.0f;
    float inside = -INFINITY;
    for (int32_t k = 0; k < 3; ++k)
    {
        // A heightfield's heights reach any height.
        if (k == 1 && c->kind == mvfx_colliderHeightfield)
        {
            continue;
        }
        float h = c->halfExtents[k];
        int32_t n = c->grid->size[k];
        // Point i lies at (i / (n - 1) - 1/2) 2h; the cells on either side
        // of the changed points change with them.
        float low =
            n == 1 ? -h
                   : fmaxf(((float)(c->grid->first[k] - 1) / (float)(n - 1) - 0.5f) * 2.0f * h, -h);
        float high =
            n == 1 ? h : fminf(((float)c->grid->end[k] / (float)(n - 1) - 0.5f) * 2.0f * h, h);
        float e = fmaxf(low - local[k], local[k] - high);
        outside += e > 0.0f ? e * e : 0.0f;
        inside = fmaxf(inside, e);
    }
    return inside > 0.0f ? sqrtf(outside) : inside;
}

float mvfxColliderGap(const mvfxResolvedCollider* c, const float point[3], bool bounds)
{
    const float d[3] = {point[0] - c->center[0], point[1] - c->center[1], point[2] - c->center[2]};
    float gap = 0.0f;
    if (c->kind == mvfx_colliderPlane)
    {
        gap = d[0] * c->normal[0] + d[1] * c->normal[1] + d[2] * c->normal[2];
    }
    else if (c->kind == mvfx_colliderSphere)
    {
        gap = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) - c->radius;
        gap = c->inside ? -gap : gap;
    }
    else if (c->kind == mvfx_colliderBox || bounds)
    {
        const float back[4] = {-c->rotation[0], -c->rotation[1], -c->rotation[2], c->rotation[3]};
        float local[3];
        mvfxRotate(back, d, local);
        gap = BoxDistance(local, c->halfExtents);
        gap = c->inside && c->kind == mvfx_colliderBox ? -gap : gap;
    }
    else
    {
        float normal[3];
        gap = mvfxFieldDistance(c, point, normal);
    }
    return gap;
}
