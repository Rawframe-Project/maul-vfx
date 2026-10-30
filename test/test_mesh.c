// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Meshes: their refusals, edges built once, normals, each mode's choice
// in proportion to length, area, count or volume within five standard
// deviations, points inside their elements and uniform there, moving a
// mesh, and meshes bound to effects and instances through the step. The
// meshes are read through their views, and where a mode places particles
// through the birth events of an emitter born on it: each event's place,
// and its velocity, the normal there at a speed of 1.

#include "particles.h"
#include "test_harness.h"

#include "maul-vfx/instance.h"
#include "maul-vfx/mesh.h"
#include "maul-vfx/plan.h"

#include <math.h>
#include <string.h>

#define SAMPLES 40000

// Whether count of SAMPLES draws matches a share within five standard
// deviations of a binomial.
static bool NearShare(int64_t count, double share)
{
    double deviation = sqrt(SAMPLES * share * (1.0 - share));
    return fabs((double)count - SAMPLES * share) <= 5.0 * deviation + 1e-9;
}

// The context the samples' meshes live in.
static mvfxContext* s_context;

static mvfxMeshId Build(const float* positions, const float* normals, int32_t vertices,
                        const uint32_t* triangles, int32_t triangleCount,
                        const uint32_t* tetrahedra, int32_t tetrahedronCount)
{
    mvfxMeshDef def = mvfxDefaultMeshDef();
    def.positions = positions;
    def.normals = normals;
    def.vertexCount = vertices;
    def.triangles = triangles;
    def.triangleCount = triangleCount;
    def.tetrahedra = tetrahedra;
    def.tetrahedronCount = tetrahedronCount;
    mvfxMeshId mesh = {0};
    CHECK(mvfxCreateMesh(s_context, &def, &mesh) == mvfx_success, "a valid mesh");
    return mesh;
}

static mvfxMeshView View(mvfxMeshId mesh)
{
    mvfxMeshView view;
    CHECK(mvfxMesh_GetView(s_context, mesh, &view) == mvfx_success, "its view");
    return view;
}

static void Free(mvfxMeshId mesh)
{
    CHECK(mvfxDestroyMesh(s_context, mesh) == mvfx_success, "destroyed");
}

// Points a mode places on a mesh: an emitter born on it, count in its
// first second, each birth event's place and velocity (the normal there,
// at a speed of 1).
static float s_places[SAMPLES][3];
static float s_normals[SAMPLES][3];
static int32_t Sample(mvfxMeshId mesh, mvfxMeshMode mode, int32_t count)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = count;
    emitter.rate = (float)count;
    emitter.lifetime = mvfxRangeSource(10.0f, 10.0f);
    emitter.shape.kind = mvfx_shapeMesh;
    emitter.shape.mesh = mesh;
    emitter.shape.meshMode = mode;
    emitter.velocity.mode = mvfx_directionShapeNormal;
    emitter.velocity.speed = (mvfxRange){1.0f, 1.0f};
    emitter.events = mvfx_eventMaskBirth;
    emitter.eventCapacity = count;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_context, &effectDef, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &instance) == mvfx_success, "instance");
    CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND) == mvfx_success, "step");
    const mvfxEvent* events = NULL;
    int32_t eventCount = 0;
    CHECK(mvfxInstance_GetEvents(s_context, instance, 0, &events, &eventCount) == mvfx_success,
          "events");
    int32_t n = 0;
    for (int32_t e = 0; e < eventCount && n < count; ++e)
    {
        if (events[e].kind == mvfx_eventBirth)
        {
            for (int32_t k = 0; k < 3; ++k)
            {
                s_places[n][k] = (float)events[e].place[k];
                s_normals[n][k] = events[e].velocity[k];
            }
            ++n;
        }
    }
    CHECK(n == count, "every sample born");
    CHECK(mvfxDestroyInstance(s_context, instance) == mvfx_success &&
              mvfxDestroyEffect(s_context, effect) == mvfx_success,
          "done");
    return n;
}

// A unit square of two triangles in the XY plane, and a triangle three
// times as large beside it.
static const float s_quad[] = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 2, 0, 0, 5, 0, 0, 2, 2, 0};
static const uint32_t s_quadTriangles[] = {0, 1, 2, 0, 2, 3, 4, 5, 6};

