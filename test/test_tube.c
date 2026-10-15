// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Ribbons as tubes: a straight trail's rings at half its width across its
// tangent, closing their seams, their normals outward and their corners
// around, its quads counterclockwise from outside; a trail curved in a
// tilted plane keeping each ring's normal in the plane (rotation-minimizing
// frames, no twist); caps facing back and on at the ends; whole ribbons
// when the buffers are short; a chain drawn round; misuse.

#include "test_harness.h"

#include "maul-vfx/output.h"

#include <math.h>
#include <string.h>

#define MOST 1024

typedef struct Vertex
{
    float position[3];
    float normal[3];
    float corner[2];
    float tangent[3];
} Vertex;

static const mvfxLayoutField s_fields[4] = {
    {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3, .offset = 0},
    {.field = mvfx_recordFaceNormal, .format = mvfx_formatFloat32, .components = 3, .offset = 12},
    {.field = mvfx_recordCorner, .format = mvfx_formatFloat32, .components = 2, .offset = 24},
    {.field = mvfx_recordTangent, .format = mvfx_formatFloat32, .components = 3, .offset = 32},
};

static const mvfxView s_view = {.forward = {0, 0, -1}, .up = {0, 1, 0}};

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
    mvfxLayoutId layout;
    Vertex vertices[MOST];
    uint32_t indices[4 * MOST];
    uint32_t scratch[4 * MOST];
    int64_t vertexCount;
    int64_t indexCount;
} Scene;

static Scene s_scene;

// A trail of particles the host places, still, one ribbon.
static mvfxEmitterDef Trail(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 64;
    emitter.hostSpawnCapacity = 64;
    emitter.lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    emitter.ribbon.ids = mvfx_ribbonOne;
    return emitter;
}

static void Open(const mvfxEmitterDef* emitter, const double* places, int32_t count)
{
    memset(&s_scene, 0, sizeof s_scene);
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_scene.context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_scene.context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_scene.context, &instanceDef, &s_scene.instance) == mvfx_success,
          "instance");
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = s_fields;
    layoutDef.fieldCount = 4;
    layoutDef.stride = (uint32_t)sizeof(Vertex);
    CHECK(mvfxCreateLayout(s_scene.context, &layoutDef, &s_scene.layout) == mvfx_success, "layout");
    static const float still[3 * 64] = {0};
    mvfxSpawnRequest request = {.count = count, .positions = places, .velocities = still};
    CHECK(mvfxInstance_Spawn(s_scene.context, s_scene.instance, 0, &request) == mvfx_success,
          "spawned");
    CHECK(mvfxContext_StepTicks(s_scene.context, 1) == mvfx_success, "step");
}

static mvfxResult Expand(int32_t sides, bool caps, int64_t vertexCapacity, int64_t indexCapacity)
{
    const mvfxTubeDef tube = {.sides = sides, .caps = caps};
    mvfxExpandBuffers buffers = {.vertices = s_scene.vertices,
                                 .vertexCapacity = vertexCapacity,
                                 .indices = s_scene.indices,
                                 .indexCapacity = indexCapacity,
                                 .scratch = s_scene.scratch,
                                 .scratchBytes = sizeof s_scene.scratch};
    return mvfxInstance_ExpandTubes(s_scene.context, s_scene.instance, 0, s_scene.layout, &s_view,
                                    &tube, &buffers, &s_scene.vertexCount, &s_scene.indexCount);
}

static float Dot(const float a[3], const float b[3])
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// Ring r's center (the mean of its first sides vertices).
static void CenterOf(int32_t sides, int32_t r, float out[3])
{
    for (int32_t k = 0; k < 3; ++k)
    {
        out[k] = 0.0f;
        for (int32_t v = 0; v < sides; ++v)
        {
            out[k] += s_scene.vertices[r * (sides + 1) + v].position[k] / (float)sides;
        }
    }
}

