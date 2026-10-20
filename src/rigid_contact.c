// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen

#include "rigid_contact.h"

#include "convex.h"
#include "depth.h"
#include "direction.h"
#include "rigid_box.h"

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
        float point[3];
        mvfxShapePoint(s, i, point);
        SurfacePoint(b, s, point, down, body, world);
        float gap[3] = {world[0] - c->center[0], world[1] - c->center[1], world[2] - c->center[2]};
        mvfxBodyContact contact = ContactAt(body, world, c->normal, Dot(gap, c->normal));
        mvfxAddContact(list, &contact);
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
        float point[3];
        float turned[3];
        mvfxShapePoint(s, i, point);
        mvfxRotate(b->q, point, turned);
        float center[3] = {b->x[0] + turned[0], b->x[1] + turned[1], b->x[2] + turned[2]};
        float out[3];
        float distance = WayFrom(c->center, center, out);
        float body[3];
        float world[3];
        SurfacePoint(b, s, point, out, body, world);
        const float in[3] = {-out[0], -out[1], -out[2]};
        mvfxBodyContact contact = ContactAt(body, world, in, c->radius - distance - s->radius);
        mvfxAddContact(list, &contact);
    }
}

// To a box collider's frame and back.
static void ToBox(const mvfxResolvedCollider* c, const float p[3], float out[3])
{
    const float d[3] = {p[0] - c->center[0], p[1] - c->center[1], p[2] - c->center[2]};
    ToBody(c->rotation, d, out);
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
    mvfxAddContact(list, &contact);
}

// The share of a body's reach by which an inner point (a capsule's core's
// deepest point, a face's center, an edge's inner points) must lie deeper
// than the ends or corners it lies between to be met: on a flat surface
// they are as deep, and rounding would decide.
#define DEEPEST_MARGIN 1e-4f

// A sphere or a capsule kept out of a box: its core's ends and its
// deepest point between (the box's signed distance is convex along the
// core, so a ternary search finds it) where clearly deeper than both ends:
// along a face the distance is linear and its least is at an end, so
// there the deepest point is any point of a flat run, which rounding
// picked, and a capsule lying on a box rocked on it.
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
    float point[3];
    float way[3];
    float endDepth =
        fminf(CoreDistance(b, s, c, 0.0f, point, way), CoreDistance(b, s, c, 1.0f, point, way));
    bool clear = CoreDistance(b, s, c, deepest, point, way) <
                 endDepth - DEEPEST_MARGIN * (s->half[1] + s->radius);
    CoreContact(b, s, c, 0.0f, list);
    CoreContact(b, s, c, 1.0f, list);
    if (clear && deepest > 1e-3f && deepest < 1.0f - 1e-3f)
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
    mvfxAddContact(list, &contact);
}

// A capsule's spans between its core's samples, at most its radius long
// (at most eight); none for a sphere.
static int32_t CoreSpans(const mvfxBodyShape* s)
{
    return s->kind == mvfx_rigidCapsule && s->radius > 0.0f
               ? (int32_t)ceilf(fminf(2.0f * s->half[1] / s->radius, 8.0f))
               : 0;
}

// How many points (in its frame) a shape meets a grid or a depth image by,
// as spheres of its radius, and how many of them are corners, met plainly:
// a sphere's center; a capsule's core, its points at most its radius
// apart; a box's eight corners, then its faces' six centers; a hull's
// vertices, then its faces' centers.
static int32_t SampleCount(const mvfxBodyShape* s, int32_t* corners)
{
    int32_t count = CoreSpans(s) + 1;
    *corners = count;
    if (s->kind == mvfx_rigidBox)
    {
        *corners = 8;
        count = 14;
    }
    else if (s->kind == mvfx_rigidHull)
    {
        *corners = s->hull->vertexCount;
        count = s->hull->vertexCount + s->hull->faceCount;
    }
    return count;
}

// A box's sample i: its corners (bit k of i set for +X, +Y, +Z), then its
// faces' centers (8 + 2k for +k, 9 + 2k for -k).
static void BoxSample(const mvfxBodyShape* s, int32_t i, float out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        float corner = (i >> k & 1) != 0 ? s->half[k] : -s->half[k];
        float side = (i - 8) % 2 == 0 ? s->half[k] : -s->half[k];
        out[k] = i < 8 ? corner : ((i - 8) / 2 == k ? side : 0.0f);
    }
}