static void TestEdgesAndNormals(void)
{
    mvfxMeshId mesh = Build(s_quad, NULL, 7, s_quadTriangles, 3, NULL, 0);
    mvfxMeshView view = View(mesh);
    CHECK(view.edgeCount == 8, "the shared diagonal once: 5 + 3 edges");
    bool ordered = true;
    for (int32_t e = 0; e < view.edgeCount; ++e)
    {
        ordered = ordered && view.edges[2 * e] < view.edges[2 * e + 1];
    }
    CHECK(ordered, "the lower vertex first");
    bool up = true;
    for (int32_t v = 0; v < 7; ++v)
    {
        up = up && view.normals[3 * v] == 0.0f && view.normals[3 * v + 1] == 0.0f &&
             view.normals[3 * v + 2] == 1.0f;
    }
    CHECK(up, "a flat mesh's normals +Z");
    CHECK(fabs(view.triangleSums[2] - 4.0) < 1e-12, "areas summed: 1/2 + 1/2 + 3");
    Free(mesh);

    // Folded at a right angle along the X axis, two triangles of equal area
    // facing +Z and (by their winding) -Y: their shared vertices' normals
    // halfway between; the vertex no triangle touches +Z.
    static const float folded[] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, -1, 5, 5, 5};
    static const uint32_t foldedTriangles[] = {0, 1, 2, 1, 0, 3};
    mesh = Build(folded, NULL, 5, foldedTriangles, 2, NULL, 0);
    view = View(mesh);
    float half = 0.70710678f;
    CHECK(fabsf(view.normals[0]) <= 1e-6f && fabsf(view.normals[1] + half) <= 1e-6f &&
              fabsf(view.normals[2] - half) <= 1e-6f,
          "a shared vertex's normal the triangles' sum");
    CHECK(view.normals[12] == 0.0f && view.normals[13] == 0.0f && view.normals[14] == 1.0f,
          "an untouched vertex's normal +Z");
    Free(mesh);
}

static void TestSurfaceByArea(void)
{
    mvfxMeshId mesh = Build(s_quad, NULL, 7, s_quadTriangles, 3, NULL, 0);
    int32_t n = Sample(mesh, mvfx_meshSurface, SAMPLES);
    int64_t inLarge = 0;
    double sumX = 0.0;
    double sumY = 0.0;
    int64_t large = 0;
    bool inside = true;
    for (int32_t i = 0; i < n; ++i)
    {
        const float* p = s_places[i];
        const float* normal = s_normals[i];
        inside = inside && p[2] == 0.0f && normal[2] == 1.0f;
        if (p[0] >= 2.0f - 1e-6f)
        {
            // The large triangle (2,0), (5,0), (2,2): x >= 2, y >= 0 and
            // below its hypotenuse.
            inside = inside && p[1] >= -1e-6f && 2.0f * (p[0] - 2.0f) + 3.0f * p[1] <= 6.0f + 1e-5f;
            ++inLarge;
            sumX += (double)p[0];
            sumY += (double)p[1];
            ++large;
        }
        else
        {
            inside = inside && p[0] >= -1e-6f && p[0] <= 1.0f + 1e-6f && p[1] >= -1e-6f &&
                     p[1] <= 1.0f + 1e-6f;
        }
    }
    CHECK(inside, "every point in its triangle, the normal +Z");
    CHECK(NearShare(inLarge, 0.75), "triangles by area");
    // Uniform within a triangle: its points' mean at its centroid (3, 2/3).
    double deviation = 1.0 / sqrt((double)large);
    CHECK(fabs(sumX / (double)large - 3.0) <= 5.0 * deviation &&
              fabs(sumY / (double)large - 2.0 / 3.0) <= 5.0 * deviation,
          "uniform within a triangle");
    Free(mesh);

    // A triangle of no area is never chosen.
    static const float flat[] = {0, 0, 0, 1, 0, 0, 2, 0, 0, 0, 0, 1, 1, 0, 1};
    static const uint32_t flatTriangles[] = {0, 1, 2, 0, 1, 3, 1, 4, 3};
    mesh = Build(flat, NULL, 5, flatTriangles, 3, NULL, 0);
    n = Sample(mesh, mvfx_meshSurface, 4000);
    bool skipped = true;
    for (int32_t i = 0; i < n; ++i)
    {
        skipped = skipped && s_places[i][0] <= 1.0f + 1e-6f;
    }
    CHECK(skipped, "a degenerate triangle never chosen");
    Free(mesh);
    // Triangles all of no area (a mesh whose positions collapsed): the
    // first is chosen, whatever the draw.
    static const float line[] = {0, 0, 0, 1, 0, 0, 2, 0, 0, 5, 0, 0, 6, 0, 0};
    static const uint32_t lineTriangles[] = {0, 1, 2, 2, 3, 4};
    mvfxMeshId collapsed = Build(line, NULL, 5, lineTriangles, 2, NULL, 0);
    n = Sample(collapsed, mvfx_meshSurface, 1000);
    bool firstChosen = true;
    for (int32_t i = 0; i < n; ++i)
    {
        firstChosen = firstChosen && s_places[i][0] <= 2.0f + 1e-6f;
    }
    CHECK(firstChosen, "the first of triangles of no area chosen");
    Free(collapsed);
}