// Whether every triangle of indices from first, count of them, is
// counterclockwise seen from its first vertex's normal side.
static bool Wound(int64_t first, int64_t count)
{
    for (int64_t t = first; t < first + count; t += 3)
    {
        const Vertex* a = &s_scene.vertices[s_scene.indices[t]];
        const Vertex* b = &s_scene.vertices[s_scene.indices[t + 1]];
        const Vertex* c = &s_scene.vertices[s_scene.indices[t + 2]];
        float e[3];
        float f[3];
        for (int32_t k = 0; k < 3; ++k)
        {
            e[k] = b->position[k] - a->position[k];
            f[k] = c->position[k] - a->position[k];
        }
        float n[3] = {e[1] * f[2] - e[2] * f[1], e[2] * f[0] - e[0] * f[2],
                      e[0] * f[1] - e[1] * f[0]};
        if (!(Dot(n, a->normal) > 0.0f))
        {
            return false;
        }
    }
    return true;
}

static void TestStraight(void)
{
    // Four points along x, 1 apart, of size 1: rings of radius 0.5 across
    // x, the first vertex along y (the axis least along x, first of the
    // two), the seam closed.
    static const double places[12] = {0, 0, 0, 1, 0, 0, 2, 0, 0, 3, 0, 0};
    mvfxEmitterDef emitter = Trail();
    Open(&emitter, places, 4);
    CHECK(Expand(6, false, MOST, 4 * MOST) == mvfx_success && s_scene.vertexCount == 28 &&
              s_scene.indexCount == 3 * 6 * 6,
          "the counts");
    bool rings = true;
    for (int32_t r = 0; r < 4; ++r)
    {
        float center[3];
        CenterOf(6, r, center);
        rings = rings && fabsf(center[0] - (float)r) <= 1e-5f && fabsf(center[1]) <= 1e-5f;
        for (int32_t v = 0; v <= 6; ++v)
        {
            const Vertex* x = &s_scene.vertices[r * 7 + v];
            float out[3] = {x->position[0] - center[0], x->position[1] - center[1],
                            x->position[2] - center[2]};
            rings = rings && fabsf(sqrtf(Dot(out, out)) - 0.5f) <= 1e-5f &&
                    fabsf(out[0]) <= 1e-5f && fabsf(Dot(out, x->normal) - 0.5f) <= 1e-5f &&
                    fabsf(x->corner[1] - (float)v / 6.0f) <= 1e-6f;
        }
        const Vertex* first = &s_scene.vertices[r * 7];
        const Vertex* seam = &s_scene.vertices[r * 7 + 6];
        rings = rings && fabsf(first->normal[1] - 1.0f) <= 1e-6f &&
                fabsf(seam->position[1] - first->position[1]) <= 1e-6f &&
                fabsf(seam->position[2] - first->position[2]) <= 1e-6f;
    }
    CHECK(rings, "rings");
    CHECK(Wound(0, s_scene.indexCount), "counterclockwise from outside");
    // Caps: two more of eight vertices and 18 indices, their centers at
    // the ends, facing -x and +x.
    CHECK(Expand(6, true, MOST, 4 * MOST) == mvfx_success && s_scene.vertexCount == 28 + 16 &&
              s_scene.indexCount == 3 * 6 * 6 + 36,
          "caps' counts");
    const Vertex* start = &s_scene.vertices[28];
    const Vertex* end = &s_scene.vertices[36];
    CHECK(fabsf(start->position[0]) <= 1e-6f && fabsf(start->normal[0] + 1.0f) <= 1e-6f &&
              fabsf(end->position[0] - 3.0f) <= 1e-6f && fabsf(end->normal[0] - 1.0f) <= 1e-6f &&
              fabsf(start->corner[0] - 0.5f) <= 1e-6f,
          "caps");
    CHECK(Wound(3 * 6 * 6, 36), "caps facing out");
    // Room for a ring short: nothing written, the counts all of it.
    s_scene.vertices[0].position[0] = 99.0f;
    CHECK(Expand(6, false, 27, 4 * MOST) == mvfx_success && s_scene.vertexCount == 28 &&
              s_scene.vertices[0].position[0] == 99.0f,
          "whole ribbons only");
    mvfxDestroyContext(s_scene.context);
}

