// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Convex polytopes meeting: boxes apart at their exact distance and
// nearest points, square and turned; boxes overlapping by their depth,
// concentric ones too, at scales from 1000 to 1e-7;
// a point's nearest point, outside and inside; a turned box on a box met
// at its overlap's eight corners, edges crossing at one point, and a box
// just apart within the margin at four; points' nearest distances to a
// ball's hull of 60 vertices by its features; thin slabs nearly parallel
// met in few steps, and searches held to one step answering with bounds;
// lopsided hulls overlapping at
// many poses by the depth the separating axes give.

#include "convex.h"
#include "test_harness.h"

#include <math.h>

static const float s_unit[3] = {0.5f, 0.5f, 0.5f};
static const float s_identity[4] = {0, 0, 0, 1};

static void TurnAbout(int32_t k, float angle, float out[4])
{
    out[0] = 0.0f;
    out[1] = 0.0f;
    out[2] = 0.0f;
    out[k] = sinf(0.5f * angle);
    out[3] = cosf(0.5f * angle);
}

static bool Near(const float a[3], float x, float y, float z, float tolerance)
{
    return fabsf(a[0] - x) <= tolerance && fabsf(a[1] - y) <= tolerance &&
           fabsf(a[2] - z) <= tolerance;
}

static void TestApart(void)
{
    // Unit boxes 0.3 apart along x; then the second turned an eighth about
    // z, its edge 0.2 from the first's face.
    mvfxPolytope a;
    mvfxPolytope b;
    static const float origin[3] = {0, 0, 0};
    mvfxBoxPolytope(origin, s_identity, s_unit, &a);
    const float right[3] = {1.3f, 0.1f, 0.2f};
    mvfxBoxPolytope(right, s_identity, s_unit, &b);
    mvfxConvexMeet meet;
    bool met = mvfxConvexMeets(&a, &b, 1.0f, &meet);
    CHECK(met && fabsf(meet.distance - 0.3f) <= 1e-5f && Near(meet.normal, -1, 0, 0, 1e-5f) &&
              fabsf(meet.onA[0] - 0.5f) <= 1e-5f && fabsf(meet.onB[0] - 0.8f) <= 1e-5f,
          "apart");
    CHECK(!mvfxConvexMeets(&a, &b, 0.29f, &meet), "past the margin");
    float turned[4];
    TurnAbout(2, 0.78539816f, turned);
    const float edge[3] = {0.5f + 0.70710678f + 0.2f, 0, 0};
    mvfxBoxPolytope(edge, turned, s_unit, &b);
    met = mvfxConvexMeets(&a, &b, 1.0f, &meet);
    CHECK(met && fabsf(meet.distance - 0.2f) <= 1e-5f && Near(meet.normal, -1, 0, 0, 1e-4f),
          "apart from an edge");
}

static void TestOverlap(void)
{
    // Unit boxes overlapping by 0.1 along x, 0.4 along y and z: parted
    // along x.
    mvfxPolytope a;
    mvfxPolytope b;
    static const float origin[3] = {0, 0, 0};
    mvfxBoxPolytope(origin, s_identity, s_unit, &a);
    const float over[3] = {0.9f, 0.6f, -0.6f};
    mvfxBoxPolytope(over, s_identity, s_unit, &b);
    mvfxConvexMeet meet;
    bool met = mvfxConvexMeets(&a, &b, 0.0f, &meet);
    CHECK(met && fabsf(meet.distance + 0.1f) <= 1e-5f && Near(meet.normal, -1, 0, 0, 1e-5f),
          "overlapping");
    float near[3];
    const float outside[3] = {2.0f, 0.2f, 0.1f};
    const float inside[3] = {0.1f, 0.2f, -0.3f};
    float d = mvfxPolytopeNearest(&a, outside, near);
    CHECK(fabsf(d - 1.5f) <= 1e-5f && Near(near, 0.5f, 0.2f, 0.1f, 1e-5f), "nearest outside");
    d = mvfxPolytopeNearest(&a, inside, near);
    CHECK(d == 0.0f && Near(near, 0.1f, 0.2f, -0.3f, 0.0f), "nearest inside");
}