static void TestEdgesByLength(void)
{
    // A right triangle of sides 3, 4 and 5: its edges hit 3 : 4 : 5.
    static const float right[] = {0, 0, 0, 3, 0, 0, 0, 4, 0};
    static const uint32_t triangle[] = {0, 1, 2};
    mvfxMeshId mesh = Build(right, NULL, 3, triangle, 1, NULL, 0);
    int32_t n = Sample(mesh, mvfx_meshEdges, SAMPLES);
    int64_t hits[3] = {0};
    bool onHypotenuse = true;
    for (int32_t i = 0; i < n; ++i)
    {
        const float* p = s_places[i];
        if (fabsf(p[1]) <= 1e-6f)
        {
            ++hits[0];
        }
        else if (fabsf(p[0]) <= 1e-6f)
        {
            ++hits[1];
        }
        else
        {
            onHypotenuse = onHypotenuse && fabsf(4.0f * p[0] + 3.0f * p[1] - 12.0f) <= 1e-4f;
            ++hits[2];
        }
    }
    CHECK(onHypotenuse, "on the hypotenuse");
    CHECK(NearShare(hits[0], 3.0 / 12.0) && NearShare(hits[1], 4.0 / 12.0) &&
              NearShare(hits[2], 5.0 / 12.0),
          "edges by length");
    Free(mesh);
}

static void TestVerticesAndGivenNormals(void)
{
    static const float normals[] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, -1};
    static const float corners[] = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
    mvfxMeshId mesh = Build(corners, normals, 4, s_quadTriangles, 2, NULL, 0);
    int32_t n = Sample(mesh, mvfx_meshVertices, SAMPLES);
    int64_t hits[4] = {0};
    bool given = true;
    for (int32_t i = 0; i < n; ++i)
    {
        const float* p = s_places[i];
        const float* normal = s_normals[i];
        int32_t v = (p[0] > 0.5f ? 1 : 0) + (p[1] > 0.5f ? 2 : 0);
        static const int32_t index[4] = {0, 1, 3, 2};
        int32_t at = index[v];
        given = given && normal[0] == normals[3 * at] && normal[1] == normals[3 * at + 1] &&
                normal[2] == normals[3 * at + 2];
        ++hits[at];
    }
    CHECK(given, "a vertex's given normal");
    for (int32_t v = 0; v < 4; ++v)
    {
        CHECK(NearShare(hits[v], 0.25), "vertices alike");
    }
    // On the surface, the given normals blend: the first triangle's three
    // (+X, +Y, +Z) give a normal with every component at least 0.
    n = Sample(mesh, mvfx_meshSurface, 4000);
    bool blended = true;
    for (int32_t i = 0; i < n; ++i)
    {
        const float* normal = s_normals[i];
        double length =
            sqrt((double)normal[0] * (double)normal[0] + (double)normal[1] * (double)normal[1] +
                 (double)normal[2] * (double)normal[2]);
        blended = blended && fabs(length - 1.0) <= 1e-5;
    }
    CHECK(blended, "blended normals of unit length");
    Free(mesh);
}