static void TestCurved(void)
{
    // A half circle of radius 2 in the plane through z and (1, 1, 0): each
    // ring's first vertex's normal stays in the plane, across the
    // plane's normal (1, -1, 0) / sqrt 2, while the axis least along the
    // tangent changes.
    double places[3 * 13];
    for (int32_t i = 0; i < 13; ++i)
    {
        double a = 3.14159265358979 * (double)i / 12.0;
        double u = 2.0 * sin(a);
        double w = 2.0 - 2.0 * cos(a);
        places[3 * i] = u / sqrt(2.0);
        places[3 * i + 1] = u / sqrt(2.0);
        places[3 * i + 2] = w;
    }
    mvfxEmitterDef emitter = Trail();
    Open(&emitter, places, 13);
    CHECK(Expand(8, false, MOST, 4 * MOST) == mvfx_success && s_scene.vertexCount == 13 * 9,
          "counted");
    const float plane[3] = {0.70710678f, -0.70710678f, 0.0f};
    bool flat = true;
    for (int32_t r = 0; r < 13; ++r)
    {
        const Vertex* first = &s_scene.vertices[r * 9];
        flat = flat && fabsf(Dot(first->normal, plane)) <= 1e-3f &&
               fabsf(Dot(first->normal, first->tangent)) <= 1e-4f;
    }
    CHECK(flat, "no twist");
    CHECK(Wound(0, s_scene.indexCount), "wound");
    mvfxDestroyContext(s_scene.context);
}

static void TestChainAndMisuse(void)
{
    // A chain of five drawn round: five rings.
    mvfxEmitterDef emitter = Trail();
    emitter.capacity = 5;
    emitter.hostSpawnCapacity = 1;
    emitter.unit = (mvfxUnitDef){.kind = mvfx_unitChain,
                                 .points = 5,
                                 .spacing = 0.5f,
                                 .direction = {0, -1, 0},
                                 .substeps = 2,
                                 .pinFirst = true};
    static const double root[3] = {0, 0, 0};
    Open(&emitter, root, 1);
    CHECK(Expand(4, false, MOST, 4 * MOST) == mvfx_success && s_scene.vertexCount == 25 &&
              s_scene.indexCount == 4 * 4 * 6,
          "a chain round");
    const mvfxTubeDef few = {.sides = 2};
    const mvfxTubeDef many = {.sides = MVFX_MAX_TUBE_SIDES + 1};
    const mvfxTubeDef none = {.sides = 0};
    mvfxExpandBuffers buffers = {.vertices = s_scene.vertices,
                                 .vertexCapacity = MOST,
                                 .indices = s_scene.indices,
                                 .indexCapacity = MOST,
                                 .scratch = s_scene.scratch,
                                 .scratchBytes = sizeof s_scene.scratch};
    int64_t v = 0;
    int64_t i = 0;
    int32_t refused = 0;
    const mvfxTubeDef* tubes[4] = {&few, &many, &none, NULL};
    for (int32_t t = 0; t < 4; ++t)
    {
        refused +=
            mvfxInstance_ExpandTubes(s_scene.context, s_scene.instance, 0, s_scene.layout, &s_view,
                                     tubes[t], &buffers, &v, &i) == mvfx_errorInvalid;
    }
    CHECK(refused == 4, "misuse");
    CHECK(Expand(MVFX_MAX_TUBE_SIDES, true, MOST, 4 * MOST) == mvfx_success &&
              s_scene.vertexCount == 5 * 65 + 2 * 66,
          "the most sides");
    mvfxDestroyContext(s_scene.context);
}

int main(void)
{
    TestStraight();
    TestCurved();
    TestChainAndMisuse();
    return s_failures == 0 ? 0 : 1;
}