static void TestConcentric(void)
{
    // Boxes about one center, of half size 1000 down to 1e-7: they
    // overlap by their whole width along an axis, the simplex grown from
    // the one point GJK starts and ends at.
    static const float origin[3] = {0, 0, 0};
    static const float halves[4] = {1e3f, 0.5f, 1e-4f, 1e-7f};
    bool parted = true;
    for (int32_t i = 0; i < 4; ++i)
    {
        const float half[3] = {halves[i], halves[i], halves[i]};
        mvfxPolytope a;
        mvfxPolytope b;
        mvfxBoxPolytope(origin, s_identity, half, &a);
        mvfxBoxPolytope(origin, s_identity, half, &b);
        mvfxConvexMeet meet;
        bool met = mvfxConvexMeets(&a, &b, 0.0f, &meet);
        float axis =
            fmaxf(fmaxf(fabsf(meet.normal[0]), fabsf(meet.normal[1])), fabsf(meet.normal[2]));
        parted = parted && met && fabsf(meet.distance + 2.0f * halves[i]) <= 1e-4f * halves[i] &&
                 fabsf(axis - 1.0f) <= 1e-5f;
    }
    CHECK(parted, "concentric boxes at every scale");
}

static void TestManifolds(void)
{
    // A 2 by 1 by 2 box turned an eighth about y, 0.01 into the top of a
    // box of 1.6 by 1 by 1.6: met at their overlap's eight corners.
    mvfxPolytope a;
    mvfxPolytope b;
    static const float origin[3] = {0, 0, 0};
    static const float slab[3] = {0.8f, 0.5f, 0.8f};
    static const float wide[3] = {1.0f, 0.5f, 1.0f};
    mvfxBoxPolytope(origin, s_identity, slab, &b);
    float turned[4];
    TurnAbout(1, 0.78539816f, turned);
    const float above[3] = {0, 0.99f, 0};
    mvfxBoxPolytope(above, turned, wide, &a);
    mvfxConvexMeet meet;
    float n[3];
    mvfxConvexPoint points[MVFX_MAX_MANIFOLD_POINTS];
    bool met = mvfxConvexMeets(&a, &b, 0.02f, &meet);
    int32_t count = met ? mvfxConvexManifold(&a, &b, &meet, 0.02f, n, points) : 0;
    bool corners = count == 8 && Near(n, 0, 1, 0, 1e-5f);
    for (int32_t i = 0; corners && i < count; ++i)
    {
        corners =
            fabsf(points[i].depth + 0.01f) <= 1e-4f && fabsf(points[i].onB[1] - 0.5f) <= 1e-4f;
    }
    CHECK(corners, "an octagon's eight corners");
    // Unit boxes on edge (an eighth about x and about z) crossing 0.01
    // into each other: one point.
    float beam[4];
    float rod[4];
    TurnAbout(0, 0.78539816f, beam);
    TurnAbout(2, 0.78539816f, rod);
    mvfxBoxPolytope(origin, beam, s_unit, &b);
    const float cross[3] = {0, 1.41421356f - 0.01f, 0};
    mvfxBoxPolytope(cross, rod, s_unit, &a);
    met = mvfxConvexMeets(&a, &b, 0.02f, &meet);
    count = met ? mvfxConvexManifold(&a, &b, &meet, 0.02f, n, points) : 0;
    CHECK(count == 1 && fabsf(points[0].depth + 0.01f) <= 1e-4f && Near(n, 0, 1, 0, 1e-4f),
          "edges crossing at a point");
    // A unit box 0.005 above another within a margin of 0.01: four points
    // 0.005 apart.
    mvfxBoxPolytope(origin, s_identity, s_unit, &b);
    const float hover[3] = {0.2f, 1.005f, 0.1f};
    mvfxBoxPolytope(hover, s_identity, s_unit, &a);
    met = mvfxConvexMeets(&a, &b, 0.01f, &meet);
    count = met ? mvfxConvexManifold(&a, &b, &meet, 0.01f, n, points) : 0;
    bool four = count == 4;
    for (int32_t i = 0; four && i < count; ++i)
    {
        four = fabsf(points[i].depth - 0.005f) <= 1e-5f;
    }
    CHECK(four, "apart within the margin");
}