static void TestVolumeByVolume(void)
{
    // Two tetrahedra sharing a face, of volumes 1/6 and 2/6.
    static const float points[] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, -2};
    static const uint32_t tetrahedra[] = {0, 1, 2, 3, 0, 2, 1, 4};
    mvfxMeshId mesh = Build(points, NULL, 5, NULL, 0, tetrahedra, 2);
    int32_t n = Sample(mesh, mvfx_meshVolume, SAMPLES);
    int64_t below = 0;
    double sum[3] = {0.0};
    int64_t above = 0;
    bool inside = true;
    for (int32_t i = 0; i < n; ++i)
    {
        const float* p = s_places[i];
        inside = inside && p[0] >= -1e-6f && p[1] >= -1e-6f;
        if (p[2] < 0.0f)
        {
            inside = inside && p[0] + p[1] - p[2] * 0.5f <= 1.0f + 1e-5f;
            ++below;
        }
        else
        {
            inside = inside && p[0] + p[1] + p[2] <= 1.0f + 1e-5f;
            for (int32_t k = 0; k < 3; ++k)
            {
                sum[k] += (double)p[k];
            }
            ++above;
        }
    }
    CHECK(inside, "every point in its tetrahedron");
    CHECK(NearShare(below, 2.0 / 3.0), "tetrahedra by volume");
    // Uniform within one: its points' mean at its centroid (1/4, 1/4, 1/4),
    // each coordinate's deviation sqrt(3/80).
    double deviation = sqrt(3.0 / 80.0 / (double)above);
    for (int32_t k = 0; k < 3; ++k)
    {
        CHECK(fabs(sum[k] / (double)above - 0.25) <= 5.0 * deviation, "uniform within one");
    }
    Free(mesh);
}

static void TestMoving(void)
{
    mvfxMeshId mesh = Build(s_quad, NULL, 7, s_quadTriangles, 3, NULL, 0);
    // The large triangle shrunk to the others' size and tipped up: a third
    // of the surface, its normal now along -Y... any way but +Z.
    float moved[21];
    for (int32_t i = 0; i < 21; ++i)
    {
        moved[i] = s_quad[i];
    }
    moved[3 * 5] = 3.0f;
    moved[3 * 6 + 1] = 0.0f;
    moved[3 * 6 + 2] = 1.0f;
    CHECK(mvfxMesh_SetPositions(s_context, mesh, moved, NULL, 7) == mvfx_success, "moved");
    int32_t n = Sample(mesh, mvfx_meshSurface, SAMPLES);
    int64_t inLarge = 0;
    bool tipped = true;
    for (int32_t i = 0; i < n; ++i)
    {
        if (s_places[i][0] >= 2.0f - 1e-6f)
        {
            tipped = tipped && fabsf(s_normals[i][1]) == 1.0f;
            ++inLarge;
        }
    }
    CHECK(tipped, "normals rebuilt");
    CHECK(NearShare(inLarge, 1.0 / 3.0), "sums rebuilt");
    Free(mesh);
}

