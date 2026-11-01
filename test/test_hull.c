// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Hulls of point sets as a rigid hull particle takes them, read from its
// emitter's view and its mesh's hull center and count: a cube's corners
// amid inner points and repeats giving eight vertices, six square faces,
// its volume and moments; a tetrahedron of unequal legs off the origin
// giving its centroid and its moments by the closed form; points of a
// ball's hull all inside every face, each face's corners on its plane and
// in order; a fan of nearly coplanar triangles merged by one plane, its
// faces sound and its volume exact; a hull of near-degenerate points
// hashed the same on every platform; hulls of 200 and 500 points counted
// past the most; points spanning no volume, and a hull of more than the
// most vertices, refused.

#include "test_harness.h"

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/mesh.h"
#include "maul-vfx/plan.h"

#include <math.h>
#include <string.h>

static mvfxContext* s_context;
static mvfxMeshId s_mesh;
static mvfxEmitterView s_view;
static float s_center[3];

// The hull of count points as a rigid hull particle takes it: a mesh of
// them in a context of its own, an effect of one rigid hull emitter over
// it, its emitter's view (the hull at a size of 1 about its centroid) in
// s_view, the mesh's hull center in s_center. Returns the effect's
// creation; the context stays open until Close.
static mvfxResult HullOf(const float* points, int32_t count)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxMeshDef meshDef = mvfxDefaultMeshDef();
    meshDef.positions = points;
    meshDef.vertexCount = count;
    CHECK(mvfxCreateMesh(s_context, &meshDef, &s_mesh) == mvfx_success, "mesh");
    bool hull = false;
    CHECK(mvfxMesh_GetHullCenter(s_context, s_mesh, &hull, s_center) == mvfx_success, "center");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 1;
    emitter.rigid = (mvfxRigidDef){
        .enable = true, .shape = mvfx_rigidHull, .density = 1.0f, .substeps = 1, .hull = s_mesh};
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    mvfxResult made = mvfxCreateEffect(s_context, &def, &effect, NULL);
    memset(&s_view, 0, sizeof s_view);
    if (made == mvfx_success)
    {
        CHECK(mvfxEffect_GetEmitterView(s_context, effect, 0, &s_view) == mvfx_success, "view");
    }
    return made;
}

static void Close(void)
{
    mvfxDestroyContext(s_context);
    s_context = NULL;
}

// Vertex i and face f's plane of the view's hull.
static const float* Vertex(int32_t i)
{
    return &s_view.hullVertices[3 * i];
}

static const float* Plane(int32_t f)
{
    return &s_view.hullPlanes[4 * f];
}

static void TestCube(void)
{
    // A 2 by 1 by 3 box's corners from (1, 2, 3), its center, a point on
    // a face and each corner twice.
    float points[3 * 18];
    int32_t n = 0;
    for (int32_t twice = 0; twice < 2; ++twice)
    {
        for (int32_t i = 0; i < 8; ++i)
        {
            points[3 * n] = 1.0f + ((i & 1) != 0 ? 2.0f : 0.0f);
            points[3 * n + 1] = 2.0f + ((i & 2) != 0 ? 1.0f : 0.0f);
            points[3 * n + 2] = 3.0f + ((i & 4) != 0 ? 3.0f : 0.0f);
            ++n;
        }
    }
    const float inner[6] = {2.0f, 2.5f, 4.5f, 3.0f, 2.25f, 4.0f};
    for (int32_t k = 0; k < 6; ++k)
    {
        points[3 * n + k] = inner[k];
    }
    n += 2;
    CHECK(HullOf(points, n) == mvfx_success, "a box's hull");
    bool squares = s_view.hullFaceCount == 6;
    for (int32_t f = 0; squares && f < 6; ++f)
    {
        squares = s_view.hullFaceStart[f + 1] - s_view.hullFaceStart[f] == 4;
    }
    // Moments of a box about its center: its volume times its side
    // squared over 12, nothing across.
    float volume = 6.0f;
    CHECK(s_view.hullVertexCount == 8 && squares && fabsf(s_view.hullVolume - volume) <= 1e-5f &&
              fabsf(s_view.hullMoments[0] - volume * 4.0f / 12.0f) <= 1e-4f &&
              fabsf(s_view.hullMoments[1] - volume * 1.0f / 12.0f) <= 1e-4f &&
              fabsf(s_view.hullMoments[2] - volume * 9.0f / 12.0f) <= 1e-4f &&
              fabsf(s_view.hullMoments[3]) <= 1e-5f && fabsf(s_view.hullMoments[4]) <= 1e-5f &&
              fabsf(s_view.hullMoments[5]) <= 1e-5f && fabsf(s_center[0] - 2.0f) <= 1e-5f &&
              fabsf(s_center[1] - 2.5f) <= 1e-5f && fabsf(s_center[2] - 4.5f) <= 1e-5f,
          "a box's mass");
    Close();
}

