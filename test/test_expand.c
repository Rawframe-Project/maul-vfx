// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Expansion: quads facing the view, turned by the particle's angle,
// upright on the world's Y, in the plane of an orientation, along the
// velocity and stretched; corners and their coordinates, the triangles'
// indices and winding; the emitter's sort; a local emitter's scale;
// buffers too small; the def's checks and the call's refusals.

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

// A vertex: position, corner, colour's red, serial.
typedef struct Vertex
{
    float position[3];
    float corner[2];
    float red;
    uint32_t serial;
} Vertex;

static const mvfxLayoutField s_fields[4] = {
    {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3, .offset = 0},
    {.field = mvfx_recordCorner, .format = mvfx_formatFloat32, .components = 2, .offset = 12},
    {.field = mvfx_recordColor, .format = mvfx_formatFloat32, .components = 1, .offset = 20},
    {.field = mvfx_recordSerial, .format = mvfx_formatUint32, .components = 1, .offset = 24},
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
    layoutDef.fieldCount = 4;
    layoutDef.stride = (uint32_t)sizeof(Vertex);
    CHECK(mvfxCreateLayout(scene.context, &layoutDef, &scene.layout) == mvfx_success, "layout");
    return scene;
}

static mvfxEmitterDef Sprites(mvfxBillboard billboard)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 4;
    emitter.hostSpawnCapacity = 4;
    emitter.lifetime = mvfxRangeSource(9.0f, 9.0f);
    emitter.size =
        (mvfxVectorSource){.kind = mvfx_sourceRange, .min = {2, 4, 1, 0}, .max = {2, 4, 1, 0}};
    emitter.color = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.5f, 1, 1, 1}, .max = {0.5f, 1, 1, 1}};
    emitter.sprite.billboard = billboard;
    return emitter;
}

static void Spawn(Scene* scene, const double* places, const float* velocities, int32_t count)
{
    mvfxSpawnRequest request = {.count = count, .positions = places, .velocities = velocities};
    CHECK(mvfxInstance_Spawn(scene->context, scene->instance, 0, &request) == mvfx_success,
          "spawned");
    CHECK(mvfxContext_StepTicks(scene->context, 1) == mvfx_success, "step");
}

typedef struct Written
{
    Vertex vertices[16];
    uint32_t indices[24];
    int64_t count;
    mvfxResult result;
} Written;

static const mvfxView s_view = {.eye = {0, 0, 20}, .forward = {0, 0, -1}, .up = {0, 1, 0}};

static Written Expand(Scene* scene, const mvfxView* view, int64_t vertices, int64_t indices)
{
    static Written written;
    static uint32_t scratch[4 * 4];
    memset(&written, 0xEE, sizeof written);
    mvfxExpandBuffers buffers = {.vertices = written.vertices,
                                 .vertexCapacity = vertices,
                                 .indices = written.indices,
                                 .indexCapacity = indices,
                                 .scratch = scratch,
                                 .scratchBytes = sizeof scratch};
    written.result = mvfxInstance_ExpandSprites(scene->context, scene->instance, 0, scene->layout,
                                                view, &buffers, &written.count);
    return written;
}

static bool Near(float a, float b)
{
    return fabsf(a - b) <= 1e-4f;
}

static bool At(const Vertex* v, float x, float y, float z)
{
    return Near(v->position[0], x) && Near(v->position[1], y) && Near(v->position[2], z);
}