static float Dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// The least overlap of a and b along unit l (below 0 apart).
static float OverlapAlong(const mvfxPolytope* a, const mvfxPolytope* b, const float l[3])
{
    float aMin = INFINITY;
    float aMax = -INFINITY;
    float bMin = INFINITY;
    float bMax = -INFINITY;
    for (int32_t i = 0; i < a->vertexCount; ++i)
    {
        aMin = fminf(aMin, Dot(a->vertices[i], l));
        aMax = fmaxf(aMax, Dot(a->vertices[i], l));
    }
    for (int32_t i = 0; i < b->vertexCount; ++i)
    {
        bMin = fminf(bMin, Dot(b->vertices[i], l));
        bMax = fmaxf(bMax, Dot(b->vertices[i], l));
    }
    return fminf(aMax - bMin, bMax - aMin);
}

// The polytope's edges' directions (each face's sides).
static int32_t EdgesOf(const mvfxPolytope* p, float out[][3])
{
    int32_t count = 0;
    for (int32_t f = 0; f < p->faceCount; ++f)
    {
        int32_t first = p->faceStart[f];
        int32_t corners = p->faceStart[f + 1] - first;
        for (int32_t j = 0; j < corners; ++j)
        {
            const float* u = p->vertices[p->loop[first + j]];
            const float* v = p->vertices[p->loop[first + (j + 1) % corners]];
            for (int32_t k = 0; k < 3; ++k)
            {
                out[count][k] = v[k] - u[k];
            }
            ++count;
        }
    }
    return count;
}

// The least overlap over every separating axis: faces and edge pairs.
static float LeastOverlap(const mvfxPolytope* a, const mvfxPolytope* b)
{
    float least = INFINITY;
    for (int32_t side = 0; side < 2; ++side)
    {
        const mvfxPolytope* p = side == 0 ? a : b;
        for (int32_t f = 0; f < p->faceCount; ++f)
        {
            least = fminf(least, OverlapAlong(a, b, p->planes[f]));
        }
    }
    float ea[MVFX_MAX_HULL_LOOP][3];
    float eb[MVFX_MAX_HULL_LOOP][3];
    int32_t na = EdgesOf(a, ea);
    int32_t nb = EdgesOf(b, eb);
    for (int32_t i = 0; i < na; ++i)
    {
        for (int32_t j = 0; j < nb; ++j)
        {
            float l[3] = {ea[i][1] * eb[j][2] - ea[i][2] * eb[j][1],
                          ea[i][2] * eb[j][0] - ea[i][0] * eb[j][2],
                          ea[i][0] * eb[j][1] - ea[i][1] * eb[j][0]};
            float length = sqrtf(Dot(l, l));
            if (length > 1e-4f)
            {
                for (int32_t k = 0; k < 3; ++k)
                {
                    l[k] /= length;
                }
                least = fminf(least, OverlapAlong(a, b, l));
            }
        }
    }
    return least;
}

// The distance from p to segment u v, in double.
static double SegmentDistance(const float u[3], const float v[3], const double p[3])
{
    double e[3];
    double d[3];
    for (int32_t k = 0; k < 3; ++k)
    {
        e[k] = (double)v[k] - (double)u[k];
        d[k] = p[k] - (double)u[k];
    }
    double length = e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
    double s = (d[0] * e[0] + d[1] * e[1] + d[2] * e[2]) / length;
    s = s < 0.0 ? 0.0 : (s > 1.0 ? 1.0 : s);
    double sum = 0.0;
    for (int32_t k = 0; k < 3; ++k)
    {
        double g = d[k] - s * e[k];
        sum += g * g;
    }
    return sqrt(sum);
}

