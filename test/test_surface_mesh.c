// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A surface's mesh by marching cubes (and squares): the refusals (a
// planned instance's among them) with the outputs cleared, and the
// scratch's size; a lone particle's sphere closed, its Euler
// characteristic 2, its vertices on the sphere and its normals radial, in
// a turned instance far from the origin; two drops, characteristic 4;
// random fields of many ambiguous faces closed without a crack; a circle
// and random 2D fields closed loops; the capacities' partial writes and
// true counts; the same bytes as committed.

#include "test_harness.h"
#include "test_hooks.h"

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

// The next instance's origin and turn about Z (radians), and whether it
// is planned.
static double s_origin[3];
static float s_turn;
static bool s_planned;

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
    if (mvfxCreateEffect(s_context, &def, &effect, NULL) != mvfx_success)
    {
        mvfxDestroyContext(s_context);
        return false;
    }
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    memcpy(instanceDef.origin, s_origin, sizeof s_origin);
    instanceDef.rotation[2] = sinf(0.5f * s_turn);
    instanceDef.rotation[3] = cosf(0.5f * s_turn);
    instanceDef.planned = s_planned;
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
// finite, NULL buffers, positions misaligned by 2 bytes, a NULL count.
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
    case 9:
        b.positions = (float*)((char*)s_positions + 2);
        break;
    default:
        count = NULL;
        break;
    }
    return mvfxInstance_WriteSurfaceMesh(s_context, s_instance, 0, origin, level, buffers, count,
                                         &i);
}

// Whether every byte of a buffer is value.
static bool Filled(const void* buffer, size_t size, unsigned char value)
{
    const unsigned char* bytes = buffer;
    for (size_t i = 0; i < size; ++i)
    {
        if (bytes[i] != value)
        {
            return false;
        }
    }
    return true;
}