static void TestCamera(void)
{
    // Size 2 by 4 at (1, 2, 3) facing the view down -z: corners from
    // (0, 0, 3) to (2, 4, 3); a second quad's indices from 4.
    mvfxEmitterDef emitter = Sprites(mvfx_billboardCamera);
    Scene scene = NewScene(&emitter);
    static const double places[6] = {1, 2, 3, 5, 5, 5};
    Spawn(&scene, places, NULL, 2);
    Written w = Expand(&scene, &s_view, 16, 24);
    CHECK(w.result == mvfx_success && w.count == 2, "two quads");
    const Vertex* v = w.vertices;
    CHECK(At(&v[0], 0, 0, 3) && At(&v[1], 2, 0, 3) && At(&v[2], 0, 4, 3) && At(&v[3], 2, 4, 3),
          "its corners");
    CHECK(v[0].corner[0] == 0.0f && v[0].corner[1] == 0.0f && v[1].corner[0] == 1.0f &&
              v[2].corner[1] == 1.0f && v[3].corner[0] == 1.0f && v[3].corner[1] == 1.0f &&
              v[3].red == 0.5f && v[3].serial == 0 && v[4].serial == 1,
          "its coordinates and values");
    static const uint32_t triangles[12] = {0, 1, 2, 2, 1, 3, 4, 5, 6, 6, 5, 7};
    CHECK(memcmp(w.indices, triangles, sizeof triangles) == 0, "two triangles each");
    // Counterclockwise seen from the eye: (v1 - v0) x (v2 - v0) points to
    // it.
    float a[2] = {v[1].position[0] - v[0].position[0], v[1].position[1] - v[0].position[1]};
    float b[2] = {v[2].position[0] - v[0].position[0], v[2].position[1] - v[0].position[1]};
    CHECK(a[0] * b[1] - a[1] * b[0] > 0.0f, "counterclockwise toward the eye");
    // Too small: whole quads that fit; none, the count.
    w = Expand(&scene, &s_view, 7, 24);
    CHECK(w.count == 2 && w.vertices[3].serial == 0 && w.vertices[4].serial == 0xEEEEEEEEu &&
              w.indices[6] == 0xEEEEEEEEu,
          "the vertices' room");
    w = Expand(&scene, &s_view, 16, 11);
    CHECK(w.count == 2 && w.vertices[4].serial == 0xEEEEEEEEu, "the indices' room");
    w = Expand(&scene, &s_view, 0, 0);
    CHECK(w.result == mvfx_success && w.count == 2, "the count alone");
    mvfxDestroyContext(scene.context);
}

static void TestRotated(void)
{
    // Turned a quarter about the view axis: right is up, up is left.
    mvfxEmitterDef emitter = Sprites(mvfx_billboardRotated);
    emitter.angle = mvfxRangeSource(1.5707963f, 1.5707963f);
    Scene scene = NewScene(&emitter);
    static const double places[3] = {1, 2, 3};
    Spawn(&scene, places, NULL, 1);
    Written w = Expand(&scene, &s_view, 16, 24);
    CHECK(At(&w.vertices[0], 3, 1, 3) && At(&w.vertices[1], 3, 3, 3) &&
              At(&w.vertices[3], -1, 3, 3),
          "turned by its angle");
    mvfxDestroyContext(scene.context);
}

static void TestAxisY(void)
{
    // Upright on Y facing an eye along +x: right is -z.
    mvfxEmitterDef emitter = Sprites(mvfx_billboardAxisY);
    Scene scene = NewScene(&emitter);
    static const double places[3] = {1, 2, 3};
    Spawn(&scene, places, NULL, 1);
    mvfxView side = s_view;
    side.eye[0] = 11.0;
    side.eye[1] = 9.0;
    side.eye[2] = 3.0;
    Written w = Expand(&scene, &side, 16, 24);
    CHECK(At(&w.vertices[0], 1, 0, 4) && At(&w.vertices[1], 1, 0, 2) && At(&w.vertices[3], 1, 4, 2),
          "upright on Y, facing the eye");
    mvfxDestroyContext(scene.context);
}

static void TestFixed(void)
{
    // An orientation a quarter about Z: right is +y, up is -x.
    mvfxEmitterDef emitter = Sprites(mvfx_billboardFixed);
    emitter.orientationMode = mvfx_orientationEuler;
    emitter.orientation = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0, 0, 1.5707963f, 0}, .max = {0, 0, 1.5707963f, 0}};
    Scene scene = NewScene(&emitter);
    static const double places[3] = {1, 2, 3};
    Spawn(&scene, places, NULL, 1);
    Written w = Expand(&scene, &s_view, 16, 24);
    CHECK(At(&w.vertices[0], 3, 1, 3) && At(&w.vertices[3], -1, 3, 3),
          "in its orientation's plane");
    mvfxDestroyContext(scene.context);
}

static void TestDirectional(void)
{
    // Moving up at 3 with a stretch of 0.5: 4 high grows by 1.5; facing
    // an eye straight above, right is +x.
    mvfxEmitterDef emitter = Sprites(mvfx_billboardDirectional);
    emitter.sprite.stretch = 0.5f;
    Scene scene = NewScene(&emitter);
    static const double places[3] = {1, 2, 3};
    static const float velocity[3] = {0, 3, 0};
    Spawn(&scene, places, velocity, 1);
    mvfxView above = s_view;
    above.eye[0] = 1.0;
    above.eye[1] = 2.0;
    Written w = Expand(&scene, &above, 16, 24);
    float y = w.vertices[0].position[1];
    CHECK(Near(w.vertices[0].position[0], 0.0f) && Near(w.vertices[1].position[0], 2.0f) &&
              Near(w.vertices[2].position[1] - y, 5.5f),
          "along its velocity, stretched");
    mvfxDestroyContext(scene.context);
}