// The distance from p outside polytope a by its features: each face's
// plane where p lies over the face, else its edges (and so its corners).
static double FeatureDistance(const mvfxPolytope* a, const double p[3])
{
    double least = (double)INFINITY;
    for (int32_t f = 0; f < a->faceCount; ++f)
    {
        const float* n = a->planes[f];
        double above =
            (double)n[0] * p[0] + (double)n[1] * p[1] + (double)n[2] * p[2] - (double)n[3];
        int32_t first = a->faceStart[f];
        int32_t corners = a->faceStart[f + 1] - first;
        bool over = true;
        for (int32_t j = 0; j < corners; ++j)
        {
            const float* u = a->vertices[a->loop[first + j]];
            const float* v = a->vertices[a->loop[first + (j + 1) % corners]];
            least = fmin(least, SegmentDistance(u, v, p));
            // Inward across the edge: the normal crossed with it.
            double e[3] = {(double)v[0] - (double)u[0], (double)v[1] - (double)u[1],
                           (double)v[2] - (double)u[2]};
            double in[3] = {(double)n[1] * e[2] - (double)n[2] * e[1],
                            (double)n[2] * e[0] - (double)n[0] * e[2],
                            (double)n[0] * e[1] - (double)n[1] * e[0]};
            over = over && in[0] * (p[0] - (double)u[0]) + in[1] * (p[1] - (double)u[1]) +
                                   in[2] * (p[2] - (double)u[2]) >=
                               0.0;
        }
        least = over && above >= 0.0 ? fmin(least, above) : least;
    }
    return least;
}

static void TestNearestOnBall(void)
{
    // The hull of 60 points on a unit ball (a Fibonacci sphere) and 200
    // points about it 1.2 to 3 from its center: each nearest distance is
    // the one its faces, edges and corners give.
    float ball[3 * 60];
    for (int32_t i = 0; i < 60; ++i)
    {
        float z = 1.0f - (2.0f * (float)i + 1.0f) / 60.0f;
        float r = sqrtf(1.0f - z * z);
        float angle = 2.3999632f * (float)i;
        ball[3 * i] = r * cosf(angle);
        ball[3 * i + 1] = r * sinf(angle);
        ball[3 * i + 2] = z;
    }
    mvfxHull hull;
    CHECK(mvfxBuildHull(ball, 60, &(mvfxAllocator){0}, &hull) == mvfx_success, "a ball's hull");
    static const float unit[3] = {1, 1, 1};
    static const float origin[3] = {0, 0, 0};
    mvfxPolytope a;
    mvfxHullPolytope(&hull, unit, origin, s_identity, &a);
    double worst = 0.0;
    for (int32_t i = 0; i < 200; ++i)
    {
        float t = (float)i;
        float d[3] = {sinf(1.7f * t + 0.3f), cosf(2.9f * t), sinf(0.61f * t + 2.0f)};
        float length = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        float reach = 1.2f + 1.8f * (0.5f + 0.5f * sinf(3.7f * t));
        const float p[3] = {reach * d[0] / length, reach * d[1] / length, reach * d[2] / length};
        const double q[3] = {(double)p[0], (double)p[1], (double)p[2]};
        float near[3];
        float distance = mvfxPolytopeNearest(&a, p, near);
        worst = fmax(worst, fabs((double)distance - FeatureDistance(&a, q)));
    }
    CHECK(worst <= 2e-6, "nearest on a ball's hull");
}

static void TestDepths(void)
{
    // A lopsided hull against another at 40 turns and offsets: where they
    // overlap, EPA's depth is the separating axes' least overlap.
    static const float lopsided[21] = {0,    0, 0,    1, 0, 0,    0, 0.6f, 0,    0,   0,
                                       0.3f, 1, 0.1f, 0, 0, 0.4f, 0, 0.7f, 0.8f, 1.1f};
    mvfxHull hull;
    CHECK(mvfxBuildHull(lopsided, 7, &(mvfxAllocator){0}, &hull) == mvfx_success, "built");
    static const float unit[3] = {1, 1, 1};
    int32_t overlapping = 0;
    bool agree = true;
    for (int32_t i = 0; i < 40; ++i)
    {
        float t = (float)i;
        float q[4] = {sinf(0.7f * t), cosf(1.3f * t), sinf(0.4f * t + 1.0f), 1.5f};
        float length = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        for (int32_t k = 0; k < 4; ++k)
        {
            q[k] /= length;
        }
        const float x[3] = {0.3f * sinf(t), 0.25f * cosf(0.6f * t), 0.2f * sinf(1.7f * t)};
        static const float origin[3] = {0, 0, 0};
        mvfxPolytope a;
        mvfxPolytope b;
        mvfxHullPolytope(&hull, unit, x, q, &a);
        mvfxHullPolytope(&hull, unit, origin, s_identity, &b);
        float overlap = LeastOverlap(&a, &b);
        mvfxConvexMeet meet;
        bool met = mvfxConvexMeets(&a, &b, 0.0f, &meet);
        if (overlap > 1e-3f)
        {
            overlapping += 1;
            agree = agree && met && fabsf(meet.distance + overlap) <= 1e-4f;
        }
    }
    CHECK(overlapping >= 20 && agree, "depths by the separating axes");
}