static void TestTetrahedron(void)
{
    // The corner tetrahedron of legs 1, 2 and 3 along x, y and z from (5,
    // -1, 2): volume 1, centroid a quarter along each leg, and about it
    // moments V a a 3/80 along each axis and -V a b / 80 across (from the
    // integrals V a a / 10 and V a b / 20 about the corner).
    const float points[12] = {5, -1, 2, 6, -1, 2, 5, 1, 2, 5, -1, 5};
    CHECK(HullOf(points, 4) == mvfx_success, "a tetrahedron's hull");
    const double legs[3] = {1.0, 2.0, 3.0};
    double v = 1.0;
    static const int32_t rows[3] = {0, 0, 1};
    static const int32_t columns[3] = {1, 2, 2};
    bool moments = true;
    for (int32_t k = 0; k < 3; ++k)
    {
        double along = v * legs[k] * legs[k] * 3.0 / 80.0;
        double across = -v * legs[rows[k]] * legs[columns[k]] / 80.0;
        moments = moments && fabs((double)s_view.hullMoments[k] - along) <= 1e-5 &&
                  fabs((double)s_view.hullMoments[3 + k] - across) <= 1e-5;
    }
    CHECK(s_view.hullVertexCount == 4 && s_view.hullFaceCount == 4 &&
              fabs((double)s_view.hullVolume - v) <= 1e-5 && moments &&
              fabsf(s_center[0] - 5.25f) <= 1e-5f && fabsf(s_center[1] + 0.5f) <= 1e-5f &&
              fabsf(s_center[2] - 2.75f) <= 1e-5f,
          "a tetrahedron's mass");
    Close();
}

// A point of a hash in [-1, 1).
static float Hashed(uint32_t i)
{
    uint32_t h = i * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return (float)(h & 0xFFFFFFu) / 8388608.0f - 1.0f;
}

static void TestConvex(void)
{
    // 400 points in a cube (corners cut by a ball of radius 1.3), their
    // hull of 62 vertices: every point inside or on each face, each face's
    // corners on its plane and counterclockwise.
    float points[3 * 400];
    int32_t n = 0;
    for (uint32_t i = 0; n < 400; ++i)
    {
        float p[3] = {Hashed(3 * i), Hashed(3 * i + 1), Hashed(3 * i + 2)};
        if (p[0] * p[0] + p[1] * p[1] + p[2] * p[2] <= 1.69f)
        {
            for (int32_t k = 0; k < 3; ++k)
            {
                points[3 * n + k] = p[k];
            }
            ++n;
        }
    }
    mvfxResult built = HullOf(points, n);
    CHECK(built == mvfx_success, "a ball's hull");
    bool inside = true;
    bool corners = true;
    for (int32_t f = 0; f < s_view.hullFaceCount; ++f)
    {
        const float* plane = Plane(f);
        for (int32_t i = 0; i < n; ++i)
        {
            float d = 0.0f;
            for (int32_t k = 0; k < 3; ++k)
            {
                d += plane[k] * (points[3 * i + k] - s_center[k]);
            }
            inside = inside && d - plane[3] <= 1e-5f;
        }
        int32_t first = s_view.hullFaceStart[f];
        int32_t count = s_view.hullFaceStart[f + 1] - first;
        for (int32_t j = 0; j < count; ++j)
        {
            const float* a = Vertex(s_view.hullLoop[first + j]);
            const float* b = Vertex(s_view.hullLoop[first + (j + 1) % count]);
            const float* c = Vertex(s_view.hullLoop[first + (j + 2) % count]);
            float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
            float e2[3] = {c[0] - b[0], c[1] - b[1], c[2] - b[2]};
            float turn = plane[0] * (e1[1] * e2[2] - e1[2] * e2[1]) +
                         plane[1] * (e1[2] * e2[0] - e1[0] * e2[2]) +
                         plane[2] * (e1[0] * e2[1] - e1[1] * e2[0]);
            float on = plane[0] * a[0] + plane[1] * a[1] + plane[2] * a[2] - plane[3];
            corners = corners && fabsf(on) <= 1e-5f && turn >= -1e-6f;
        }
    }
    CHECK(inside && corners, "a convex hull");
    Close();
}

