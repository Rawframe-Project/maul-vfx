// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Ribbon strips: two vertices a point across its width along its side,
// their corners, the triangles' indices and winding; tessellated points;
// whole ribbons in buffers too small, the counts alone; an emitter not
// drawn as ribbons; scratch too small; the refusals.

#include "test_harness.h"

#include "maul-vfx/output.h"

#include <math.h>
#include <string.h>

typedef struct Scene
{
    mvfxContext* context;
    mvfxInstanceId instance;
    mvfxLayoutId layout;
} Scene;

// A vertex: position, corner, serial.
typedef struct Vertex
{
    float position[3];
    float corner[2];
    uint32_t serial;
} Vertex;

static const mvfxLayoutField s_fields[3] = {
    {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3, .offset = 0},
    {.field = mvfx_recordCorner, .format = mvfx_formatFloat32, .components = 2, .offset = 12},
    {.field = mvfx_recordSerial, .format = mvfx_formatUint32, .components = 1, .offset = 20},
};

static Scene NewScene(const mvfxEmitterDef* emitter)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    Scene scene = {0};
    CHECK(mvfxCreateContext(&contextDef, &scene.context) == mvfx_success, "context");
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(scene.context, &effectDef, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = s_fields;
    layoutDef.fieldCount = 3;
    layoutDef.stride = (uint32_t)sizeof(Vertex);
    CHECK(mvfxCreateLayout(scene.context, &layoutDef, &scene.layout) == mvfx_success, "layout");
    return scene;
}

static mvfxEmitterDef Ribbon(mvfxRibbonIds ids)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 8;
    emitter.hostSpawnCapacity = 8;
    emitter.lifetime = mvfxRangeSource(9.0f, 9.0f);
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.5f, 1, 1, 0}, .max = {0.5f, 1, 1, 0}};
    emitter.ribbon.ids = ids;
    return emitter;
}

static void Spawn(Scene* scene, const double* places, int32_t count)
{
    mvfxSpawnRequest request = {.count = count, .positions = places};
    CHECK(mvfxInstance_Spawn(scene->context, scene->instance, 0, &request) == mvfx_success,
          "spawned");
    CHECK(mvfxContext_StepTicks(scene->context, 1) == mvfx_success, "step");
}

typedef struct Written
{
    Vertex vertices[24];
    uint32_t indices[48];
    int64_t vertexCount;
    int64_t indexCount;
    mvfxResult result;
} Written;

static const mvfxView s_view = {.eye = {0, 0, 10}, .forward = {0, 0, -1}, .up = {0, 1, 0}};

static Written Expand(Scene* scene, int64_t vertices, int64_t indices, int64_t scratchBytes)
{
    static Written written;
    static uint32_t scratch[8 * 4];
    memset(&written, 0xEE, sizeof written);
    mvfxExpandBuffers buffers = {.vertices = written.vertices,
                                 .vertexCapacity = vertices,
                                 .indices = written.indices,
                                 .indexCapacity = indices,
                                 .scratch = scratch,
                                 .scratchBytes = scratchBytes};
    written.result =
        mvfxInstance_ExpandRibbons(scene->context, scene->instance, 0, scene->layout, &s_view,
                                   &buffers, &written.vertexCount, &written.indexCount);
    return written;
}

static bool At(const Vertex* v, float x, float y)
{
    return fabsf(v->position[0] - x) <= 1e-5f && fabsf(v->position[1] - y) <= 1e-5f &&
           v->position[2] == 0.0f;
}

static void TestStrip(void)
{
    // Along x at 0, 1 and 2, half a unit wide, facing an eye above: the
    // side is -y, so corner 0 lies at +y.
    mvfxEmitterDef emitter = Ribbon(mvfx_ribbonOne);
    Scene scene = NewScene(&emitter);
    static const double places[9] = {0, 0, 0, 1, 0, 0, 2, 0, 0};
    Spawn(&scene, places, 3);
    Written w = Expand(&scene, 24, 48, 128);
    CHECK(w.result == mvfx_success && w.vertexCount == 6 && w.indexCount == 12, "a strip");
    const Vertex* v = w.vertices;
    CHECK(At(&v[0], 0, 0.25f) && At(&v[1], 0, -0.25f) && At(&v[4], 2, 0.25f) &&
              At(&v[5], 2, -0.25f),
          "two vertices a point across its width");
    CHECK(v[0].corner[1] == 0.0f && v[1].corner[1] == 1.0f && v[2].corner[0] == 0.5f &&
              v[5].corner[0] == 1.0f && v[3].serial == 1,
          "corners along and across, values");
    static const uint32_t triangles[12] = {0, 1, 2, 2, 1, 3, 2, 3, 4, 4, 3, 5};
    CHECK(memcmp(w.indices, triangles, sizeof triangles) == 0 && w.indices[12] == 0xEEEEEEEEu,
          "two triangles a segment");
    float a[2] = {v[1].position[0] - v[0].position[0], v[1].position[1] - v[0].position[1]};
    float b[2] = {v[2].position[0] - v[0].position[0], v[2].position[1] - v[0].position[1]};
    CHECK(a[0] * b[1] - a[1] * b[0] > 0.0f, "counterclockwise toward the eye");
    w = Expand(&scene, 0, 0, 128);
    CHECK(w.result == mvfx_success && w.vertexCount == 6 && w.indexCount == 12, "the counts alone");
    w = Expand(&scene, 24, 48, 47);
    CHECK(w.result == mvfx_errorCapacity && w.vertexCount == 0, "scratch too small");
    mvfxDestroyContext(scene.context);
}