static void TestSteps(void)
{
    // Slabs 0.001 thick, the upper tilted by 1e-5 to 1e-3 about x, 1e-3
    // apart and 1e-4 into each other: answered as the separating axes say,
    // in fewer than half the steps.
    mvfxPolytope a;
    mvfxPolytope b;
    static const float origin[3] = {0, 0, 0};
    static const float slab[3] = {1, 0.0005f, 1};
    mvfxBoxPolytope(origin, s_identity, slab, &a);
    bool accurate = true;
    int32_t most = 0;
    for (int32_t i = 0; i < 20; ++i)
    {
        float tilt[4];
        TurnAbout(0, 1e-5f * (float)(1 + 5 * i), tilt);
        for (int32_t overlap = 0; overlap < 2; ++overlap)
        {
            const float place[3] = {0.1f, overlap != 0 ? 0.0009f : 0.002f, -0.2f};
            mvfxBoxPolytope(place, tilt, slab, &b);
            float lowest = INFINITY;
            for (int32_t v = 0; v < b.vertexCount; ++v)
            {
                lowest = fminf(lowest, b.vertices[v][1]);
            }
            mvfxConvexMeet meet;
            mvfxConvexSteps steps;
            bool met = mvfxConvexMeetsWithin(&b, &a, 1.0f, MVFX_CONVEX_STEPS, &meet, &steps);
            float expected = overlap != 0 ? -LeastOverlap(&b, &a) : lowest - 0.0005f;
            accurate = accurate && met && fabsf(meet.distance - expected) <= 2e-6f;
            most = steps.gjk > most ? steps.gjk : most;
            most = steps.epa > most ? steps.epa : most;
        }
    }
    CHECK(accurate && most < MVFX_CONVEX_STEPS / 2, "thin slabs nearly parallel");
    // Held to one step: GJK's distance at least the true one, EPA's depth
    // at most the true one, each marked capped.
    static const float unit[3] = {1, 1, 1};
    mvfxBoxPolytope(origin, s_identity, unit, &a);
    float turn[4];
    TurnAbout(1, 0.6f, turn);
    bool bounded = true;
    for (int32_t overlap = 0; overlap < 2; ++overlap)
    {
        // Apart by 0.31 (the turned box reaches 1.39 along x), or into it.
        const float place[3] = {overlap != 0 ? 0.8f : 2.7f, 0.3f, 0.2f};
        mvfxBoxPolytope(place, turn, unit, &b);
        mvfxConvexMeet full;
        mvfxConvexMeet held;
        mvfxConvexSteps steps;
        bool met = mvfxConvexMeetsWithin(&b, &a, 10.0f, MVFX_CONVEX_STEPS, &full, &steps);
        bool heldMet = mvfxConvexMeetsWithin(&b, &a, 10.0f, 1, &held, &steps);
        // Apart, the distance an upper bound; overlapping, the depth a
        // lower one: either way never less than the full answer.
        bounded = bounded && met && heldMet && steps.gjk <= 1 && steps.epa <= 1 &&
                  held.distance >= full.distance - 1e-6f && held.capped && !full.capped;
        // A GJK stopped short is met whatever the margin: the pair may
        // still touch, and is kept rather than missed (overlapping pairs
        // held to one step were answered apart).
        mvfxConvexMeet near;
        bool kept = mvfxConvexMeetsWithin(&b, &a, 0.01f, 1, &near, &steps);
        bool dropped = !mvfxConvexMeetsWithin(&b, &a, 0.01f, MVFX_CONVEX_STEPS, &near, &steps);
        bounded = bounded && kept && (overlap != 0 || dropped);
    }
    CHECK(bounded, "searches held to their steps");
}

int main(void)
{
    TestApart();
    TestOverlap();
    TestConcentric();
    TestManifolds();
    TestNearestOnBall();
    TestDepths();
    TestSteps();
    return s_failures == 0 ? 0 : 1;
}