static void TestRefusals(void)
{
    // A smoke gas, its outputs cleared; each spoiled call (scratch too
    // small a capacity error, the others invalid); then a valid call over
    // nothing. The scratch a 3D and a 2D surface take. A planned
    // instance's surface: its scratch, its mesh refused, the counts
    // cleared.
    CHECK(Open(8, 8, 0.1f, 0.1f, mvfx_gasSmoke), "opened");
    int64_t bytes = 5;
    mvfxSurfaceMeshBuffers b = Buffers();
    int64_t v = -1;
    int64_t i = -1;
    const double zero[3] = {0.0, 0.0, 0.0};
    bool right = mvfxInstance_GetSurfaceMeshScratchBytes(s_context, s_instance, 0, &bytes) ==
                     mvfx_errorInvalid &&
                 mvfxInstance_WriteSurfaceMesh(s_context, s_instance, 0, zero, 0.0f, &b, &v, &i) ==
                     mvfx_errorInvalid &&
                 bytes == 0 && v == 0 && i == 0;
    memset(s_positions, 0xEE, sizeof s_positions);
    memset(s_normals, 0xEE, sizeof s_normals);
    memset(s_indices, 0xEE, sizeof s_indices);
    right = right &&
            mvfxInstance_WriteSurfaceMesh(s_context, s_instance, 0, zero, 0.0f, &b, &v, &i) ==
                mvfx_errorInvalid &&
            Filled(s_positions, sizeof s_positions, 0) && Filled(s_normals, sizeof s_normals, 0) &&
            Filled(s_indices, sizeof s_indices, 0);
    mvfxDestroyContext(s_context);
    CHECK(Open(8, 8, 0.1f, 0.1f, mvfx_gasSurface), "opened");
    right =
        right &&
        mvfxInstance_GetSurfaceMeshScratchBytes(s_context, s_instance, 0, &bytes) == mvfx_success &&
        bytes == 32 * 64;
    for (int32_t c = 0; c < 11; ++c)
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
        bytes == 24 * 8;
    mvfxDestroyContext(s_context);
    s_planned = true;
    CHECK(Open(8, 8, 0.1f, 0.1f, mvfx_gasSurface), "opened");
    s_planned = false;
    v = -1;
    i = -1;
    right =
        right &&
        mvfxInstance_GetSurfaceMeshScratchBytes(s_context, s_instance, 0, &bytes) == mvfx_success &&
        bytes == 32 * 64 &&
        mvfxInstance_WriteSurfaceMesh(s_context, s_instance, 0, zero, 0.0f, &b, &v, &i) ==
            mvfx_errorUnsupported &&
        v == 0 && i == 0;
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

static void TestEdgeNormals(void)
{
    // A sphere of 0.25 crossing the box's -X face (its center at -0.45,
    // the samples' first plane at -0.575): its normals on that plane, from
    // one-sided differences, stay within 8 degrees of radial (4.1
    // measured; differences over the wrong span give 16).
    CHECK(Open(24, 24, 0.05f, 0.25f, mvfx_gasSurface), "opened");
    const double center[3] = {-0.45, 0.013, 0.007};
    Spawn(center, 1);
    int64_t vertices = 0;
    int64_t indices = 0;
    Mesh(s_origin, &vertices, &indices);
    float cosine = 1.0f;
    int32_t onface = 0;
    for (int64_t v = 0; v < vertices; ++v)
    {
        double d[3];
        double r2 = 0.0;
        for (int32_t k = 0; k < 3; ++k)
        {
            d[k] = (double)s_positions[3 * v + k] - center[k];
            r2 += d[k] * d[k];
        }
        double along = 0.0;
        for (int32_t k = 0; k < 3; ++k)
        {
            along += (double)s_normals[3 * v + k] * d[k] / sqrt(r2);
        }
        if (fabsf(s_positions[3 * v] + 0.575f) < 1e-4f)
        {
            cosine = fminf(cosine, (float)along);
            ++onface;
        }
    }
    mvfxDestroyContext(s_context);
    CHECK(onface > 20 && cosine >= cosf(8.0f * 3.14159265f / 180.0f), "normals at the box's face");
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

// A surface's distance field as a test writes it, at most 32 by 32 by 32
// cells.
static float s_field[32 * 32 * 32];

// Writes the first count values of s_field into the surface's field.
static void WriteField(int64_t count)
{
    CHECK(mvfxTestWriteGasScalar(s_context, s_instance, 0, mvfx_gasDistance, s_field, count),
          "the field written");
}

// Writes a random field of values within 1 into a surface of n by n by
// depth cells, its border's samples positive, so its mesh closes.
static void RandomField(int32_t n, int32_t depth, uint64_t* state)
{
    float* phi = s_field;
    for (int64_t i = 0; i < (int64_t)n * n * depth; ++i)
    {
        int32_t at[3] = {(int32_t)(i % n), (int32_t)(i / n % n), (int32_t)(i / n / n)};
        bool border = at[0] == 0 || at[0] == n - 1 || at[1] == 0 || at[1] == n - 1 ||
                      (depth > 1 && (at[2] == 0 || at[2] == depth - 1));
        *state = *state * 6364136223846793005u + 1442695040888963407u;
        float u = (float)(*state >> 40) / 16777216.0f;
        phi[i] = border ? 1.0f : 2.0f * u - 1.0f;
    }
    WriteField((int64_t)n * n * depth);
}

// Whether a mesh's primitives of count vertices close, at samples where
// sheets (curves) meet too: each directed edge (a, b) as often as (b, a)
// for triangles, each vertex as many segments' end as start for segments;
// and none of zero length.
static bool Balanced(int64_t vertices, int64_t indices, int32_t count)
{
    int64_t edges = count == 3 ? indices : indices / 2;
    uint64_t* keys = malloc(sizeof(uint64_t) * (size_t)(2 * edges + 1));
    int64_t* degrees = calloc((size_t)(vertices + 1), sizeof(int64_t));
    if (keys == NULL || degrees == NULL)
    {
        free(keys);
        free(degrees);
        return false;
    }
    bool sized = true;
    for (int64_t e = 0; e < edges; ++e)
    {
        int64_t first = count == 3 ? e : 2 * e;
        int64_t second = count == 3 ? (e % 3 == 2 ? e - 2 : e + 1) : 2 * e + 1;
        uint64_t a = s_indices[first];
        uint64_t b = s_indices[second];
        keys[e] = a << 32 | b;
        keys[edges + e] = b << 32 | a;
        degrees[a] += 1;
        degrees[b] -= 1;
        const float* p = &s_positions[3 * a];
        const float* q = &s_positions[3 * b];
        sized = sized && (p[0] != q[0] || p[1] != q[1] || p[2] != q[2]);
    }
    qsort(keys, (size_t)edges, sizeof(uint64_t), Compare);
    qsort(keys + edges, (size_t)edges, sizeof(uint64_t), Compare);
    bool closed = count == 2 || memcmp(keys, keys + edges, sizeof(uint64_t) * (size_t)edges) == 0;
    for (int64_t v = 0; v < vertices; ++v)
    {
        closed = closed && degrees[v] == 0;
    }
    free(keys);
    free(degrees);
    return closed && sized;
}

// What a tie field's vertices lie on: on a sample, its count and how many
// have a unit normal; and whether each lies at the level 0: a sample's
// value 0 and its normal unit or 0, an edge's line through its two
// samples 0 there (a tunnel's center, on neither, passes).
typedef struct OnSamples
{
    int64_t count;
    int64_t unit;
} OnSamples;

// Vertex v of a mesh over size samples of 0.05 in samples: the sample at
// or below it along each axis, the axis it lies between samples along and
// its share of the way there; how many axes it lies between samples on.
static int32_t Between(int64_t v, const int32_t size[3], int32_t lo[3], int32_t* axis, float* share)
{
    int32_t off = 0;
    for (int32_t k = 0; k < 3; ++k)
    {
        float g = s_positions[3 * v + k] / 0.05f + 0.5f * (float)(size[k] - 1);
        bool whole = fabsf(g - roundf(g)) < 1e-3f;
        lo[k] = (int32_t)(whole ? roundf(g) : floorf(g));
        *share = whole ? *share : g - floorf(g);
        *axis = whole ? *axis : k;
        off += whole ? 0 : 1;
    }
    return off;
}

static bool AtLevel(const float* phi, int32_t n, int32_t depth, int64_t vertices, OnSamples* on)
{
    const int32_t size[3] = {n, n, depth};
    bool right = true;
    for (int64_t v = 0; v < vertices; ++v)
    {
        int32_t lo[3];
        float share = 0.0f;
        int32_t axis = -1;
        int32_t off = Between(v, size, lo, &axis, &share);
        int64_t at = ((int64_t)lo[2] * n + lo[1]) * n + lo[0];
        const float* normal = &s_normals[3 * v];
        float length = normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2];
        if (off == 0)
        {
            on->count += 1;
            on->unit += fabsf(length - 1.0f) <= 1e-5f ? 1 : 0;
            right = right && phi[at] == 0.0f && (length == 0.0f || fabsf(length - 1.0f) <= 1e-5f);
        }
        if (off == 1)
        {
            int64_t step = axis == 0 ? 1 : (axis == 1 ? n : (int64_t)n * n);
            float value = phi[at] + share * (phi[at + step] - phi[at]);
            right = right && fabsf(value) <= 1e-4f;
        }
    }
    return right;
}

// Writes a field of -1, 0 and 1 at random into a surface of n by n by
// depth cells, its border 1; the field.
static const float* TieField(int32_t n, int32_t depth, uint64_t* state)
{
    float* phi = s_field;
    for (int64_t i = 0; i < (int64_t)n * n * depth; ++i)
    {
        int32_t at[3] = {(int32_t)(i % n), (int32_t)(i / n % n), (int32_t)(i / n / n)};
        bool border = at[0] == 0 || at[0] == n - 1 || at[1] == 0 || at[1] == n - 1 ||
                      (depth > 1 && (at[2] == 0 || at[2] == depth - 1));
        *state = *state * 6364136223846793005u + 1442695040888963407u;
        phi[i] = border ? 1.0f : (float)(int32_t)((*state >> 33) % 3u) - 1.0f;
    }
    WriteField((int64_t)n * n * depth);
    return phi;
}

static void TestTies(void)
{
    // Fields of -1, 0 and 1 at random, the border 1, at the level 0: ties
    // everywhere. Samples at the level are outside, edges to them take
    // their own vertex, primitives on them alone are left out: every mesh
    // closes, none of its edges of zero length, every vertex at the level.
    uint64_t state = 7u;
    bool right = true;
    int64_t total = 0;
    OnSamples on = {0};
    for (int32_t c = 0; c < 40; ++c)
    {
        int32_t depth = c < 20 ? 10 : 1;
        int32_t n = c < 20 ? 10 : 24;
        CHECK(Open(n, depth, 0.05f, 0.25f, mvfx_gasSurface), "opened");
        const float* phi = TieField(n, depth, &state);
        int64_t vertices = 0;
        int64_t indices = 0;
        Mesh(s_origin, &vertices, &indices);
        right = right && Balanced(vertices, indices, depth > 1 ? 3 : 2) &&
                AtLevel(phi, n, depth, vertices, &on);
        total += indices;
        mvfxDestroyContext(s_context);
    }
    CHECK(right && total > 0, "ties: closed, no edge of zero length");
    CHECK(on.count > 0 && on.unit > 0, "ties: vertices on samples at the level");
}

static void TestRandomFields(void)
{
    // 40 random fields of 12 cells a side and 40 of 32 by 32 in 2D: every
    // mesh closed, its vertices within the samples' span, its normals unit
    // (or 0 where the field is flat).
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
        // Every vertex within the samples' span, its normal unit or 0.
        float span = 0.5f * (float)(n - 1) * 0.05f + 1e-5f;
        for (int64_t v = 0; v < vertices; ++v)
        {
            float length = 0.0f;
            for (int32_t k = 0; k < 3; ++k)
            {
                closed = closed && fabsf(s_positions[3 * v + k]) <= span;
                length += s_normals[3 * v + k] * s_normals[3 * v + k];
            }
            closed = closed && (length == 0.0f || fabsf(length - 1.0f) <= 1e-5f);
        }
        mvfxDestroyContext(s_context);
    }
    CHECK(closed, "random fields' meshes closed");
}

