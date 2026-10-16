// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A surface's mesh by marching cubes (and squares): the refusals and the
// scratch's size; a lone particle's sphere closed, its Euler
// characteristic 2, its vertices on the sphere and its normals radial, in
// a turned instance far from the origin; two drops, characteristic 4;
// random fields of many ambiguous faces closed without a crack; a circle
// and random 2D fields closed loops; the capacities' partial writes and
// true counts; the same bytes as committed.

#include "gas.h"
#include "instance.h"
#include "instance_api.h"
#include "test_harness.h"

#include "maul-vfx/base.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/gas.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/output.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static mvfxContext* s_context;
static mvfxInstanceId s_instance;

// The next instance's origin and turn about Z (radians).
static double s_origin[3];
static float s_turn;

// The mesh's buffers, large enough for every case here.
#define VERTICES (1 << 16)
#define INDICES  (1 << 18)
static float s_positions[3 * VERTICES];
static float s_normals[3 * VERTICES];
static uint32_t s_indices[INDICES];
static int32_t s_scratch[1 << 16];

// Opens a surface of n by n by depth cells of cell over emitter 0's
// particles of radius and reach 3; false when refused.
static bool Open(int32_t n, int32_t depth, float cell, float radius, mvfxGasKind kind)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.kind = kind;
    gas.size[0] = n;
    gas.size[1] = n;
    gas.size[2] = depth;
    gas.cellSize = cell;
    gas.surface.radius = radius;
    gas.surface.reach = 3.0f;
    gas.surface.smoothing = 0;
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 64;
    emitter.hostSpawnCapacity = 64;
    emitter.lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    def.gases = &gas;
    def.gasCount = 1;
    mvfxEffectId effect = {0};
    if (mvfxCreateEffect(s_context, &def, &effect) != mvfx_success)
    {
        mvfxDestroyContext(s_context);
        return false;
    }
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    memcpy(instanceDef.origin, s_origin, sizeof s_origin);
    instanceDef.rotation[2] = sinf(0.5f * s_turn);
    instanceDef.rotation[3] = cosf(0.5f * s_turn);
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
    return true;
}

// Spawns particles at places in the world and steps one tick.
static void Spawn(const double* places, int32_t count)
{
    mvfxSpawnRequest request = {.count = count, .positions = places};
    CHECK(mvfxInstance_Spawn(s_context, s_instance, 0, &request) == mvfx_success, "spawned");
    CHECK(mvfxContext_StepTicks(s_context, 1) == mvfx_success, "born");
}

// The buffers at their full sizes.
static mvfxSurfaceMeshBuffers Buffers(void)
{
    return (mvfxSurfaceMeshBuffers){.positions = s_positions,
                                    .normals = s_normals,
                                    .vertexCapacity = VERTICES,
                                    .indices = s_indices,
                                    .indexCapacity = INDICES,
                                    .scratch = s_scratch,
                                    .scratchBytes = (int64_t)sizeof s_scratch};
}

// Meshes the surface at level 0 relative to origin; its counts.
static void Mesh(const double origin[3], int64_t* vertices, int64_t* indices)
{
    mvfxSurfaceMeshBuffers buffers = Buffers();
    CHECK(mvfxInstance_WriteSurfaceMesh(s_context, s_instance, 0, origin, 0.0f, &buffers, vertices,
                                        indices) == mvfx_success,
          "meshed");
    CHECK(*vertices <= VERTICES && *indices <= INDICES, "the buffers hold the mesh");
}