// A quad's right and up edges in x and y.
static void Edges(const Written* w, float right[2], float up[2])
{
    for (int32_t k = 0; k < 2; ++k)
    {
        right[k] = w->vertices[1].position[k] - w->vertices[0].position[k];
        up[k] = w->vertices[2].position[k] - w->vertices[0].position[k];
    }
}

static void TestTurned(void)
{
    // A local emitter turned a quarter about Z: a fixed quad of no turn of
    // its own has right +y and up -x; a directional one moving along local
    // x moves along the world's y, so its up is +y and its right +x.
    mvfxEmitterDef emitter = Sprites(mvfx_billboardFixed);
    emitter.space = mvfx_spaceLocal;
    emitter.orientationMode = mvfx_orientationEuler;
    emitter.orientation = (mvfxVectorSource){.kind = mvfx_sourceRange};
    static const double origin[3] = {0, 0, 0};
    static const float quarter[4] = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    static const double places[3] = {0, 0, 0};
    static const float along[3] = {3, 0, 0};
    mvfxView above = s_view;
    above.eye[0] = 0.0;
    above.eye[1] = 0.0;
    float right[2];
    float up[2];
    Scene scene = NewScene(&emitter);
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 1.0f, true) ==
              mvfx_success,
          "turned");
    Spawn(&scene, places, along, 1);
    Written w = Expand(&scene, &above, 16, 24);
    Edges(&w, right, up);
    CHECK(Near(right[0], 0.0f) && Near(right[1], 2.0f) && Near(up[0], -4.0f) && Near(up[1], 0.0f),
          "a fixed quad turned with the instance");
    mvfxDestroyContext(scene.context);
    emitter.sprite.billboard = mvfx_billboardDirectional;
    scene = NewScene(&emitter);
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, quarter, 1.0f, true) ==
              mvfx_success,
          "turned");
    Spawn(&scene, places, along, 1);
    w = Expand(&scene, &above, 16, 24);
    Edges(&w, right, up);
    CHECK(Near(right[0], 2.0f) && Near(right[1], 0.0f) && Near(up[0], 0.0f) && Near(up[1], 4.0f),
          "moving along the world's y");
    mvfxDestroyContext(scene.context);
}

static void TestSortedAndLocal(void)
{
    // Back to front from the eye: the far one's quad first. A local
    // emitter at scale 2: twice the size.
    mvfxEmitterDef emitter = Sprites(mvfx_billboardCamera);
    emitter.sortMode = mvfx_sortBackToFront;
    emitter.space = mvfx_spaceLocal;
    Scene scene = NewScene(&emitter);
    static const double origin[3] = {0, 0, 0};
    static const float identity[4] = {0, 0, 0, 1};
    CHECK(mvfxInstance_SetTransform(scene.context, scene.instance, origin, identity, 2.0f, true) ==
              mvfx_success,
          "scaled");
    static const double places[6] = {0, 0, 5, 0, 0, -5};
    Spawn(&scene, places, NULL, 2);
    Written w = Expand(&scene, &s_view, 16, 24);
    CHECK(w.count == 2 && w.vertices[0].serial == 1 && w.vertices[4].serial == 0,
          "in the emitter's order");
    CHECK(Near(w.vertices[1].position[0] - w.vertices[0].position[0], 4.0f) &&
              Near(w.vertices[2].position[1] - w.vertices[0].position[1], 8.0f),
          "scaled with the instance");
    w = Expand(&scene, &s_view, 16, 24);
    mvfxExpandBuffers small = {.vertices = w.vertices,
                               .vertexCapacity = 16,
                               .indices = w.indices,
                               .indexCapacity = 24,
                               .scratch = w.indices,
                               .scratchBytes = 31};
    int64_t count = -1;
    CHECK(mvfxInstance_ExpandSprites(scene.context, scene.instance, 0, scene.layout, &s_view,
                                     &small, &count) == mvfx_errorCapacity &&
              count == 0,
          "scratch too small for the sort");
    mvfxDestroyContext(scene.context);
}