// Whether a mesh written into vertexCapacity vertices and indexCapacity
// indices counts all of it and writes the first triangles of full that
// fit both, and nothing after them.
static bool WritesWithin(const uint32_t* full, int64_t vertices, int64_t indices,
                         int64_t vertexCapacity, int64_t indexCapacity)
{
    memset(s_indices, 0xFF, sizeof s_indices);
    mvfxSurfaceMeshBuffers b = Buffers();
    b.vertexCapacity = vertexCapacity;
    b.indexCapacity = indexCapacity;
    int64_t v = 0;
    int64_t i = 0;
    bool right = mvfxInstance_WriteSurfaceMesh(s_context, s_instance, 0, s_origin, 0.0f, &b, &v,
                                               &i) == mvfx_success &&
                 v == vertices && i == indices;
    int64_t fit = 0;
    while (fit + 3 <= indexCapacity && fit < indices && full[fit] < vertexCapacity &&
           full[fit + 1] < vertexCapacity && full[fit + 2] < vertexCapacity)
    {
        fit += 3;
    }
    for (int64_t k = 0; k < indexCapacity; ++k)
    {
        right = right && s_indices[k] == (k < fit ? full[k] : 0xFFFFFFFFu);
    }
    return right && fit > 0;
}

static void TestCapacity(void)
{
    // A sphere into 50 vertices and 60 indices; into exactly the vertices
    // before the first triangle that needs one more; into 61 indices: the
    // counts are all of it and the first triangles that fit are written,
    // nothing after. NULL buffers of no size count it too.
    CHECK(Open(24, 24, 0.05f, 0.25f, mvfx_gasSurface), "opened");
    Spawn((const double[3]){0.013, -0.021, 0.007}, 1);
    int64_t vertices = 0;
    int64_t indices = 0;
    Mesh(s_origin, &vertices, &indices);
    static uint32_t full[INDICES];
    memcpy(full, s_indices, sizeof(uint32_t) * (size_t)indices);
    // The first triangle reaching vertex 50 or more: its largest vertex.
    int64_t first = 0;
    while (first < indices && full[first] < 50u && full[first + 1] < 50u && full[first + 2] < 50u)
    {
        first += 3;
    }
    uint32_t exact = full[first] > full[first + 1] ? full[first] : full[first + 1];
    exact = exact > full[first + 2] ? exact : full[first + 2];
    bool right = WritesWithin(full, vertices, indices, 50, 60) &&
                 WritesWithin(full, vertices, indices, exact, 3000) &&
                 WritesWithin(full, vertices, indices, VERTICES, 61);
    mvfxSurfaceMeshBuffers none = {.scratch = s_scratch, .scratchBytes = (int64_t)sizeof s_scratch};
    int64_t v = 0;
    int64_t i = 0;
    right = right &&
            mvfxInstance_WriteSurfaceMesh(s_context, s_instance, 0, s_origin, 0.0f, &none, &v,
                                          &i) == mvfx_success &&
            v == vertices && i == indices;
    mvfxDestroyContext(s_context);
    CHECK(right, "a mesh's capacities");
}