// A hull's sample i: its vertices, then its faces' centers.
static void HullSample(const mvfxBodyShape* s, int32_t i, float out[3])
{
    const mvfxHull* hull = s->hull;
    if (i < hull->vertexCount)
    {
        mvfxShapePoint(s, i, out);
        return;
    }
    int32_t f = i - hull->vertexCount;
    int32_t first = hull->faceStart[f];
    int32_t corners = hull->faceStart[f + 1] - first;
    float center[3] = {0.0f, 0.0f, 0.0f};
    for (int32_t j = 0; j < corners; ++j)
    {
        float corner[3];
        mvfxShapePoint(s, hull->loop[first + j], corner);
        for (int32_t k = 0; k < 3; ++k)
        {
            center[k] += corner[k] / (float)corners;
        }
    }
    memcpy(out, center, sizeof center);
}

static void SampleOf(const mvfxBodyShape* s, int32_t i, float out[3])
{
    if (s->kind == mvfx_rigidBox)
    {
        BoxSample(s, i, out);
    }
    else if (s->kind == mvfx_rigidHull)
    {
        HullSample(s, i, out);
    }
    else
    {
        int32_t spans = CoreSpans(s);
        CoreAt(s, spans > 0 ? (float)i / (float)spans : 0.5f, out);
    }
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

// Whether face center i, at depth, is met: only where the surface bulges
// under it, deeper than each corner of its face found by more than clear.
// On a flat surface the corners carry a face; a fifth contact amid them
// shifts the load the one pass gives each and unsettled a box held on a
// slope.
static bool CenterMet(const mvfxBodyShape* s, const float* depths, const bool* found, int32_t i,
                      float depth, float clear)
{
    bool met = true;
    if (s->kind == mvfx_rigidBox)
    {
        int32_t k = (i - 8) / 2;
        int32_t side = (i - 8) % 2 == 0 ? 1 : 0;
        for (int32_t c = 0; met && c < 8; ++c)
        {
            bool onFace = (c >> k & 1) == side;
            met = !onFace || !found[c] || depth < depths[c] - clear;
        }
    }
    else
    {
        const mvfxHull* hull = s->hull;
        int32_t f = i - hull->vertexCount;
        for (int32_t j = hull->faceStart[f]; met && j < hull->faceStart[f + 1]; ++j)
        {
            int32_t c = hull->loop[j];
            met = !found[c] || depth < depths[c] - clear;
        }
    }
    return met;
}

// What samples meet: a grid's field, or the depth image seen from the
// pose at the substep's start.
typedef struct Probe
{
    const mvfxResolvedCollider* grid;
    const mvfxResolvedDepth* depth;
    const mvfxBodyPose* from;
} Probe;

// Whether a sample meets the probe's surface, and its contact.
static bool ProbeSample(const Probe* p, const mvfxBodyPose* b, const mvfxBodyShape* s,
                        const float sample[3], mvfxBodyContact* out)
{
    float world[3];
    SampleAt(b, sample, world);
    if (p->grid != nullptr)
    {
        float n[3];
        float distance = mvfxFieldDistance(p->grid, world, n);
        bool found = distance < INFINITY;
        if (found)
        {
            *out = SampleContact(b, s, sample, world, n, distance);
        }
        return found;
    }
    float from[3];
    float t = 0.0f;
    float point[3] = {0.0f, 0.0f, 0.0f};
    float n[3] = {0.0f, 1.0f, 0.0f};
    SampleAt(p->from, sample, from);
    bool found = mvfxDepthHit(p->depth, from, world, &t, point, n);
    if (found)
    {
        const float gap[3] = {world[0] - point[0], world[1] - point[1], world[2] - point[2]};
        *out = SampleContact(b, s, sample, world, n, Dot(gap, n) + p->depth->radius);
    }
    return found;
}

// The most edges a shape has: a hull's, each in two of its faces' loops.
#define MOST_EDGES (MVFX_MAX_HULL_LOOP / 2)

// A box's or a hull's edges as pairs of its corners (its samples below
// its corner count): a box's twelve, between corners differing in one
// bit; a hull's from its faces' loops, each once, its lower corner first.
static int32_t EdgesOf(const mvfxBodyShape* s, uint8_t edges[MOST_EDGES][2])
{
    int32_t count = 0;
    if (s->kind == mvfx_rigidBox)
    {
        for (int32_t i = 0; i < 8; ++i)
        {
            for (int32_t k = 0; k < 3; ++k)
            {
                int32_t j = i | 1 << k;
                if (j != i)
                {
                    edges[count][0] = (uint8_t)i;
                    edges[count++][1] = (uint8_t)j;
                }
            }
        }
        return count;
    }
    const mvfxHull* hull = s->hull;
    for (int32_t f = 0; f < hull->faceCount; ++f)
    {
        int32_t first = hull->faceStart[f];
        int32_t corners = hull->faceStart[f + 1] - first;
        for (int32_t j = 0; j < corners && count < MOST_EDGES; ++j)
        {
            uint8_t from = hull->loop[first + j];
            uint8_t to = hull->loop[first + (j + 1) % corners];
            if (from < to)
            {
                edges[count][0] = from;
                edges[count++][1] = to;
            }
        }
    }
    return count;
}

// Adds each edge's deepest inner point (a quarter, a half and three
// quarters along) where the surface bulges under it, deeper than each of
// its corners found by more than clear: an edge laid across a crest, its
// corners clear of
// the ground on either side, rests on the crest. On a flat surface the
// corners carry the edge, as they do the faces.
static void EdgeContacts(const Probe* p, const mvfxBodyPose* b, const mvfxBodyShape* s,
                         const float* depths, const bool* found, float clear,
                         mvfxBodyContacts* list)
{
    uint8_t edges[MOST_EDGES][2];
    int32_t count = EdgesOf(s, edges);
    for (int32_t e = 0; e < count; ++e)
    {
        float from[3];
        float to[3];
        SampleOf(s, edges[e][0], from);
        SampleOf(s, edges[e][1], to);
        mvfxBodyContact deepest = {.depth = INFINITY};
        bool met = false;
        for (int32_t q = 1; q <= 3; ++q)
        {
            float t = 0.25f * (float)q;
            const float point[3] = {from[0] + t * (to[0] - from[0]),
                                    from[1] + t * (to[1] - from[1]),
                                    from[2] + t * (to[2] - from[2])};
            mvfxBodyContact contact;
            bool hit = ProbeSample(p, b, s, point, &contact) && contact.depth < deepest.depth;
            deepest = hit ? contact : deepest;
            met = met || hit;
        }
        for (int32_t k = 0; k < 2 && met; ++k)
        {
            int32_t c = edges[e][k];
            met = !found[c] || deepest.depth < depths[c] - clear;
        }
        if (met)
        {
            mvfxAddContact(list, &deepest);
        }
    }
}

// Adds the samples meeting the probe's surface in order, corners first:
// only the corners' depths are kept, for their faces' centers and their
// edges.
static void SampleContacts(const Probe* p, const mvfxBodyPose* b, const mvfxBodyShape* s,
                           mvfxBodyContacts* list)
{
    float depths[MVFX_MAX_HULL_VERTICES];
    bool found[MVFX_MAX_HULL_VERTICES];
    float clear = DEEPEST_MARGIN * mvfxBodyReach(s);
    int32_t corners = 0;
    int32_t count = SampleCount(s, &corners);
    for (int32_t i = 0; i < count; ++i)
    {
        float sample[3];
        mvfxBodyContact contact;
        SampleOf(s, i, sample);
        bool hit = ProbeSample(p, b, s, sample, &contact);
        if (i < corners)
        {
            found[i] = hit;
            depths[i] = hit ? contact.depth : INFINITY;
        }
        if (hit && (i < corners || CenterMet(s, depths, found, i, contact.depth, clear)))
        {
            mvfxAddContact(list, &contact);
        }
    }
    if (s->kind == mvfx_rigidBox || s->kind == mvfx_rigidHull)
    {
        EdgeContacts(p, b, s, depths, found, clear, list);
    }
}

// A heightfield or a distance grid, by its field at the samples.
static void GridContacts(const mvfxBodyPose* b, const mvfxBodyShape* s,
                         const mvfxResolvedCollider* c, mvfxBodyContacts* list)
{
    const Probe probe = {.grid = c};
    SampleContacts(&probe, b, s, list);
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
    const Probe probe = {.depth = &depth, .from = surfaces->from};
    SampleContacts(&probe, b, s, list);
}

// Adds a's points meeting b by the manifold of polytopes a (the body's)
// and b.
static void PolytopeContacts(const mvfxBodyPose* body, const mvfxPolytope* a, const mvfxPolytope* b,
                             mvfxBodyContacts* list)
{
    mvfxConvexMeet meet;
    if (!mvfxConvexMeets(a, b, list->margin, &meet))
    {
        return;
    }
    list->searchesCapped += meet.capped ? 1 : 0;
    float n[3];
    mvfxConvexPoint points[MVFX_MAX_MANIFOLD_POINTS];
    int32_t count = mvfxConvexManifold(a, b, &meet, list->margin, n, points);
    for (int32_t i = 0; i < count; ++i)
    {
        const float offset[3] = {points[i].onA[0] - body->x[0], points[i].onA[1] - body->x[1],
                                 points[i].onA[2] - body->x[2]};
        float point[3];
        ToBody(body->q, offset, point);
        mvfxAddBodyContact(list, point, points[i].onA, n, points[i].depth);
    }
}

// A hull outside a sphere: its point nearest the center, or for a center
// inside it its face nearest the center.
static void HullSphereContacts(const mvfxBodyPose* b, const mvfxBodyShape* s,
                               const mvfxResolvedCollider* c, mvfxBodyContacts* list)
{
    mvfxPolytope hull;
    mvfxHullPolytope(s->hull, s->scale, b->x, b->q, &hull);
    float near[3];
    float distance = mvfxPolytopeNearest(&hull, c->center, near);
    float n[3] = {0.0f, 1.0f, 0.0f};
    float depth = distance - c->radius;
    if (distance > 0.0f)
    {
        for (int32_t k = 0; k < 3; ++k)
        {
            n[k] = (near[k] - c->center[k]) / distance;
        }
    }
    else
    {
        // Pushed out through the face the center is nearest, against its
        // normal.
        int32_t face = 0;
        float above = -INFINITY;
        for (int32_t f = 0; f < hull.faceCount; ++f)
        {
            float off = Dot(hull.planes[f], c->center) - hull.planes[f][3];
            face = off > above ? f : face;
            above = fmaxf(above, off);
        }
        for (int32_t k = 0; k < 3; ++k)
        {
            n[k] = -hull.planes[face][k];
            near[k] = c->center[k] - above * hull.planes[face][k];
        }
        depth = above - c->radius;
    }
    const float offset[3] = {near[0] - b->x[0], near[1] - b->x[1], near[2] - b->x[2]};
    float point[3];
    ToBody(b->q, offset, point);
    mvfxAddBodyContact(list, point, near, n, depth);
}

// A hull outside a box, as two polytopes.
static void HullBoxContacts(const mvfxBodyPose* b, const mvfxBodyShape* s,
                            const mvfxResolvedCollider* c, mvfxBodyContacts* list)
{
    mvfxPolytope hull;
    mvfxPolytope box;
    mvfxHullPolytope(s->hull, s->scale, b->x, b->q, &hull);
    mvfxBoxPolytope(c->center, c->rotation, c->halfExtents, &box);
    PolytopeContacts(b, &hull, &box, list);
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
    else if (c->kind == mvfx_colliderSphere && s->kind == mvfx_rigidHull)
    {
        HullSphereContacts(b, s, c, list);
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
        mvfxBoxBoxContacts(b, s, c, list);
    }
    else if (c->kind == mvfx_colliderBox && s->kind == mvfx_rigidHull)
    {
        HullBoxContacts(b, s, c, list);
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
    list->margin = MVFX_CONTACT_MARGIN * mvfxBodyReach(s);
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

// A sphere or a box body as a collider kept out of.
static mvfxResolvedCollider AsCollider(const mvfxBodyPose* b, const mvfxBodyShape* s)
{
    mvfxResolvedCollider c = {
        .kind = s->kind == mvfx_rigidBox ? mvfx_colliderBox : mvfx_colliderSphere,
        .radius = s->radius,
    };
    for (int32_t k = 0; k < 3; ++k)
    {
        c.center[k] = b->x[k];
        c.halfExtents[k] = s->half[k];
    }
    for (int32_t k = 0; k < 4; ++k)
    {
        c.rotation[k] = b->q[k];
    }
    return c;
}

// The world ends of a capsule's core (a sphere's center twice).
static void CoreEnds(const mvfxBodyPose* b, const mvfxBodyShape* s, float out[2][3])
{
    for (int32_t e = 0; e < 2; ++e)
    {
        float core[3];
        CoreAt(s, (float)e, core);
        SampleAt(b, core, out[e]);
    }
}

// The share along p0 to p1 nearest q, held to the segment.
static float NearestShare(const float p0[3], const float p1[3], const float q[3])
{
    const float d[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
    const float r[3] = {q[0] - p0[0], q[1] - p0[1], q[2] - p0[2]};
    float square = Dot(d, d);
    return square > 0.0f ? fminf(fmaxf(Dot(r, d) / square, 0.0f), 1.0f) : 0.0f;
}

// The shares of two segments' nearest points (Ericson, "Real-Time
// Collision Detection", 2005, 5.1.9).
static void NearestShares(const float a[2][3], const float b[2][3], float* s, float* t)
{
    const float d1[3] = {a[1][0] - a[0][0], a[1][1] - a[0][1], a[1][2] - a[0][2]};
    const float d2[3] = {b[1][0] - b[0][0], b[1][1] - b[0][1], b[1][2] - b[0][2]};
    const float r[3] = {a[0][0] - b[0][0], a[0][1] - b[0][1], a[0][2] - b[0][2]};
    float e1 = Dot(d1, d1);
    float e2 = Dot(d2, d2);
    float f = Dot(d2, r);
    float c = Dot(d1, r);
    float m = Dot(d1, d2);
    float denominator = e1 * e2 - m * m;
    *s = denominator > 0.0f ? fminf(fmaxf((m * f - c * e2) / denominator, 0.0f), 1.0f) : 0.0f;
    *t = e2 > 0.0f ? (m * *s + f) / e2 : 0.0f;
    if (*t < 0.0f || *t > 1.0f)
    {
        *t = fminf(fmaxf(*t, 0.0f), 1.0f);
        *s = e1 > 0.0f ? fminf(fmaxf((m * *t - c) / e1, 0.0f), 1.0f) : 0.0f;
    }
}

// The contact of capsule a's core at share s with capsule b's at t.
static void CoreToCore(const mvfxBodyPose* a, const mvfxBodyShape* sa, const mvfxBodyShape* sb,
                       const float ea[2][3], const float eb[2][3], float s, float t,
                       mvfxBodyContacts* list)
{
    float pa[3];
    float pb[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        pa[k] = ea[0][k] + s * (ea[1][k] - ea[0][k]);
        pb[k] = eb[0][k] + t * (eb[1][k] - eb[0][k]);
    }
    float n[3];
    float distance = WayFrom(pb, pa, n);
    const float world[3] = {pa[0] - sa->radius * n[0], pa[1] - sa->radius * n[1],
                            pa[2] - sa->radius * n[2]};
    const float offset[3] = {world[0] - a->x[0], world[1] - a->x[1], world[2] - a->x[2]};
    float point[3];
    ToBody(a->q, offset, point);
    mvfxBodyContact contact = ContactAt(point, world, n, distance - sa->radius - sb->radius);
    mvfxAddContact(list, &contact);
}

// Two capsules: their cores' nearest points, and each core's ends with
// the other's nearest, so capsules lying side by side rest on a line.
static void CapsulePair(const mvfxBodyPose* a, const mvfxBodyShape* sa, const mvfxBodyPose* b,
                        const mvfxBodyShape* sb, mvfxBodyContacts* list)
{
    float ea[2][3];
    float eb[2][3];
    CoreEnds(a, sa, ea);
    CoreEnds(b, sb, eb);
    float s = 0.0f;
    float t = 0.0f;
    NearestShares(ea, eb, &s, &t);
    float shares[5][2] = {{s, t}};
    shares[1][0] = 0.0f;
    shares[1][1] = NearestShare(eb[0], eb[1], ea[0]);
    shares[2][0] = 1.0f;
    shares[2][1] = NearestShare(eb[0], eb[1], ea[1]);
    shares[3][0] = NearestShare(ea[0], ea[1], eb[0]);
    shares[3][1] = 0.0f;
    shares[4][0] = NearestShare(ea[0], ea[1], eb[1]);
    shares[4][1] = 1.0f;
    for (int32_t i = 0; i < 5; ++i)
    {
        // Each pair of shares once.
        bool fresh = true;
        for (int32_t j = 0; fresh && j < i; ++j)
        {
            fresh = fabsf(shares[i][0] - shares[j][0]) > 1e-4f ||
                    fabsf(shares[i][1] - shares[j][1]) > 1e-4f;
        }
        if (fresh)
        {
            CoreToCore(a, sa, sb, ea, eb, shares[i][0], shares[i][1], list);
        }
    }
}

void mvfxGatherPairContacts(const mvfxBodyPose* a, const mvfxBodyShape* sa, const mvfxBodyPose* b,
                            const mvfxBodyShape* sb, mvfxBodyContacts* list)
{
    list->margin = MVFX_CONTACT_MARGIN * fminf(mvfxBodyReach(sa), mvfxBodyReach(sb));
    if (sb->kind == mvfx_rigidHull)
    {
        mvfxPolytope pa;
        mvfxPolytope pb;
        mvfxHullPolytope(sa->hull, sa->scale, a->x, a->q, &pa);
        mvfxHullPolytope(sb->hull, sb->scale, b->x, b->q, &pb);
        PolytopeContacts(a, &pa, &pb, list);
        return;
    }
    if (sb->kind == mvfx_rigidCapsule)
    {
        CapsulePair(a, sa, b, sb, list);
        return;
    }
    const mvfxResolvedCollider c = AsCollider(b, sb);
    ColliderContacts(&c, a, sa, list);
}