static void TestChecks(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = Sprites(5);
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectId effect = {0};
    int32_t refused = mvfxCreateEffect(context, &def, &effect) == mvfx_errorInvalid;
    const float stretches[3] = {-0.5f, (float)NAN, 2e6f};
    for (int32_t s = 0; s < 3; ++s)
    {
        emitter = Sprites(mvfx_billboardDirectional);
        emitter.sprite.stretch = stretches[s];
        refused += mvfxCreateEffect(context, &def, &effect) == mvfx_errorInvalid;
    }
    emitter = Sprites(mvfx_billboardDirectional);
    emitter.sprite.stretch = 1e6f;
    CHECK(refused == 4 && mvfxCreateEffect(context, &def, &effect) == mvfx_success,
          "the def's checks");
    // A corner has two components.
    static const mvfxLayoutField one = {
        .field = mvfx_recordCorner, .format = mvfx_formatFloat32, .components = 1};
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = &one;
    layoutDef.fieldCount = 1;
    layoutDef.stride = 8;
    mvfxLayoutId layout = {0};
    CHECK(mvfxCreateLayout(context, &layoutDef, &layout) == mvfx_errorInvalid, "a corner of one");
    mvfxDestroyContext(context);
}

static void TestRefusals(void)
{
    mvfxEmitterDef emitter = Sprites(mvfx_billboardCamera);
    Scene scene = NewScene(&emitter);
    mvfxContext* c = scene.context;
    mvfxInstanceId i = scene.instance;
    mvfxLayoutId l = scene.layout;
    static Vertex vertices[4];
    static uint32_t indices[6];
    static uint32_t scratch[16];
    mvfxExpandBuffers good = {.vertices = vertices,
                              .vertexCapacity = 4,
                              .indices = indices,
                              .indexCapacity = 6,
                              .scratch = scratch,
                              .scratchBytes = 64};
    mvfxExpandBuffers bad[6] = {good, good, good, good, good, good};
    bad[0].vertices = NULL;
    bad[1].vertexCapacity = -1;
    bad[2].indices = NULL;
    bad[3].indexCapacity = -1;
    bad[4].indices = (uint32_t*)((unsigned char*)indices + 2);
    bad[5].scratch = (unsigned char*)scratch + 1;
    mvfxView upNan = s_view;
    upNan.up[2] = (float)NAN;
    int64_t count = 0;
    uint64_t misuse = mvfxGetContextMisuse(c);
    int32_t refused = 0;
    for (int32_t b = 0; b < 6; ++b)
    {
        refused +=
            mvfxInstance_ExpandSprites(c, i, 0, l, &s_view, &bad[b], &count) == mvfx_errorInvalid;
    }
    refused += mvfxInstance_ExpandSprites(c, i, 0, l, &upNan, &good, &count) == mvfx_errorInvalid;
    refused += mvfxInstance_ExpandSprites(c, i, 0, l, NULL, &good, &count) == mvfx_errorInvalid;
    refused += mvfxInstance_ExpandSprites(c, i, 0, l, &s_view, NULL, &count) == mvfx_errorInvalid;
    refused += mvfxInstance_ExpandSprites(c, i, 0, l, &s_view, &good, NULL) == mvfx_errorInvalid;
    refused += mvfxInstance_ExpandSprites(c, i, 1, l, &s_view, &good, &count) == mvfx_errorInvalid;
    CHECK(refused == 11 && mvfxGetContextMisuse(c) == misuse + 11, "refused, each counted");
    mvfxInstanceId stale = i;
    stale.generation += 1;
    mvfxLayoutId staleLayout = l;
    staleLayout.generation += 1;
    CHECK(mvfxInstance_ExpandSprites(c, stale, 0, l, &s_view, &good, &count) == mvfx_errorStale &&
              mvfxInstance_ExpandSprites(c, i, 0, staleLayout, &s_view, &good, &count) ==
                  mvfx_errorStale &&
              mvfxInstance_ExpandSprites(NULL, i, 0, l, &s_view, &good, &count) ==
                  mvfx_errorInvalid,
          "stale, no context");
    mvfxDestroyContext(c);
}

int main(void)
{
    TestCamera();
    TestRotated();
    TestAxisY();
    TestFixed();
    TestDirectional();
    TestTurned();
    TestSortedAndLocal();
    TestChecks();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