// Sets a surface's field of n by n by depth cells to 1 everywhere, then
// count values at places (x, y, z, value).
static void SetField(int32_t n, int32_t depth, const float (*set)[4], int32_t count)
{
    float* phi = s_field;
    for (int64_t i = 0; i < (int64_t)n * n * depth; ++i)
    {
        phi[i] = 1.0f;
    }
    for (int32_t c = 0; c < count; ++c)
    {
        int64_t at = ((int64_t)set[c][2] * n + (int64_t)set[c][1]) * n + (int64_t)set[c][0];
        phi[at] = set[c][3];
    }
    WriteField((int64_t)n * n * depth);
}

// The loops of a closed curve: each segment's end is the next's start.
static int32_t Loops(int64_t vertices, int64_t indices)
{
    int32_t* next = malloc(sizeof(int32_t) * (size_t)(vertices > 0 ? vertices : 1));
    bool* seen = calloc((size_t)(vertices > 0 ? vertices : 1), sizeof(bool));
    if (next == NULL || seen == NULL)
    {
        free(next);
        free(seen);
        return -1;
    }
    for (int64_t v = 0; v < vertices; ++v)
    {
        next[v] = (int32_t)v;
    }
    for (int64_t s = 0; s + 1 < indices; s += 2)
    {
        next[s_indices[s]] = (int32_t)s_indices[s + 1];
    }
    int32_t loops = 0;
    for (int64_t v = 0; v < vertices; ++v)
    {
        loops += seen[v] ? 0 : 1;
        for (int64_t w = v; !seen[w]; w = next[w])
        {
            seen[w] = true;
        }
    }
    free(next);
    free(seen);
    return loops;
}

