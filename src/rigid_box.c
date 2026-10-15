// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "rigid_box.h"

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
// the body; false when one parts them by more than margin. Edges win only
// by a clear margin, so resting faces keep face contacts.
static bool LeastAxis(const BoxPair* p, float margin, float n[3], int32_t* axis)
{
    float best = -INFINITY;
    for (int32_t i = 0; i < 6; ++i)
    {
        float l[3] = {0.0f, 0.0f, 0.0f};
        l[i % 3] = 1.0f;
        const float* way = i < 3 ? l : p->axes[i - 3];
        float gap = Separation(p, way);
        if (gap > margin)
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
        if (gap > margin)
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

// The share of a clipping box's reach within which a point is on its
// plane.
#define CLIP_TOLERANCE 1e-4f

// Clips a polygon of count points (at most 8) to dot(x, l) <= limit, the
// clipping box reaching reach along l. A point on the plane (within the
// tolerance) is kept and no crossing is added beside it: faces of boxes
// aligned edge to edge clipped their shared corners in twice or three
// times, the doubled corners weighting their side of the patch, and the
// count changed with every slight turn, so a stack of boxes rocked.
static int32_t Clip(float polygon[8][3], int32_t count, const float l[3], float limit, float reach)
{
    float out[8][3];
    int32_t kept = 0;
    float on = CLIP_TOLERANCE * reach;
    for (int32_t i = 0; i < count; ++i)
    {
        const float* a = polygon[i];
        const float* b = polygon[(i + 1) % count];
        float da = Dot(a, l) - limit;
        float db = Dot(b, l) - limit;
        if (da <= on && kept < 8)
        {
            memcpy(out[kept++], a, 3 * sizeof(float));
        }
        if (((da < -on && db > on) || (da > on && db < -on)) && kept < 8)
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
    mvfxAddBodyContact(list, point, world, n0, Dot(gap, n));
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
        count = Clip(polygon, count, l, p->a[(k + m) % 3], p->a[(k + m) % 3]);
        l[(k + m) % 3] = -1.0f;
        count = Clip(polygon, count, l, p->a[(k + m) % 3], p->a[(k + m) % 3]);
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
        count = Clip(polygon, count, l, Dot(p->t, l) + reach, reach);
        count = Clip(polygon, count, back, Dot(p->t, back) + reach, reach);
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

void mvfxBoxBoxContacts(const mvfxBodyPose* b, const mvfxBodyShape* s,
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
    if (!LeastAxis(&p, list->margin, n, &axis))
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