static void TestRefusals(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    contextDef.limits.meshes = 2;
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    uint64_t misuse = 0;
    static const float nan[] = {0, 0, 0, NAN, 0, 0, 0, 1, 0};
    static const float far[] = {0, 0, 0, 2e6f, 0, 0, 0, 1, 0};
    static const float badNormals[] = {0, 0, 3, 0, 0, 1, 0, 0, 1};
    static const uint32_t past[] = {0, 1, 3};
    for (int32_t field = 0; field < 13; ++field)
    {
        // Past a named bound: mvfx_errorLimit, data a correct program may
        // pass, so no misuse.
        mvfxResult want =
            field == 3 || field == 9 || field == 11 ? mvfx_errorLimit : mvfx_errorInvalid;
        mvfxMeshDef def = mvfxDefaultMeshDef();
        def.positions = s_quad;
        def.vertexCount = 3;
        def.triangles = s_quadTriangles;
        def.triangleCount = 1;
        switch (field)
        {
        case 0:
            def.cookie = 0;
            break;
        case 1:
            def.positions = NULL;
            break;
        case 2:
            def.vertexCount = 0;
            break;
        case 3:
            def.vertexCount = MVFX_MAX_MESH_VERTICES + 1;
            break;
        case 4:
            def.triangles = past;
            break;
        case 5:
            def.triangles = NULL;
            break;
        case 6:
            def.triangleCount = -1;
            break;
        case 7:
            def.tetrahedronCount = 1;
            break;
        case 8:
            def.positions = nan;
            break;
        case 9:
            def.positions = far;
            break;
        case 10:
            def.normals = badNormals;
            break;
        case 11:
            def.tetrahedra = past;
            def.tetrahedronCount = MVFX_MAX_MESH_ELEMENTS + 1;
            break;
        default:
        {
            static const uint32_t tetrahedron[] = {0, 1, 2, 3};
            def.tetrahedra = tetrahedron;
            def.tetrahedronCount = 1;
            break;
        }
        }
        mvfxMeshId mesh = {5, 5};
        CHECK(mvfxCreateMesh(context, &def, &mesh) == want, "refused");
        misuse += want == mvfx_errorInvalid ? 1u : 0u;
        CHECK(mesh.index1 == 0 && mvfxContext_GetMisuse(context) == misuse, "counted");
    }
    mvfxMeshId mesh = {0};
    CHECK(mvfxCreateMesh(context, NULL, &mesh) == mvfx_errorInvalid, "no def");
    CHECK(mvfxCreateMesh(NULL, NULL, &mesh) == mvfx_errorInvalid, "no context");
    misuse += 1;
    mvfxMeshDef def = mvfxDefaultMeshDef();
    def.positions = s_quad;
    def.vertexCount = 7;
    def.triangles = s_quadTriangles;
    def.triangleCount = 3;
    CHECK(mvfxCreateMesh(context, &def, NULL) == mvfx_errorInvalid, "no out-parameter");
    misuse += 1;
    mvfxMeshId a = {0};
    mvfxMeshId b = {0};
    CHECK(mvfxCreateMesh(context, &def, &a) == mvfx_success, "first");
    CHECK(mvfxCreateMesh(context, &def, &b) == mvfx_success, "second");
    CHECK(mvfxCreateMesh(context, &def, &mesh) == mvfx_errorCapacity, "past the table");
    CHECK(mvfxMesh_IsValid(context, a) && !mvfxMesh_IsValid(context, (mvfxMeshId){0}) &&
              !mvfxMesh_IsValid(NULL, a),
          "validity");
    CHECK(mvfxMesh_SetPositions(context, a, s_quad, NULL, 6) == mvfx_errorInvalid,
          "another count of vertices");
    CHECK(mvfxMesh_SetPositions(context, a, NULL, NULL, 7) == mvfx_errorInvalid, "no positions");
    float bad[21];
    for (int32_t i = 0; i < 21; ++i)
    {
        bad[i] = s_quad[i];
    }
    bad[4] = INFINITY;
    CHECK(mvfxMesh_SetPositions(context, a, bad, NULL, 7) == mvfx_errorInvalid, "a bad position");
    misuse += 3;
    CHECK(mvfxContext_GetMisuse(context) == misuse, "each counted");
    CHECK(mvfxMesh_SetPositions(NULL, a, s_quad, NULL, 7) == mvfx_errorInvalid, "no context");
    CHECK(mvfxMesh_SetPositions(context, (mvfxMeshId){a.index1, a.generation + 1}, s_quad, NULL,
                                7) == mvfx_errorStale,
          "a stale mesh");
    CHECK(mvfxMesh_SetPositions(context, a, s_quad, NULL, 7) == mvfx_success, "moved");
    CHECK(mvfxDestroyMesh(context, b) == mvfx_success, "destroyed");
    CHECK(mvfxDestroyMesh(context, b) == mvfx_errorStale, "twice is stale");
    CHECK(mvfxDestroyMesh(NULL, a) == mvfx_errorInvalid, "no context");
    CHECK(mvfxContext_GetMisuse(context) == misuse, "stale ids are not misuse");
    mvfxDestroyContext(context);

    contextDef.limits.meshes = 0;
    mvfxContext* none = NULL;
    CHECK(mvfxCreateContext(&contextDef, &none) == mvfx_errorInvalid, "no meshes allowed");
    contextDef.limits.meshes = 65537;
    CHECK(mvfxCreateContext(&contextDef, &none) == mvfx_errorInvalid, "too many meshes");
}