static void TestFan(void)
{
    // A strip bulging gently over an arc of radius 10, 3e-4 radians a
    // column, two rows at y = -1 and 1, closed below at z = -1: each
    // triangle within the merging tolerance of its neighbors, its ends 20
    // tolerances apart. Faces merge against one plane, not along a chain:
    // the top several faces, every face's corners on its plane, each face
    // convex with every point inside, and the volume the exact one (the
    // shoelace area of the arc's section times the depth 2).
    float points[3 * 22];
    double section[11][2];
    int32_t n = 0;
    for (int32_t i = 0; i < 9; ++i)
    {
        double angle = 3e-4 * (double)i;
        double x = 10.0 * sin(angle);
        double z = 10.0 * (cos(angle) - 1.0);
        section[i][0] = (double)(float)x;
        section[i][1] = (double)(float)z;
        for (int32_t row = 0; row < 2; ++row)
        {
            points[3 * n] = (float)x;
            points[3 * n + 1] = row == 0 ? -1.0f : 1.0f;
            points[3 * n + 2] = (float)z;
            ++n;
        }
    }
    section[9][0] = section[8][0];
    section[9][1] = -1.0;
    section[10][0] = 0.0;
    section[10][1] = -1.0;
    for (int32_t corner = 0; corner < 4; ++corner)
    {
        points[3 * n] = corner < 2 ? 0.0f : (float)section[8][0];
        points[3 * n + 1] = corner % 2 == 0 ? -1.0f : 1.0f;
        points[3 * n + 2] = -1.0f;
        ++n;
    }
    double area = 0.0;
    for (int32_t i = 0; i < 11; ++i)
    {
        const double* p = section[i];
        const double* q = section[(i + 1) % 11];
        area += p[0] * q[1] - q[0] * p[1];
    }
    double volume = 2.0 * fabs(0.5 * area);
    CHECK(HullOf(points, n) == mvfx_success, "a fan's hull");
    bool sound = true;
    for (int32_t f = 0; f < s_view.hullFaceCount; ++f)
    {
        const float* plane = Plane(f);
        for (int32_t i = 0; i < n; ++i)
        {
            float d = 0.0f;
            for (int32_t k = 0; k < 3; ++k)
            {
                d += plane[k] * (points[3 * i + k] - s_center[k]);
            }
            sound = sound && d - plane[3] <= 2e-6f;
        }
        int32_t first = s_view.hullFaceStart[f];
        int32_t count = s_view.hullFaceStart[f + 1] - first;
        for (int32_t j = 0; j < count; ++j)
        {
            const float* a = Vertex(s_view.hullLoop[first + j]);
            const float* b = Vertex(s_view.hullLoop[first + (j + 1) % count]);
            const float* c = Vertex(s_view.hullLoop[first + (j + 2) % count]);
            float e1[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
            float e2[3] = {c[0] - b[0], c[1] - b[1], c[2] - b[2]};
            float turn = plane[0] * (e1[1] * e2[2] - e1[2] * e2[1]) +
                         plane[1] * (e1[2] * e2[0] - e1[0] * e2[2]) +
                         plane[2] * (e1[0] * e2[1] - e1[1] * e2[0]);
            float on = plane[0] * a[0] + plane[1] * a[1] + plane[2] * a[2] - plane[3];
            sound = sound && fabsf(on) <= 2e-6f && turn >= -1e-9f;
        }
    }
    // Chained, the whole top would be one face; by one plane it is several.
    int32_t tops = 0;
    for (int32_t f = 0; f < s_view.hullFaceCount; ++f)
    {
        tops += Plane(f)[2] > 0.99f ? 1 : 0;
    }
    CHECK(sound && tops >= 3 && fabs((double)s_view.hullVolume - volume) <= 1e-5 * volume,
          "a fan merged by one plane");
    Close();
}

// The hash of everything the view's hull holds, and its center.
static uint64_t HashHull(void)
{
    const mvfxEmitterView* v = &s_view;
    uint64_t h = mvfxHash64(0u, &v->hullVertexCount, (int64_t)sizeof v->hullVertexCount);
    h = mvfxHash64(h, &v->hullFaceCount, (int64_t)sizeof v->hullFaceCount);
    h = mvfxHash64(h, v->hullVertices, (int64_t)(3 * sizeof(float) * (size_t)v->hullVertexCount));
    h = mvfxHash64(h, v->hullPlanes, (int64_t)(4 * sizeof(float) * (size_t)v->hullFaceCount));
    h = mvfxHash64(h, v->hullFaceStart,
                   (int64_t)(sizeof v->hullFaceStart[0] * (size_t)(v->hullFaceCount + 1)));
    h = mvfxHash64(h, v->hullLoop,
                   (int64_t)(sizeof v->hullLoop[0] * (size_t)v->hullFaceStart[v->hullFaceCount]));
    h = mvfxHash64(h, &v->hullVolume, (int64_t)sizeof v->hullVolume);
    h = mvfxHash64(h, s_center, (int64_t)sizeof s_center);
    return mvfxHash64(h, v->hullMoments, (int64_t)sizeof v->hullMoments);
}

static void TestNearDegenerate(void)
{
    // Five points within 1e-6 of each of a unit cube's corners and twenty
    // within 3e-7 of its top face, near the tolerance everywhere: the hull
    // decides its ties and near-coplanar points the same on every
    // platform, its hash fixed.
    float points[3 * 60];
    int32_t n = 0;
    for (uint32_t i = 0; i < 40; ++i, ++n)
    {
        for (uint32_t k = 0; k < 3; ++k)
        {
            float corner = (i >> k & 1u) != 0u ? 0.5f : -0.5f;
            points[3 * n + (int32_t)k] = corner + 1e-6f * Hashed(1000u + 3u * i + k);
        }
    }
    for (uint32_t i = 0; i < 20; ++i, ++n)
    {
        points[3 * n] = 0.45f * Hashed(2000u + 3u * i);
        points[3 * n + 1] = 0.45f * Hashed(2001u + 3u * i);
        points[3 * n + 2] = 0.5f + 3e-7f * Hashed(2002u + 3u * i);
    }
    CHECK(HullOf(points, n) == mvfx_success, "a near-degenerate hull");
    CHECK(s_view.hullVertexCount == 12 && s_view.hullFaceCount == 8 &&
              HashHull() == 0xA4B089E4BC1A4BABu,
          "a near-degenerate hull, the same everywhere");
    Close();
}

static void TestRefused(void)
{
    // A square's corners and its center, a line, one point repeated; 200
    // points on a ball, a hull of more than 64 vertices.
    const float flat[15] = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0.5f, 0.5f, 0};
    const float line[12] = {0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3};
    const float same[12] = {1, 2, 3, 1, 2, 3, 1, 2, 3, 1, 2, 3};
    float ball[3 * 200];
    for (int32_t i = 0; i < 200; ++i)
    {
        // A Fibonacci sphere.
        float z = 1.0f - (2.0f * (float)i + 1.0f) / 200.0f;
        float r = sqrtf(1.0f - z * z);
        float angle = 2.3999632f * (float)i;
        ball[3 * i] = r * cosf(angle);
        ball[3 * i + 1] = r * sinf(angle);
        ball[3 * i + 2] = z;
    }
    // No hull: the effect refused as unsupported, the mesh without a
    // center; past the most vertices, refused as past a limit.
    bool refused = true;
    const float* none[4] = {flat, line, same, same};
    const int32_t counts[4] = {5, 4, 4, 3};
    for (int32_t i = 0; i < 4; ++i)
    {
        bool hull = true;
        refused = refused && HullOf(none[i], counts[i]) == mvfx_errorUnsupported &&
                  mvfxMesh_GetHullCenter(s_context, s_mesh, &hull, s_center) == mvfx_success &&
                  !hull;
        Close();
    }
    refused = refused && HullOf(ball, 200) == mvfx_errorLimit;
    Close();
    CHECK(refused, "refused");
}