static int Compare(const void* a, const void* b)
{
    uint64_t x = *(const uint64_t*)a;
    uint64_t y = *(const uint64_t*)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

// Whether a mesh of triangles is closed (each directed edge once, its
// reverse once) and its Euler characteristic (vertices less edges plus
// triangles).
static bool ClosedSurface(int64_t vertices, int64_t indices, int64_t* euler)
{
    int64_t edges = indices;
    uint64_t* keys = malloc(sizeof(uint64_t) * (size_t)(edges > 0 ? edges : 1));
    CHECK(keys != NULL, "memory");
    for (int64_t e = 0; e < edges; ++e)
    {
        uint64_t a = s_indices[e];
        uint64_t b = s_indices[e % 3 == 2 ? e - 2 : e + 1];
        keys[e] = a << 32 | b;
    }
    qsort(keys, (size_t)edges, sizeof(uint64_t), Compare);
    bool closed = true;
    for (int64_t i = 0; closed && i < edges; ++i)
    {
        uint64_t back = keys[i] << 32 | keys[i] >> 32;
        int64_t lo = 0;
        int64_t hi = edges;
        while (lo < hi)
        {
            int64_t mid = (lo + hi) / 2;
            lo = keys[mid] < back ? mid + 1 : lo;
            hi = keys[mid] < back ? hi : mid;
        }
        closed = (i == 0 || keys[i] != keys[i - 1]) && lo < edges && keys[lo] == back;
    }
    free(keys);
    *euler = vertices - edges / 2 + indices / 3;
    return closed;
}

// Whether a mesh of segments is closed: each vertex one segment's end and
// another's start.
static bool ClosedCurve(int64_t vertices, int64_t indices)
{
    int32_t* in = calloc((size_t)(vertices > 0 ? vertices : 1), sizeof(int32_t));
    int32_t* out = calloc((size_t)(vertices > 0 ? vertices : 1), sizeof(int32_t));
    if (in == NULL || out == NULL)
    {
        free(in);
        free(out);
        return false;
    }
    for (int64_t s = 0; s + 1 < indices; s += 2)
    {
        out[s_indices[s]] += 1;
        in[s_indices[s + 1]] += 1;
    }
    bool closed = true;
    for (int64_t v = 0; v < vertices; ++v)
    {
        closed = closed && in[v] == 1 && out[v] == 1;
    }
    free(in);
    free(out);
    return closed;
}

// Whether a mesh is closed, by its kind; and a surface's characteristic
// (a curve's 0).
static bool Closed(int64_t vertices, int64_t indices, int32_t per, int64_t* euler)
{
    *euler = 0;
    return per == 3 ? ClosedSurface(vertices, indices, euler) : ClosedCurve(vertices, indices);
}

// The result of a mesh call spoiled by case c: a misaligned or NULL
// positions buffer with a size, a NULL indices buffer with a size, a
// negative capacity, scratch too small, a NULL or far origin, a level not
// finite, NULL buffers, a NULL count.
static mvfxResult Spoiled(int32_t c, int64_t bytes)
{
    static const double zero[3] = {0.0, 0.0, 0.0};
    static const double far[3] = {0x1p53, 0.0, 0.0};
    mvfxSurfaceMeshBuffers b = Buffers();
    const double* origin = zero;
    float level = 0.0f;
    const mvfxSurfaceMeshBuffers* buffers = &b;
    int64_t v = 0;
    int64_t i = 0;
    int64_t* count = &v;
    switch (c)
    {
    case 0:
        b.positions = (float*)((char*)s_positions + 1);
        break;
    case 1:
        b.positions = NULL;
        break;
    case 2:
        b.indices = NULL;
        break;
    case 3:
        b.vertexCapacity = -1;
        break;
    case 4:
        b.scratchBytes = bytes - 4;
        break;
    case 5:
        origin = NULL;
        break;
    case 6:
        origin = far;
        break;
    case 7:
        level = NAN;
        break;
    case 8:
        buffers = NULL;
        break;
    default:
        count = NULL;
        break;
    }
    return mvfxInstance_WriteSurfaceMesh(s_context, s_instance, 0, origin, level, buffers, count,
                                         &i);
}

static void TestRefusals(void)
{
    // A smoke gas; each spoiled call (scratch too small a capacity error,
    // the others invalid); then a valid call over nothing. The scratch a
    // 3D and a 2D surface take.
    CHECK(Open(8, 8, 0.1f, 0.1f, mvfx_gasSmoke), "opened");
    int64_t bytes = 0;
    mvfxSurfaceMeshBuffers b = Buffers();
    int64_t v = -1;
    int64_t i = -1;
    const double zero[3] = {0.0, 0.0, 0.0};
    bool right = mvfxInstance_GetSurfaceMeshScratchBytes(s_context, s_instance, 0, &bytes) ==
                     mvfx_errorInvalid &&
                 mvfxInstance_WriteSurfaceMesh(s_context, s_instance, 0, zero, 0.0f, &b, &v, &i) ==
                     mvfx_errorInvalid;
    mvfxDestroyContext(s_context);
    CHECK(Open(8, 8, 0.1f, 0.1f, mvfx_gasSurface), "opened");
    right =
        right &&
        mvfxInstance_GetSurfaceMeshScratchBytes(s_context, s_instance, 0, &bytes) == mvfx_success &&
        bytes == 24 * 64;
    for (int32_t c = 0; c < 10; ++c)
    {
        right = right && Spoiled(c, bytes) == (c == 4 ? mvfx_errorCapacity : mvfx_errorInvalid);
    }
    right = right &&
            mvfxInstance_WriteSurfaceMesh(s_context, s_instance, 0, zero, 0.0f, &b, &v, &i) ==
                mvfx_success &&
            v == 0 && i == 0;
    mvfxDestroyContext(s_context);
    CHECK(Open(8, 1, 0.1f, 0.1f, mvfx_gasSurface), "opened");
    right =
        right &&
        mvfxInstance_GetSurfaceMeshScratchBytes(s_context, s_instance, 0, &bytes) == mvfx_success &&
        bytes == 16 * 8;
    mvfxDestroyContext(s_context);
    CHECK(right, "a mesh's refusals");
}

// Checks a lone particle's sphere (circle for depth 1) of radius 0.25 at
// center in a box of 24 cells of 0.05: closed, characteristic 2 (a loop's
// 0), vertices within 0.002 of the sphere, normals within a degree of
// radial.
static bool Sphere(int32_t depth, const double center[3])
{
    CHECK(Open(24, depth, 0.05f, 0.25f, mvfx_gasSurface), "opened");
    Spawn(center, 1);
    int64_t vertices = 0;
    int64_t indices = 0;
    Mesh(s_origin, &vertices, &indices);
    int64_t euler = 0;
    bool right = Closed(vertices, indices, depth > 1 ? 3 : 2, &euler) &&
                 euler == (depth > 1 ? 2 : 0) && vertices > (depth > 1 ? 400 : 32);
    float worst = 0.0f;
    float cosine = 1.0f;
    for (int64_t v = 0; v < vertices; ++v)
    {
        double d[3];
        for (int32_t k = 0; k < 3; ++k)
        {
            d[k] = (double)s_positions[3 * v + k] + s_origin[k] - center[k];
        }
        double r = sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        worst = fmaxf(worst, (float)fabs(r - 0.25));
        double along = 0.0;
        for (int32_t k = 0; k < 3; ++k)
        {
            along += (double)s_normals[3 * v + k] * d[k] / r;
        }
        cosine = fminf(cosine, (float)along);
    }
    mvfxDestroyContext(s_context);
    return right && worst <= 0.002f && cosine >= cosf(3.14159265f / 180.0f);
}

static void TestSphere(void)
{
    // Upright at the origin, and turned by 0.5 about Z far from it.
    const double here[3] = {0.013, -0.021, 0.007};
    bool upright = Sphere(24, here) && Sphere(1, (const double[3]){0.013, -0.021, 0.0});
    s_origin[0] = 1000.0;
    s_origin[1] = -500.0;
    s_turn = 0.5f;
    const double there[3] = {1000.013, -500.021, 0.007};
    bool turned = Sphere(24, there);
    s_origin[0] = s_origin[1] = 0.0;
    s_turn = 0.0f;
    CHECK(upright && turned, "a lone particle's sphere and circle");
}

static void TestDrops(void)
{
    // Two particles 0.9 apart (beyond each other's reach, 0.75), each a
    // sphere of 0.25: two closed pieces.
    CHECK(Open(32, 32, 0.05f, 0.25f, mvfx_gasSurface), "opened");
    const double places[6] = {-0.45, 0.0, 0.0, 0.45, 0.0, 0.0};
    Spawn(places, 2);
    int64_t vertices = 0;
    int64_t indices = 0;
    Mesh(s_origin, &vertices, &indices);
    int64_t euler = 0;
    bool closed = Closed(vertices, indices, 3, &euler);
    mvfxDestroyContext(s_context);
    CHECK(closed && euler == 4, "two drops, two pieces");
}

// Writes a random field of values within 1 into a surface of n by n by
// depth cells, its border's samples positive, so its mesh closes.
static void RandomField(int32_t n, int32_t depth, uint64_t* state)
{
    mvfxGasState* gas = &mvfxContextInstance(s_context, s_instance)->gases[0];
    float* phi = gas->scalars[0][gas->current];
    for (int64_t i = 0; i < (int64_t)n * n * depth; ++i)
    {
        int32_t at[3] = {(int32_t)(i % n), (int32_t)(i / n % n), (int32_t)(i / n / n)};
        bool border = at[0] == 0 || at[0] == n - 1 || at[1] == 0 || at[1] == n - 1 ||
                      (depth > 1 && (at[2] == 0 || at[2] == depth - 1));
        *state = *state * 6364136223846793005u + 1442695040888963407u;
        float u = (float)(*state >> 40) / 16777216.0f;
        phi[i] = border ? 1.0f : 2.0f * u - 1.0f;
    }
}

static void TestRandomFields(void)
{
    // 40 random fields of 12 cells a side and 40 of 32 by 32 in 2D: every
    // mesh closed.
    uint64_t state = 2026u;
    bool closed = true;
    for (int32_t c = 0; c < 80; ++c)
    {
        int32_t depth = c < 40 ? 12 : 1;
        int32_t n = c < 40 ? 12 : 32;
        CHECK(Open(n, depth, 0.05f, 0.25f, mvfx_gasSurface), "opened");
        RandomField(n, depth, &state);
        int64_t vertices = 0;
        int64_t indices = 0;
        Mesh(s_origin, &vertices, &indices);
        int64_t euler = 0;
        closed = closed && Closed(vertices, indices, depth > 1 ? 3 : 2, &euler) && indices > 0;
        mvfxDestroyContext(s_context);
    }
    CHECK(closed, "random fields' meshes closed");
}

static void TestCapacity(void)
{
    // A sphere into 50 vertices and 60 indices: the counts are all of it,
    // the first triangles that fit are written, and no index reaches a
    // vertex not written. NULL buffers of no size count it too.
    CHECK(Open(24, 24, 0.05f, 0.25f, mvfx_gasSurface), "opened");
    Spawn((const double[3]){0.013, -0.021, 0.007}, 1);
    int64_t vertices = 0;
    int64_t indices = 0;
    Mesh(s_origin, &vertices, &indices);
    static uint32_t full[INDICES];
    memcpy(full, s_indices, sizeof(uint32_t) * (size_t)indices);
    memset(s_indices, 0xFF, sizeof s_indices);
    mvfxSurfaceMeshBuffers b = Buffers();
    b.vertexCapacity = 50;
    b.indexCapacity = 60;
    int64_t v = 0;
    int64_t i = 0;
    bool right = mvfxInstance_WriteSurfaceMesh(s_context, s_instance, 0, s_origin, 0.0f, &b, &v,
                                               &i) == mvfx_success &&
                 v == vertices && i == indices;
    int64_t written = 0;
    while (written < 60 && s_indices[written] != 0xFFFFFFFFu)
    {
        right = right && s_indices[written] == full[written] && s_indices[written] < 50u;
        ++written;
    }
    right = right && written % 3 == 0 && written > 0 &&
            (written == 60 || full[written] >= 50u || full[written + 1] >= 50u ||
             full[written + 2] >= 50u);
    mvfxSurfaceMeshBuffers none = {.scratch = s_scratch, .scratchBytes = (int64_t)sizeof s_scratch};
    right = right &&
            mvfxInstance_WriteSurfaceMesh(s_context, s_instance, 0, s_origin, 0.0f, &none, &v,
                                          &i) == mvfx_success &&
            v == vertices && i == indices;
    mvfxDestroyContext(s_context);
    CHECK(right, "a mesh's capacities");
}

static void TestBits(void)
{
    // Five particles' surface in 3D and in 2D, at level 0.01: the mesh's
    // bytes hash to what is committed, on every platform.
    uint64_t hash = 0u;
    for (int32_t c = 0; c < 2; ++c)
    {
        int32_t depth = c == 0 ? 24 : 1;
        CHECK(Open(24, depth, 0.05f, 0.12f, mvfx_gasSurface), "opened");
        const double places[15] = {0.0, 0.0,  0.0,   0.11, 0.03,  0.0,   -0.1, 0.08,
                                   0.0, 0.05, -0.12, 0.0,  -0.04, -0.05, 0.0};
        double p[15];
        memcpy(p, places, sizeof p);
        for (int32_t k = 0; depth > 1 && k < 5; ++k)
        {
            p[3 * k + 2] = 0.03 * (double)(k - 2);
        }
        Spawn(p, 5);
        mvfxSurfaceMeshBuffers b = Buffers();
        int64_t v = 0;
        int64_t i = 0;
        CHECK(mvfxInstance_WriteSurfaceMesh(s_context, s_instance, 0, s_origin, 0.01f, &b, &v,
                                            &i) == mvfx_success,
              "meshed");
        hash = mvfxHash64(hash, s_positions, (int32_t)(12 * v));
        hash = mvfxHash64(hash, s_normals, (int32_t)(12 * v));
        hash = mvfxHash64(hash, s_indices, (int32_t)(4 * i));
        mvfxDestroyContext(s_context);
    }
    CHECK(hash == 0x49BB75B441899370u, "a mesh's bytes as committed");
}

int main(void)
{
    TestRefusals();
    TestSphere();
    TestDrops();
    TestRandomFields();
    TestCapacity();
    TestBits();
    return s_failures;
}