static mvfxEffectDef MeshEffect(mvfxEmitterDef* emitters, mvfxMeshId mesh, mvfxMeshMode mode)
{
    emitters[0] = mvfxDefaultEmitterDef();
    emitters[0].shape.kind = mvfx_shapeMesh;
    emitters[0].shape.mesh = mesh;
    emitters[0].shape.meshMode = mode;
    emitters[0].shape.offset[2] = 7.0f;
    emitters[1] = mvfxDefaultEmitterDef();
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = 2;
    return def;
}

static void TestThroughTheStep(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxMeshDef def = mvfxDefaultMeshDef();
    def.positions = s_quad;
    def.vertexCount = 7;
    def.triangles = s_quadTriangles;
    def.triangleCount = 3;
    mvfxMeshId flat = {0};
    CHECK(mvfxCreateMesh(context, &def, &flat) == mvfx_success, "a flat mesh");
    static const float raised[] = {0, 0, 5, 1, 0, 5, 0, 1, 5, 0, 0, 6};
    static const uint32_t tetrahedron[] = {0, 1, 2, 3};
    static const uint32_t face[] = {0, 1, 2};
    mvfxMeshDef solidDef = mvfxDefaultMeshDef();
    solidDef.positions = raised;
    solidDef.vertexCount = 4;
    solidDef.triangles = face;
    solidDef.triangleCount = 1;
    solidDef.tetrahedra = tetrahedron;
    solidDef.tetrahedronCount = 1;
    mvfxMeshId solid = {0};
    CHECK(mvfxCreateMesh(context, &solidDef, &solid) == mvfx_success, "a solid mesh");

    mvfxEmitterDef emitters[2];
    mvfxEffectDef effectDef = MeshEffect(emitters, flat, mvfx_meshVolume);
    mvfxEffectId effect = {0};
    uint64_t misuse = mvfxContext_GetMisuse(context);
    CHECK(mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_errorInvalid,
          "a volume needs tetrahedra");
    CHECK(mvfxContext_GetMisuse(context) == misuse + 1, "counted");
    effectDef =
        MeshEffect(emitters, (mvfxMeshId){flat.index1, flat.generation + 1}, mvfx_meshSurface);
    CHECK(mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_errorStale, "a stale mesh");
    effectDef = MeshEffect(emitters, flat, mvfx_meshSurface);
    CHECK(mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success, "a surface effect");
    CHECK(mvfxDestroyMesh(context, flat) == mvfx_errorBusy, "busy while an effect uses it");

    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    // The meshes as stored, each with a serial of its own; the instance's
    // emitter spawning from the effect's.
    mvfxMeshView flatView;
    mvfxMeshView solidView;
    mvfxMeshView spawning;
    CHECK(mvfxMesh_GetView(context, flat, &flatView) == mvfx_success && flatView.vertexCount == 7 &&
              flatView.triangleCount == 3 && flatView.edgeCount > 0 &&
              flatView.positions[2] == s_quad[2] &&
              flatView.triangleSums[2] > flatView.triangleSums[0] && flatView.serial != 0,
          "a mesh's view");
    CHECK(mvfxMesh_GetView(context, solid, &solidView) == mvfx_success &&
              solidView.tetrahedronCount == 1 && solidView.tetrahedronSums[0] > 0.0 &&
              solidView.serial != flatView.serial,
          "another mesh's serial");
    CHECK(mvfxInstance_GetEmitterMesh(context, instance, 0, &spawning) == mvfx_success &&
              spawning.serial == flatView.serial && spawning.positions == flatView.positions,
          "the emitter's mesh, its effect's");
    CHECK(mvfxInstance_GetEmitterMesh(context, instance, 1, &spawning) == mvfx_errorInvalid &&
              spawning.positions == NULL &&
              mvfxInstance_GetEmitterMesh(context, instance, 2, &spawning) == mvfx_errorInvalid &&
              mvfxInstance_GetEmitterMesh(context, instance, 0, NULL) == mvfx_errorInvalid &&
              mvfxInstance_GetEmitterMesh(NULL, instance, 0, &spawning) == mvfx_errorInvalid,
          "no mesh shape, no emitter, no view, no context");
    CHECK(mvfxMesh_GetView(context, (mvfxMeshId){0}, &spawning) == mvfx_errorStale &&
              mvfxMesh_GetView(context, flat, NULL) == mvfx_errorInvalid &&
              mvfxMesh_GetView(NULL, flat, &spawning) == mvfx_errorInvalid,
          "a stale mesh, no view, no context");
    CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 2) == mvfx_success, "step");
    static TestParticle records[64];
    int32_t count = ReadParticles(context, instance, 0, records, 64);
    bool onFlat = count == 32;
    for (int32_t i = 0; i < count; ++i)
    {
        onFlat = onFlat && records[i].position[2] == 7.0f;
    }
    CHECK(onFlat, "born on the flat mesh, moved by the offset");

    misuse = mvfxContext_GetMisuse(context);
    CHECK(mvfxInstance_SetEmitterMesh(context, instance, 1, solid) == mvfx_errorInvalid,
          "not a mesh shape");
    CHECK(mvfxInstance_SetEmitterMesh(context, instance, 2, solid) == mvfx_errorInvalid,
          "no such emitter");
    CHECK(mvfxContext_GetMisuse(context) == misuse + 2, "counted");
    CHECK(mvfxInstance_SetEmitterMesh(context, instance, 0, (mvfxMeshId){0}) == mvfx_errorStale,
          "a stale mesh");
    static const uint32_t onlyCells[] = {0, 1, 2, 3};
    mvfxMeshDef cellsDef = mvfxDefaultMeshDef();
    cellsDef.positions = raised;
    cellsDef.vertexCount = 4;
    cellsDef.tetrahedra = onlyCells;
    cellsDef.tetrahedronCount = 1;
    mvfxMeshId cellsOnly = {0};
    CHECK(mvfxCreateMesh(context, &cellsDef, &cellsOnly) == mvfx_success, "a mesh of cells");
    misuse = mvfxContext_GetMisuse(context);
    CHECK(mvfxInstance_SetEmitterMesh(context, instance, 0, cellsOnly) == mvfx_errorInvalid,
          "a surface needs triangles");
    CHECK(mvfxContext_GetMisuse(context) == misuse + 1, "counted");
    CHECK(mvfxInstance_SetEmitterMesh(NULL, instance, 0, solid) == mvfx_errorInvalid, "no context");
    CHECK(mvfxInstance_SetEmitterMesh(context, instance, 0, solid) == mvfx_success, "bound");
    CHECK(mvfxInstance_GetEmitterMesh(context, instance, 0, &spawning) == mvfx_success &&
              spawning.serial == solidView.serial,
          "the emitter's mesh, the one bound");
    CHECK(mvfxDestroyMesh(context, solid) == mvfx_errorBusy, "busy while an instance uses it");
    int32_t before = count;
    CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 4) == mvfx_success, "step");
    count = ReadParticles(context, instance, 0, records, 64);
    bool onSolid = count == before + 16;
    for (int32_t i = before; i < count; ++i)
    {
        onSolid = onSolid && records[i].position[2] == 12.0f;
    }
    CHECK(onSolid, "born on the bound mesh's face");
    // Moved, the bound mesh takes a new serial, past every one before.
    static const float lowered[] = {0, 0, 4, 1, 0, 4, 0, 1, 4, 0, 0, 5};
    CHECK(mvfxMesh_SetPositions(context, solid, lowered, NULL, 4) == mvfx_success, "moved");
    CHECK(mvfxInstance_GetEmitterMesh(context, instance, 0, &spawning) == mvfx_success &&
              spawning.serial > solidView.serial && spawning.serial > flatView.serial &&
              spawning.positions[2] == 4.0f,
          "a new serial for new positions");

    CHECK(mvfxDestroyInstance(context, instance) == mvfx_success, "instance gone");
    CHECK(mvfxInstance_GetEmitterMesh(context, instance, 0, &spawning) == mvfx_errorStale,
          "a stale instance");
    CHECK(mvfxDestroyMesh(context, solid) == mvfx_success, "free once the instance is gone");
    CHECK(mvfxDestroyEffect(context, effect) == mvfx_success, "effect gone");
    CHECK(mvfxDestroyMesh(context, flat) == mvfx_success, "free once the effect is gone");
    mvfxDestroyContext(context);
}

int main(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    TestEdgesAndNormals();
    TestSurfaceByArea();
    TestEdgesByLength();
    TestVerticesAndGivenNormals();
    TestVolumeByVolume();
    TestMoving();
    mvfxDestroyContext(s_context);
    TestRefusals();
    TestThroughTheStep();
    return s_failures == 0 ? 0 : 1;
}