static void TestCount(void)
{
    // Fibonacci spheres of 200 and 500 points (MVFX_MAX_HULL_COUNT): every
    // point a vertex, counted past the most a hull keeps, none built; one
    // of 1,500 past what a count holds, refused with a count of 0; a flat
    // set counted 0, no hull built.
    static float ball[3 * 1500];
    bool counted = true;
    for (int32_t total = 200; total <= 1500; total += total == 500 ? 1000 : 300)
    {
        for (int32_t i = 0; i < total; ++i)
        {
            float z = 1.0f - (2.0f * (float)i + 1.0f) / (float)total;
            float r = sqrtf(1.0f - z * z);
            float angle = 2.3999632f * (float)i;
            ball[3 * i] = r * cosf(angle);
            ball[3 * i + 1] = r * sinf(angle);
            ball[3 * i + 2] = z;
        }
        int32_t count = -1;
        counted =
            counted && HullOf(ball, total) == mvfx_errorLimit &&
            (total > MVFX_MAX_HULL_COUNT
                 ? mvfxMesh_GetHullVertexCount(s_context, s_mesh, &count) == mvfx_errorLimit &&
                       count == 0
                 : mvfxMesh_GetHullVertexCount(s_context, s_mesh, &count) == mvfx_success &&
                       count == total);
        Close();
    }
    const float flat[15] = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0.5f, 0.5f, 0};
    int32_t none = -1;
    bool flatNone = HullOf(flat, 5) == mvfx_errorUnsupported &&
                    mvfxMesh_GetHullVertexCount(s_context, s_mesh, &none) == mvfx_success &&
                    none == 0;
    Close();
    CHECK(counted && flatNone, "hull vertices counted");
}

int main(void)
{
    TestCube();
    TestTetrahedron();
    TestConvex();
    TestFan();
    TestNearDegenerate();
    TestCount();
    TestRefused();
    return s_failures == 0 ? 0 : 1;
}