static void TestTessellatedAndFits(void)
{
    // Two ribbons of two points cut in halves: three points, six vertices
    // and twelve indices each; room for one takes the first.
    mvfxEmitterDef emitter = Ribbon(mvfx_ribbonPerBatch);
    emitter.ribbon.pieces = 2;
    Scene scene = NewScene(&emitter);
    static const double first[6] = {0, 0, 0, 1, 0, 0};
    static const double second[6] = {0, 5, 0, 1, 5, 0};
    Spawn(&scene, first, 2);
    Spawn(&scene, second, 2);
    Written w = Expand(&scene, 24, 48, 128);
    CHECK(w.vertexCount == 12 && w.indexCount == 24 && At(&w.vertices[2], 0.5f, 0.25f) &&
              w.indices[12] == 6 && w.indices[23] == 11,
          "tessellated, the second's indices after the first's");
    w = Expand(&scene, 11, 48, 128);
    CHECK(w.vertexCount == 12 && w.indexCount == 24 && w.vertices[5].serial == 1 &&
              w.vertices[6].serial == 0xEEEEEEEEu && w.indices[12] == 0xEEEEEEEEu,
          "the vertices' room");
    w = Expand(&scene, 24, 23, 128);
    CHECK(w.vertices[6].serial == 0xEEEEEEEEu && w.indices[11] == 5, "the indices' room");
    mvfxDestroyContext(scene.context);
    // Not drawn as ribbons: nothing, and no scratch needed.
    emitter = Ribbon(mvfx_ribbonNone);
    scene = NewScene(&emitter);
    Spawn(&scene, first, 2);
    w = Expand(&scene, 24, 48, 0);
    CHECK(w.result == mvfx_success && w.vertexCount == 0 && w.indexCount == 0, "no ribbons");
    mvfxDestroyContext(scene.context);
}

static void TestRefusals(void)
{
    mvfxEmitterDef emitter = Ribbon(mvfx_ribbonOne);
    Scene scene = NewScene(&emitter);
    mvfxContext* c = scene.context;
    mvfxInstanceId i = scene.instance;
    mvfxLayoutId l = scene.layout;
    static Vertex vertices[4];
    static uint32_t indices[6];
    static uint32_t scratch[32];
    mvfxExpandBuffers good = {.vertices = vertices,
                              .vertexCapacity = 4,
                              .indices = indices,
                              .indexCapacity = 6,
                              .scratch = scratch,
                              .scratchBytes = 128};
    mvfxExpandBuffers bad[6] = {good, good, good, good, good, good};
    bad[0].vertices = NULL;
    bad[1].vertexCapacity = -1;
    bad[2].indices = NULL;
    bad[3].indexCapacity = -1;
    bad[4].indices = (uint32_t*)((unsigned char*)indices + 2);
    bad[5].scratch = (unsigned char*)scratch + 1;
    mvfxView far = s_view;
    far.eye[0] = 0x1p53;
    int64_t vertexCount = 0;
    int64_t indexCount = 0;
    uint64_t misuse = mvfxContext_GetMisuse(c);
    int32_t refused = 0;
    for (int32_t b = 0; b < 6; ++b)
    {
        refused += mvfxInstance_ExpandRibbons(c, i, 0, l, &s_view, &bad[b], &vertexCount,
                                              &indexCount) == mvfx_errorInvalid;
    }
    refused += mvfxInstance_ExpandRibbons(c, i, 0, l, &far, &good, &vertexCount, &indexCount) ==
               mvfx_errorInvalid;
    refused += mvfxInstance_ExpandRibbons(c, i, 0, l, NULL, &good, &vertexCount, &indexCount) ==
               mvfx_errorInvalid;
    refused += mvfxInstance_ExpandRibbons(c, i, 0, l, &s_view, NULL, &vertexCount, &indexCount) ==
               mvfx_errorInvalid;
    refused += mvfxInstance_ExpandRibbons(c, i, 0, l, &s_view, &good, NULL, &indexCount) ==
               mvfx_errorInvalid;
    refused += mvfxInstance_ExpandRibbons(c, i, 0, l, &s_view, &good, &vertexCount, NULL) ==
               mvfx_errorInvalid;
    refused += mvfxInstance_ExpandRibbons(c, i, 1, l, &s_view, &good, &vertexCount, &indexCount) ==
               mvfx_errorInvalid;
    CHECK(refused == 12 && mvfxContext_GetMisuse(c) == misuse + 12, "refused, each counted");
    mvfxInstanceId stale = i;
    stale.generation += 1;
    mvfxLayoutId staleLayout = l;
    staleLayout.generation += 1;
    CHECK(mvfxInstance_ExpandRibbons(c, stale, 0, l, &s_view, &good, &vertexCount, &indexCount) ==
                  mvfx_errorStale &&
              mvfxInstance_ExpandRibbons(c, i, 0, staleLayout, &s_view, &good, &vertexCount,
                                         &indexCount) == mvfx_errorStale &&
              mvfxInstance_ExpandRibbons(NULL, i, 0, l, &s_view, &good, &vertexCount,
                                         &indexCount) == mvfx_errorInvalid,
          "stale, no context");
    mvfxDestroyContext(c);
}

int main(void)
{
    TestStrip();
    TestTessellatedAndFits();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