// The pieces a diagonal pair of samples of value a makes with the other
// pair of b (on a face of a cube in 3D, a square in 2D), all else 1: loops
// in 2D, and in 3D half the Euler characteristic.
static int32_t Pieces(int32_t depth, bool diagonal, float a, float b)
{
    CHECK(Open(6, depth, 0.05f, 0.25f, mvfx_gasSurface), "opened");
    int32_t z = depth > 1 ? 2 : 0;
    const float set[4][4] = {{2, 2, (float)z, diagonal ? a : b},
                             {3, 3, (float)z, diagonal ? a : b},
                             {3, 2, (float)z, diagonal ? b : a},
                             {2, 3, (float)z, diagonal ? b : a}};
    SetField(6, depth, set, 4);
    int64_t vertices = 0;
    int64_t indices = 0;
    Mesh(s_origin, &vertices, &indices);
    int64_t euler = 0;
    int32_t pieces = depth > 1
                         ? (ClosedSurface(vertices, indices, &euler) ? (int32_t)euler / 2 : -1)
                         : Loops(vertices, indices);
    mvfxDestroyContext(s_context);
    return pieces;
}

static void TestDecider(void)
{
    // Inside samples (-2) diagonal across a face, the others 1: the
    // saddle is inside, one piece; inside -0.5 and outside 2: the saddle
    // is outside, two; -1 and 1 tie: two. Both diagonals, in 2D and 3D.
    bool right = true;
    for (int32_t c = 0; c < 4; ++c)
    {
        int32_t depth = c < 2 ? 1 : 6;
        bool diagonal = c % 2 == 0;
        right = right && Pieces(depth, diagonal, -2.0f, 1.0f) == 1 &&
                Pieces(depth, diagonal, -0.5f, 2.0f) == 2 &&
                Pieces(depth, diagonal, -1.0f, 1.0f) == 2;
    }
    CHECK(right, "ambiguous faces by their saddle");
}

static void TestAtLevel(void)
{
    // A sample exactly at the level is outside: one inside sample beside
    // it makes 4 vertices in 2D and 6 in 3D, one at the sample itself.
    bool right = true;
    for (int32_t c = 0; c < 2; ++c)
    {
        int32_t depth = c == 0 ? 1 : 5;
        float z = c == 0 ? 0.0f : 2.0f;
        CHECK(Open(5, depth, 0.05f, 0.25f, mvfx_gasSurface), "opened");
        const float set[2][4] = {{2, 2, z, -1.0f}, {3, 2, z, 0.0f}};
        SetField(5, depth, set, 2);
        int64_t vertices = 0;
        int64_t indices = 0;
        Mesh(s_origin, &vertices, &indices);
        mvfxDestroyContext(s_context);
        right = right && vertices == (c == 0 ? 4 : 6);
    }
    CHECK(right, "a sample at the level is outside");
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
    CHECK(hash == 0x5FC7FA883FE07A31u, "a mesh's bytes as committed");
}

int main(void)
{
    TestRefusals();
    TestSphere();
    TestEdgeNormals();
    TestDrops();
    TestRandomFields();
    TestTies();
    TestCapacity();
    TestDecider();
    TestAtLevel();
    TestBits();
    return s_failures;
}
