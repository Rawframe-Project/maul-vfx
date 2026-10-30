// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Every public call that computes, under every host test_fenv.h plays:
// each rounding mode, each with flush-to-zero and denormals-are-zero off
// and on (src/fpenv.h). A case makes its inputs from constants and the
// library's own calls, does no float arithmetic of its own, and writes
// every result it gets; it runs once in the default setting and then
// under each host, and its bytes must be the same, with the host's
// setting as it set it after every case.

#include "test_fenv.h"
#include "test_harness.h"

#include "maul-vfx/context.h"
#include "maul-vfx/effect.h"
#include "maul-vfx/file.h"
#include "maul-vfx/gas.h"
#include "maul-vfx/grid.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/mesh.h"
#include "maul-vfx/output.h"
#include "maul-vfx/plan.h"
#include "maul-vfx/water.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define STEP_TICKS (MVFX_TICKS_PER_SECOND / 60)

typedef struct Out
{
    unsigned char bytes[1 << 20];
    size_t at;
    bool full;
} Out;

static void Put(Out* out, const void* value, size_t size)
{
    if (value == NULL)
    {
        static const unsigned char none = 0xFF;
        value = &none;
        size = 1;
    }
    if (out->at + size > sizeof out->bytes)
    {
        out->full = true;
        return;
    }
    memcpy(out->bytes + out->at, value, size);
    out->at += size;
}

#define PUT(out, value) Put((out), &(value), sizeof(value))

static mvfxContext* NewContext(Out* out)
{
    mvfxContextDef def = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    mvfxResult r = mvfxCreateContext(&def, &context);
    PUT(out, r);
    return context;
}

// The effect NewInstance made last.
static mvfxEffectId s_effect;

// A quaternion whose norm is within 1e-3 of 1 when rounded to nearest and
// not when rounded upward, where a frame's or a pose's check reads it.
static const float s_edge[4] = {0x1.5f5bb4p-7f, 0x1.63c3f4p-5f, 0.0f, 1.0f};

// An effect of the emitters, gases and surfaces, and an instance of it
// placed off the origin, turned and grown.
static mvfxInstanceId NewInstance(Out* out, mvfxContext* context, const mvfxEmitterDef* emitters,
                                  int32_t emitterCount, const mvfxGasDef* gases, int32_t gasCount,
                                  const mvfxWaterDef* waters, int32_t waterCount, bool planned)
{
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = emitterCount;
    def.gases = gases;
    def.gasCount = gasCount;
    def.waters = waters;
    def.waterCount = waterCount;
    mvfxEffectId effect = {0};
    mvfxResult r = mvfxCreateEffect(context, &def, &effect, NULL);
    PUT(out, r);
    s_effect = effect;
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    instanceDef.origin[0] = 0.3333333333333333;
    instanceDef.origin[1] = -0.2;
    instanceDef.origin[2] = 0.7;
    instanceDef.rotation[0] = 0.1f;
    instanceDef.rotation[1] = 0.3f;
    instanceDef.rotation[2] = -0.2f;
    instanceDef.rotation[3] = 0.92736185f;
    instanceDef.scale = 1.3f;
    instanceDef.planned = planned;
    mvfxInstanceId instance = {0};
    r = mvfxCreateInstance(context, &instanceDef, &instance);
    PUT(out, r);
    return instance;
}

// Moves an instance to its place of step s, along a turn.
static void Move(Out* out, mvfxContext* context, mvfxInstanceId instance, int32_t s)
{
    static const double origins[4][3] = {
        {0.3333333333333333, -0.2, 0.7}, {0.41, -0.17, 0.66}, {0.52, -0.1, 0.61}, {0.6, 0.0, 0.55}};
    static const float turns[4][4] = {{0.1f, 0.3f, -0.2f, 0.92736185f},
                                      {0.11f, 0.29f, -0.19f, 0.93127597f},
                                      {0.12f, 0.28f, -0.18f, 0.93509358f},
                                      {0.13f, 0.27f, -0.17f, 0.9388291f}};
    static const float scales[4] = {1.3f, 1.31f, 1.33f, 1.37f};
    mvfxResult r = mvfxInstance_SetTransform(context, instance, origins[s % 4], turns[s % 4],
                                             scales[s % 4], false);
    PUT(out, r);
}

static void Step(Out* out, mvfxContext* context)
{
    mvfxResult r = mvfxContext_StepTicks(context, STEP_TICKS);
    PUT(out, r);
}

// A layout of a record's place, velocity, color and size.
static mvfxLayoutId Layout(Out* out, mvfxContext* context)
{
    static const mvfxLayoutField fields[4] = {
        {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
        {.field = mvfx_recordVelocity, .format = mvfx_formatFloat32, .components = 3, .offset = 12},
        {.field = mvfx_recordColor, .format = mvfx_formatFloat16, .components = 4, .offset = 24},
        {.field = mvfx_recordSize, .format = mvfx_formatFloat32, .components = 2, .offset = 32},
    };
    mvfxLayoutDef def = mvfxDefaultLayoutDef();
    def.fields = fields;
    def.fieldCount = 4;
    def.stride = 40;
    mvfxLayoutId layout = {0};
    mvfxResult r = mvfxCreateLayout(context, &def, &layout);
    PUT(out, r);
    return layout;
}

static const mvfxView s_view = {.eye = {0.7, 1.1, 9.3},
                                .forward = {0.0f, -0.0995037f, -0.9950372f},
                                .up = {0.0f, 0.9950372f, -0.0995037f}};

static void Particles(Out* out, mvfxContext* context, mvfxInstanceId instance, int32_t emitter,
                      mvfxLayoutId layout)
{
    static unsigned char records[256 * 40];
    static uint32_t scratch[256 * 4];
    memset(records, 0, sizeof records);
    int64_t count = 0;
    mvfxResult r = mvfxInstance_WriteParticles(context, instance, emitter, layout, &s_view, scratch,
                                               (int64_t)sizeof scratch, records, 256, &count);
    PUT(out, r);
    PUT(out, count);
    Put(out, records, (size_t)(count > 0 ? count : 0) * 40);
}

static void Events(Out* out, mvfxContext* context, mvfxInstanceId instance, int32_t emitter)
{
    const mvfxEvent* events = NULL;
    int32_t count = 0;
    mvfxResult r = mvfxInstance_GetEvents(context, instance, emitter, &events, &count);
    PUT(out, r);
    PUT(out, count);
    for (int32_t i = 0; i < count; ++i)
    {
        PUT(out, events[i]);
    }
    mvfxEmitterStats stats;
    memset(&stats, 0, sizeof stats);
    r = mvfxInstance_GetEmitterStats(context, instance, emitter, &stats);
    PUT(out, r);
    PUT(out, stats);
}

// The values that need no context: the sRGB steps and a baked curl noise.
static void Constants(Out* out)
{
    static float steps[MVFX_SRGB_STEPS];
    mvfxGetSrgbSteps(steps);
    PUT(out, steps);
    mvfxNoise noise = {.frequency = 0.37f,
                       .octaves = 2,
                       .lacunarity = 2.0f,
                       .gain = 0.45f,
                       .scroll = {0.1f, 0.0f, -0.3f},
                       .seed = 7};
    static float baked[4 * 4 * 4 * 3];
    const int32_t size[3] = {4, 4, 4};
    mvfxResult r = mvfxBakeCurlNoise(&noise, 4, size, baked, 4 * 4 * 4 * 3);
    PUT(out, r);
    PUT(out, baked);
}

// An emitter of thrown particles: a cone, gravity, drag, a ground plane
// and the instance's colliders, lights, sorted, its births, deaths and
// collisions recorded.
static mvfxEmitterDef Thrown(void)
{
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 256;
    emitter.rate = 37.0f;
    emitter.lifetime = mvfxRangeSource(0.3f, 0.9f);
    emitter.velocity = (mvfxVelocity){.mode = mvfx_directionCone,
                                      .direction = {0.0f, 1.0f, 0.0f},
                                      .coneAngle = 0.7f,
                                      .speed = {1.3f, 3.7f}};
    emitter.motion.gravity[1] = -9.81f;
    emitter.motion.drag = 0.37f;
    // Subnormal, so not 0, which its view's features compare.
    emitter.inheritVelocity = 0x1p-140f;
    emitter.color = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.1f, 0.2f, 0.3f, 0.4f}, .max = {0.9f, 0.7f, 0.5f, 1.0f}};
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.1f, 0.2f, 0.0f, 0.0f}, .max = {0.3f, 0.4f, 0.0f, 0.0f}};
    emitter.light = (mvfxLightDef){
        .enable = true, .every = 3, .intensity = 2.7f, .radius = 0.9f, .radiusBySize = 0.3f};
    emitter.sortMode = mvfx_sortBackToFront;
    emitter.sortMeasure = mvfx_sortByDistance;
    static const mvfxCollider ground = {
        .kind = mvfx_colliderPlane, .center = {0.0f, -0.37f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}};
    emitter.colliders = &ground;
    emitter.colliderCount = 1;
    emitter.collision.enable = true;
    emitter.collision.hostColliders = true;
    emitter.collision.restitution = 0.43f;
    emitter.collision.friction = 0.21f;
    emitter.events = mvfx_eventMaskBirth | mvfx_eventMaskDeath | mvfx_eventMaskCollision;
    emitter.eventCapacity = 64;
    return emitter;
}

// Particles thrown, stepped by ticks and by seconds under a moving
// instance with the host's colliders, read as records, lights, bounds,
// draw order and quads; another instance prewarmed.
static void Throws(Out* out)
{
    mvfxContext* context = NewContext(out);
    mvfxEmitterDef emitter = Thrown();
    mvfxInstanceId instance = NewInstance(out, context, &emitter, 1, NULL, 0, NULL, 0, false);
    mvfxEmitterView view;
    memset(&view, 0, sizeof view);
    mvfxResult viewed = mvfxEffect_GetEmitterView(context, s_effect, 0, &view);
    PUT(out, viewed);
    PUT(out, view.features);
    PUT(out, view.inheritVelocity);
    static const mvfxCollider hosts[3] = {
        {.kind = mvfx_colliderPlane, .center = {0.0f, 0.03f, 0.0f}, .normal = {0.3f, 1.1f, 0.2f}},
        {.kind = mvfx_colliderSphere, .center = {0.31f, 0.2f, 0.17f}, .radius = 0.45f},
        {.kind = mvfx_colliderBox,
         .center = {-0.4f, 0.1f, 0.3f},
         .halfExtents = {0.3f, 0.2f, 0.1f},
         .rotation = {0.0f, 0.38268343f, 0.0f, 0.92387953f}}};
    // A plane whose normal's square is subnormal: valid, but flushed to 0
    // it has no direction.
    static const mvfxCollider faint = {.kind = mvfx_colliderPlane,
                                       .center = {0.0f, -0.29f, 0.0f},
                                       .normal = {0x1p-70f, 0.0f, 0.0f}};
    mvfxResult r = mvfxInstance_SetColliders(context, instance, &faint, 1);
    PUT(out, r);
    r = mvfxInstance_SetColliders(context, instance, hosts, 3);
    PUT(out, r);
    mvfxLayoutId layout = Layout(out, context);
    for (int32_t s = 0; s < 48; ++s)
    {
        Move(out, context, instance, s);
        Step(out, context);
        Events(out, context, instance, 0);
    }
    r = mvfxContext_Step(context, 0.0123);
    PUT(out, r);
    r = mvfxContext_Step(context, -0x1p-1060);
    PUT(out, r);
    Particles(out, context, instance, 0, layout);
    static mvfxLight lights[64];
    memset(lights, 0, sizeof lights);
    int64_t count = 0;
    r = mvfxInstance_WriteLights(context, instance, 0, &s_view, lights, 64, &count);
    PUT(out, r);
    PUT(out, count);
    PUT(out, lights);
    mvfxBounds bounds;
    memset(&bounds, 0, sizeof bounds);
    r = mvfxInstance_GetBounds(context, instance, -1, &bounds);
    PUT(out, r);
    PUT(out, bounds);
    static mvfxDrawItem items[8];
    memset(items, 0, sizeof items);
    r = mvfxContext_OrderEmitters(context, s_view.eye, NULL, items, 8, &count);
    PUT(out, r);
    PUT(out, count);
    PUT(out, items);
    static const mvfxLayoutField corners[2] = {
        {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
        {.field = mvfx_recordCorner, .format = mvfx_formatFloat32, .components = 2, .offset = 12},
    };
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = corners;
    layoutDef.fieldCount = 2;
    layoutDef.stride = 20;
    mvfxLayoutId quads = {0};
    r = mvfxCreateLayout(context, &layoutDef, &quads);
    PUT(out, r);
    static float vertices[4 * 256][5];
    static uint32_t indices[6 * 256];
    static uint32_t scratch[256 * 4];
    memset(vertices, 0, sizeof vertices);
    const mvfxExpandBuffers buffers = {.vertices = vertices,
                                       .vertexCapacity = 4 * 256,
                                       .indices = indices,
                                       .indexCapacity = 6 * 256,
                                       .scratch = scratch,
                                       .scratchBytes = (int64_t)sizeof scratch};
    r = mvfxInstance_ExpandSprites(context, instance, 0, quads, &s_view, &buffers, &count);
    PUT(out, r);
    PUT(out, count);
    PUT(out, vertices);
    // Prewarmed: a third of a second in steps of a sixtieth.
    mvfxInstanceId warm = NewInstance(out, context, &emitter, 1, NULL, 0, NULL, 0, false);
    double left = -1.0;
    r = mvfxInstance_Prewarm(context, warm, 0.3333, 0.0166667, 64, &left);
    PUT(out, r);
    PUT(out, left);
    Particles(out, context, warm, 0, layout);
    mvfxDestroyContext(context);
}

// An effect written, read back and made from its bytes, its particles
// stepped and read.
static void Files(Out* out)
{
    // The second emitter's least lifetime a subnormal, which is above 0.
    mvfxEmitterDef emitters[2] = {Thrown(), Thrown()};
    emitters[1].lifetime = mvfxRangeSource(0x1p-140f, 0.5f);
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = 2;
    static unsigned char file[1 << 15];
    memset(file, 0, sizeof file);
    int64_t size = 0;
    mvfxFileResult written =
        mvfxWriteEffect(&def, NULL, 0, file, (int64_t)sizeof file, &size, NULL);
    PUT(out, written);
    PUT(out, size);
    PUT(out, file);
    static _Alignas(16) unsigned char memory[1 << 15];
    memset(memory, 0, sizeof memory);
    int64_t needed = 0;
    mvfxEffectDef read;
    memset(&read, 0, sizeof read);
    mvfxFileResult r =
        mvfxReadEffect(file, size, NULL, 0, memory, (int64_t)sizeof memory, &needed, &read, NULL);
    PUT(out, r);
    PUT(out, needed);
    PUT(out, memory);
    mvfxContext* context = NewContext(out);
    mvfxEffectId effect = {0};
    r = mvfxCreateEffectFromBytes(context, file, size, NULL, 0, &effect, NULL);
    PUT(out, r);
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    mvfxInstanceId instance = {0};
    mvfxResult made = mvfxCreateInstance(context, &instanceDef, &instance);
    PUT(out, made);
    mvfxLayoutId layout = Layout(out, context);
    for (int32_t s = 0; s < 6; ++s)
    {
        Step(out, context);
    }
    Particles(out, context, instance, 0, layout);
    mvfxDestroyContext(context);
}

// A mesh of a lopsided pyramid: made, moved, its hull's center and
// vertices; rigid boxes on its hull fall onto a ground, sleep, and are
// woken within a sphere.
static void Meshes(Out* out)
{
    mvfxContext* context = NewContext(out);
    static const float positions[5][3] = {{-0.7f, 0.0f, -0.3f},
                                          {0.6f, 0.1f, -0.4f},
                                          {0.5f, 0.0f, 0.7f},
                                          {-0.6f, -0.1f, 0.6f},
                                          {0.1f, 0.9f, 0.13f}};
    static const uint32_t triangles[6][3] = {{0, 2, 1}, {0, 3, 2}, {0, 1, 4},
                                             {1, 2, 4}, {2, 3, 4}, {3, 0, 4}};
    mvfxMeshDef def = mvfxDefaultMeshDef();
    def.positions = &positions[0][0];
    def.vertexCount = 5;
    def.triangles = &triangles[0][0];
    def.triangleCount = 6;
    mvfxMeshId mesh = {0};
    mvfxResult r = mvfxCreateMesh(context, &def, &mesh);
    PUT(out, r);
    mvfxMeshView view;
    memset(&view, 0, sizeof view);
    r = mvfxMesh_GetView(context, mesh, &view);
    PUT(out, r);
    Put(out, view.normals, 5 * 3 * sizeof(float));
    Put(out, view.triangleSums, 6 * sizeof(double));
    bool hull = false;
    float center[3] = {0.0f, 0.0f, 0.0f};
    int32_t vertices = 0;
    r = mvfxMesh_GetHullCenter(context, mesh, &hull, center);
    PUT(out, r);
    PUT(out, hull);
    PUT(out, center);
    r = mvfxMesh_GetHullVertexCount(context, mesh, &vertices);
    PUT(out, r);
    PUT(out, vertices);
    static const float moved[5][3] = {{-0.71f, 0.01f, -0.3f},
                                      {0.61f, 0.1f, -0.41f},
                                      {0.5f, 0.02f, 0.71f},
                                      {-0.6f, -0.11f, 0.6f},
                                      {0.1f, 0.93f, 0.13f}};
    r = mvfxMesh_SetPositions(context, mesh, &moved[0][0], NULL, 5);
    PUT(out, r);
    r = mvfxMesh_GetView(context, mesh, &view);
    PUT(out, r);
    Put(out, view.normals, 5 * 3 * sizeof(float));
    Put(out, view.triangleSums, 6 * sizeof(double));
    r = mvfxMesh_GetHullCenter(context, mesh, &hull, center);
    PUT(out, r);
    PUT(out, center);
    static const mvfxCollider ground = {
        .kind = mvfx_colliderPlane, .center = {0.0f, -0.5f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.capacity = 4;
    emitter.hostSpawnCapacity = 4;
    emitter.lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    emitter.motion.gravity[1] = -9.81f;
    emitter.size = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.3f, 0.3f, 0.3f}, .max = {0.3f, 0.3f, 0.3f}};
    emitter.orientationMode = mvfx_orientationEuler;
    emitter.orientation = (mvfxVectorSource){
        .kind = mvfx_sourceRange, .min = {0.1f, 0.2f, 0.3f}, .max = {0.1f, 0.2f, 0.3f}};
    emitter.rigid.enable = true;
    emitter.rigid.shape = mvfx_rigidHull;
    emitter.rigid.hull = mesh;
    emitter.rigid.density = 1.0f;
    emitter.rigid.substeps = 8;
    emitter.rigid.staticFriction = 0.6f;
    emitter.rigid.dynamicFriction = 0.4f;
    emitter.collision.enable = true;
    emitter.colliders = &ground;
    emitter.colliderCount = 1;
    mvfxInstanceId instance = NewInstance(out, context, &emitter, 1, NULL, 0, NULL, 0, false);
    static const double places[6] = {0.1, 0.4, 0.2, 1.3, 0.6, -0.4};
    const mvfxSpawnRequest request = {.count = 2, .positions = places};
    r = mvfxInstance_Spawn(context, instance, 0, &request);
    PUT(out, r);
    mvfxLayoutId layout = Layout(out, context);
    for (int32_t s = 0; s < 150; ++s)
    {
        Step(out, context);
    }
    Events(out, context, instance, 0);
    static const float near[3] = {0.5f, -0.3f, 0.9f};
    r = mvfxInstance_WakeBodies(context, instance, 0, near, 0.7f);
    PUT(out, r);
    Events(out, context, instance, 0);
    Step(out, context);
    Particles(out, context, instance, 0, layout);
    mvfxDestroyContext(context);
}

// Grids of both formats, subnormal values among them: made, set whole and
// in a box, read back.
static void Grids(Out* out)
{
    mvfxContext* context = NewContext(out);
    static const float values[2 * 2 * 2] = {0.3333333f,  -0x1p-20f,  0x1p-130f, 65519.0f,
                                            -2.7182817f, 0x1.8p-25f, 1e-5f,     7.0e-8f};
    static const float others[2 * 2 * 2] = {0.1f,    0x1p-140f, -0.2f, 0.7f,
                                            6.1e-5f, -3.0e-6f,  9.9f,  1.0f / 3.0f};
    for (int32_t f = 0; f < 2; ++f)
    {
        mvfxGridDef def = mvfxDefaultGridDef();
        def.size[0] = 2;
        def.size[1] = 2;
        def.size[2] = 2;
        def.format = f == 0 ? mvfx_gridFloat32 : mvfx_gridFloat16;
        def.values = values;
        mvfxGridId grid = {0};
        mvfxResult r = mvfxCreateGrid(context, &def, &grid);
        PUT(out, r);
        mvfxGridView view;
        memset(&view, 0, sizeof view);
        r = mvfxGrid_GetView(context, grid, &view);
        PUT(out, r);
        Put(out, view.values, (size_t)view.bytes);
        r = mvfxGrid_SetValues(context, grid, others, 8);
        PUT(out, r);
        r = mvfxGrid_GetView(context, grid, &view);
        Put(out, view.values, (size_t)view.bytes);
        const int32_t first[3] = {1, 0, 1};
        const int32_t size[3] = {1, 2, 1};
        r = mvfxGrid_SetValuesIn(context, grid, first, size, values, 2);
        PUT(out, r);
        r = mvfxGrid_GetView(context, grid, &view);
        Put(out, view.values, (size_t)view.bytes);
    }
    mvfxDestroyContext(context);
}

// Strips: a trail behind the moving instance as points, a strip and a
// tube; a plexus of host points as links and quads; a sheet of cloth
// expanded; a chain hung from the host's turning frames.
static void Strips(Out* out)
{
    mvfxContext* context = NewContext(out);
    mvfxEmitterDef emitters[4] = {mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef(),
                                  mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef()};
    emitters[0].capacity = 64;
    emitters[0].rate = 30.0f;
    emitters[0].lifetime = mvfxRangeSource(0.7f, 0.9f);
    emitters[0].velocity.speed = (mvfxRange){0.3f, 0.7f};
    emitters[0].ribbon.ids = mvfx_ribbonOne;
    emitters[0].ribbon.pieces = 3;
    emitters[0].ribbon.pinHead = true;
    emitters[1].rate = 0.0f;
    emitters[1].capacity = 8;
    emitters[1].hostSpawnCapacity = 8;
    emitters[1].lifetime = mvfxRangeSource(100.0f, 100.0f);
    emitters[1].plexus = (mvfxPlexusDef){.enable = true,
                                         .distance = 0.9f,
                                         .maxLinks = 64,
                                         .maxPerParticle = 4,
                                         .width = 0.13f,
                                         .fade = true};
    emitters[2].rate = 0.0f;
    emitters[2].capacity = 12;
    emitters[2].hostSpawnCapacity = 1;
    emitters[2].lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    emitters[2].motion.gravity[1] = -9.81f;
    emitters[2].unit = (mvfxUnitDef){.kind = mvfx_unitSheet,
                                     .points = 4,
                                     .rows = 3,
                                     .spacing = 0.3f,
                                     .direction = {1, 0, 0},
                                     .downDirection = {0, 0, 1},
                                     .substeps = 4,
                                     .damping = 2.0f,
                                     .pinFirstRow = true};
    emitters[3].rate = 0.0f;
    emitters[3].capacity = 6;
    emitters[3].hostSpawnCapacity = 2;
    emitters[3].lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    emitters[3].motion.gravity[1] = -9.81f;
    emitters[3].unit = (mvfxUnitDef){.kind = mvfx_unitChain,
                                     .points = 3,
                                     .spacing = 0.37f,
                                     .direction = {0, -1, 0},
                                     .substeps = 4,
                                     .damping = 1.0f,
                                     .pinFirst = true,
                                     .attach = mvfx_attachHost,
                                     .attachFrameCapacity = 2};
    mvfxInstanceId instance = NewInstance(out, context, emitters, 4, NULL, 0, NULL, 0, false);
    static const double points[18] = {0.1, 0.2, 0.3,  0.5, 0.1, 0.2, -0.3, 0.4, 0.1,
                                      0.2, 0.7, -0.1, 0.9, 0.3, 0.0, -0.1, 0.0, 0.45};
    mvfxSpawnRequest request = {.count = 6, .positions = points};
    mvfxResult r = mvfxInstance_Spawn(context, instance, 1, &request);
    PUT(out, r);
    static const double root[3] = {0.2, 0.5, -0.1};
    request = (mvfxSpawnRequest){.count = 1, .positions = root};
    r = mvfxInstance_Spawn(context, instance, 2, &request);
    PUT(out, r);
    mvfxAttachFrame frames[2] = {
        {.position = {0.3, 0.2, 0.1}, .rotation = {0.0f, 0.0f, 0.38268343f, 0.92387953f}},
        {.position = {-0.7, 0.1, 0.4}, .rotation = {s_edge[0], s_edge[1], s_edge[2], s_edge[3]}}};
    r = mvfxInstance_SetAttachFrames(context, instance, 3, frames, 2);
    PUT(out, r);
    static const double roots[6] = {0.4, 0.0, 0.0, -0.3, 0.1, 0.2};
    static const int32_t on[2] = {0, 1};
    request = (mvfxSpawnRequest){.count = 2, .positions = roots, .attachFrames = on};
    r = mvfxInstance_Spawn(context, instance, 3, &request);
    PUT(out, r);
    for (int32_t s = 0; s < 20; ++s)
    {
        Move(out, context, instance, s);
        frames[0].position[0] = s % 2 == 0 ? 0.3 : 0.37;
        r = mvfxInstance_SetAttachFrames(context, instance, 3, frames, 2);
        PUT(out, r);
        Step(out, context);
    }
    mvfxLayoutId layout = Layout(out, context);
    Particles(out, context, instance, 3, layout);
    static unsigned char points40[256 * 40];
    static mvfxRibbon ribbons[64];
    static uint32_t scratch[1024 * 4];
    memset(points40, 0, sizeof points40);
    memset(ribbons, 0, sizeof ribbons);
    const mvfxRibbonBuffers ribbonBuffers = {.points = points40,
                                             .pointCapacity = 256,
                                             .ribbons = ribbons,
                                             .ribbonCapacity = 64,
                                             .scratch = scratch,
                                             .scratchBytes = (int64_t)sizeof scratch};
    int64_t counts[2] = {0, 0};
    r = mvfxInstance_WriteRibbons(context, instance, 0, layout, &s_view, &ribbonBuffers, &counts[0],
                                  &counts[1]);
    PUT(out, r);
    PUT(out, counts);
    PUT(out, points40);
    PUT(out, ribbons);
    memset(points40, 0, sizeof points40);
    r = mvfxInstance_WritePlexus(context, instance, 1, layout, &s_view, &ribbonBuffers, &counts[0],
                                 &counts[1]);
    PUT(out, r);
    PUT(out, counts);
    PUT(out, points40);
    static const mvfxLayoutField fields[3] = {
        {.field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3},
        {.field = mvfx_recordCorner, .format = mvfx_formatFloat32, .components = 2, .offset = 12},
        {.field = mvfx_recordFaceNormal,
         .format = mvfx_formatFloat32,
         .components = 3,
         .offset = 20},
    };
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = fields;
    layoutDef.fieldCount = 3;
    layoutDef.stride = 32;
    mvfxLayoutId strip = {0};
    r = mvfxCreateLayout(context, &layoutDef, &strip);
    PUT(out, r);
    static float vertices[2048][8];
    static uint32_t indices[8192];
    const mvfxExpandBuffers buffers = {.vertices = vertices,
                                       .vertexCapacity = 2048,
                                       .indices = indices,
                                       .indexCapacity = 8192,
                                       .scratch = scratch,
                                       .scratchBytes = (int64_t)sizeof scratch};
    const mvfxTubeDef tube = {.sides = 5, .caps = true};
    for (int32_t k = 0; k < 4; ++k)
    {
        memset(vertices, 0, sizeof vertices);
        memset(indices, 0, sizeof indices);
        counts[0] = 0;
        counts[1] = 0;
        r = k == 0   ? mvfxInstance_ExpandRibbons(context, instance, 0, strip, &s_view, &buffers,
                                                  &counts[0], &counts[1])
            : k == 1 ? mvfxInstance_ExpandTubes(context, instance, 0, strip, &s_view, &tube,
                                                &buffers, &counts[0], &counts[1])
            : k == 2 ? mvfxInstance_ExpandPlexus(context, instance, 1, strip, &s_view, &buffers,
                                                 &counts[0], &counts[1])
                     : mvfxInstance_ExpandSheets(context, instance, 2, strip, &s_view, &buffers,
                                                 &counts[0], &counts[1]);
        PUT(out, r);
        PUT(out, counts);
        Put(out, vertices, (size_t)(counts[0] > 0 ? counts[0] : 0) * sizeof vertices[0]);
        Put(out, indices, (size_t)(counts[1] > 0 ? counts[1] : 0) * sizeof indices[0]);
    }
    mvfxDestroyContext(context);
}

// A depth image a camera sees: particles thrown at it collide where it
// says; and the host's scene answering queries, one answer's normal
// subnormal.
static void Scenes(Out* out)
{
    mvfxContext* context = NewContext(out);
    mvfxEmitterDef emitters[2] = {mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef()};
    emitters[0].rate = 0.0f;
    emitters[0].capacity = 8;
    emitters[0].hostSpawnCapacity = 8;
    emitters[0].lifetime = mvfxRangeSource(100.0f, 100.0f);
    emitters[0].collision = (mvfxCollision){.enable = true,
                                            .restitution = 0.7f,
                                            .enableDepthImage = true,
                                            .depthThickness = 0.5f,
                                            .depthSamples = 64};
    emitters[0].events = mvfx_eventMaskCollision;
    emitters[0].eventCapacity = 8;
    static const mvfxBurst burst = {
        .minCount = 32, .maxCount = 32, .cycles = 1, .probability = 1.0f};
    emitters[1].capacity = 32;
    emitters[1].rate = 0.0f;
    emitters[1].bursts = &burst;
    emitters[1].burstCount = 1;
    emitters[1].lifetime = mvfxRangeSource(10.0f, 10.0f);
    emitters[1].shape.kind = mvfx_shapeBox;
    emitters[1].shape.offset[1] = 1.3f;
    emitters[1].shape.halfExtents[0] = 0.7f;
    emitters[1].shape.halfExtents[2] = 0.7f;
    emitters[1].motion.gravity[1] = -9.81f;
    emitters[1].collision.enable = true;
    emitters[1].collision.restitution = 0.3f;
    emitters[1].collision.friction = 0.5f;
    emitters[1].collision.enableSceneQueries = true;
    emitters[1].collision.queryCapacity = 32;
    emitters[1].collision.lookahead = 0.25f;
    mvfxInstanceId instance = NewInstance(out, context, emitters, 2, NULL, 0, NULL, 0, false);
    static float pixels[8 * 8];
    for (int32_t i = 0; i < 8 * 8; ++i)
    {
        pixels[i] = i % 3 == 0 ? 0.9937f : 0.9971f;
    }
    const mvfxDepthImage image = {
        .pixels = pixels,
        .byteCount = (int64_t)sizeof pixels,
        .width = 8,
        .height = 8,
        .stride = 8 * 4,
        .format = mvfx_depthFloat32,
        .projection = {1.37f, 0, 0, 0, 0, 1.37f, 0, 0, 0, 0, -1.0013f, -1, 0, 0, -0.10037f, 0},
        .origin = {0.1, 0.2, 10.0},
        .rotation = {s_edge[0], s_edge[1], s_edge[2], s_edge[3]},
    };
    mvfxResult r = mvfxInstance_SetDepthImage(context, instance, &image);
    PUT(out, r);
    static const double places[12] = {0.1, 0.2, 0.3, -0.2, 0.1, 0.0, 0.3, -0.3, 0.7, 0.0, 0.0, 0.1};
    static const float velocities[12] = {0.1f,  0.0f, -7.0f, 0.0f, 0.2f, -6.0f,
                                         -0.3f, 0.1f, -9.0f, 0.0f, 0.0f, -5.0f};
    const mvfxSpawnRequest request = {.count = 4, .positions = places, .velocities = velocities};
    r = mvfxInstance_Spawn(context, instance, 0, &request);
    PUT(out, r);
    static mvfxSceneAnswer answers[32];
    for (int32_t s = 0; s < 40; ++s)
    {
        Step(out, context);
        Events(out, context, instance, 0);
        const mvfxSceneQuery* queries = NULL;
        int32_t count = 0;
        r = mvfxInstance_GetSceneQueries(context, instance, 1, &queries, &count);
        PUT(out, r);
        for (int32_t i = 0; i < count; ++i)
        {
            answers[i] =
                (mvfxSceneAnswer){.hit = true,
                                  .point = {queries[i].origin[0], 0.0, queries[i].origin[2]},
                                  .normal = {0.6f, 0.8f, 0.0f}};
            PUT(out, queries[i]);
        }
        if (count > 0 && s % 4 == 1)
        {
            answers[0].normal[0] = 0x1p-140f;
            answers[0].normal[1] = 0x1p-141f;
            answers[0].normal[2] = 0.0f;
        }
        r = mvfxInstance_AnswerSceneQueries(context, instance, 1, answers, count);
        PUT(out, r);
    }
    mvfxLayoutId layout = Layout(out, context);
    Particles(out, context, instance, 0, layout);
    Particles(out, context, instance, 1, layout);
    mvfxDestroyContext(context);
}

// A smoke gas stirred by turbulence under a moving instance, sampled, read
// and read as bytes, placed; a surface wrapped round a disc of particles
// and meshed.
static void Gases(Out* out)
{
    mvfxContext* context = NewContext(out);
    static const mvfxGasSource source = {.shape = mvfx_gasSphere,
                                         .center = {0.1f, -0.5f, 0.0f},
                                         .radius = 0.3f,
                                         .density = 1.3f,
                                         .temperature = 3.7f};
    static const mvfxForce stir = {.kind = mvfx_forceTurbulence,
                                   .strength = 1.7f,
                                   .noise = {.frequency = 1.3f,
                                             .octaves = 2,
                                             .lacunarity = 2.0f,
                                             .gain = 0.5f,
                                             .scroll = {0.2f, 0.0f, 0.1f},
                                             .seed = 3}};
    mvfxGasDef gases[2] = {mvfxDefaultGasDef(), mvfxDefaultGasDef()};
    gases[0].size[0] = 16;
    gases[0].size[1] = 16;
    gases[0].cellSize = 0.1f;
    gases[0].sources = &source;
    gases[0].sourceCount = 1;
    gases[0].forces = &stir;
    gases[0].forceCount = 1;
    gases[0].heatLift = 1.1f;
    gases[1].kind = mvfx_gasSurface;
    gases[1].surface.emitter = 0;
    gases[1].size[0] = 24;
    gases[1].size[1] = 24;
    gases[1].cellSize = 0.1f;
    // A reach times a radius that rounds: a cleared surface's every cell.
    gases[1].surface.radius = 0.137f;
    gases[1].surface.reach = 2.3f;
    static const mvfxBurst burst = {
        .minCount = 200, .maxCount = 200, .cycles = 1, .probability = 1.0f};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.capacity = 200;
    emitter.rate = 0.0f;
    emitter.bursts = &burst;
    emitter.burstCount = 1;
    emitter.lifetime = mvfxRangeSource(10.0f, 10.0f);
    emitter.shape.kind = mvfx_shapeCircle;
    emitter.shape.radius = 0.7f;
    emitter.shape.thickness = 1.0f;
    mvfxInstanceId instance = NewInstance(out, context, &emitter, 1, gases, 2, NULL, 0, false);
    for (int32_t s = 0; s < 8; ++s)
    {
        Move(out, context, instance, s);
        Step(out, context);
    }
    static const double points[9] = {0.3, -0.1, 0.7, 0.4, 0.2, 0.6, 0.1, 0.5, 0.8};
    float sampled[3] = {0.0f, 0.0f, 0.0f};
    mvfxResult r =
        mvfxInstance_SampleGas(context, instance, 0, mvfx_gasDensity, points, 3, sampled);
    PUT(out, r);
    PUT(out, sampled);
    r = mvfxInstance_SampleGas(context, instance, 0, mvfx_gasVelocityY, points, 3, sampled);
    PUT(out, r);
    PUT(out, sampled);
    static float cells[16 * 16];
    r = mvfxInstance_ReadGas(context, instance, 0, mvfx_gasVelocityX, cells, 16 * 16);
    PUT(out, r);
    PUT(out, cells);
    r = mvfxInstance_ReadGas(context, instance, 0, mvfx_gasTemperature, cells, 16 * 16);
    PUT(out, r);
    PUT(out, cells);
    static uint16_t bytes[16 * 16];
    const mvfxGasRead read = {.field = mvfx_gasVelocityY,
                              .format = mvfx_formatUnorm16,
                              .size = {16, 16, 1},
                              .low = -0.7f,
                              .high = 0.7f};
    r = mvfxInstance_ReadGasAs(context, instance, 0, &read, bytes, (int64_t)sizeof bytes);
    PUT(out, r);
    PUT(out, bytes);
    mvfxGasInfo info;
    memset(&info, 0, sizeof info);
    r = mvfxInstance_GetGasInfo(context, instance, 0, &info);
    PUT(out, r);
    PUT(out, info);
    static uint32_t scratch[8192];
    static float positions[1024][3];
    static float normals[1024][3];
    static uint32_t indices[2048];
    memset(positions, 0, sizeof positions);
    memset(normals, 0, sizeof normals);
    const mvfxSurfaceMeshBuffers buffers = {.positions = &positions[0][0],
                                            .normals = &normals[0][0],
                                            .vertexCapacity = 1024,
                                            .indices = indices,
                                            .indexCapacity = 2048,
                                            .scratch = scratch,
                                            .scratchBytes = sizeof scratch};
    const double origin[3] = {0.3, -0.1, 0.0};
    int64_t counts[2] = {0, 0};
    r = mvfxInstance_WriteSurfaceMesh(context, instance, 1, origin, 0.013f, &buffers, &counts[0],
                                      &counts[1]);
    PUT(out, r);
    PUT(out, counts);
    PUT(out, positions);
    PUT(out, normals);
    // Cleared by a stop, then by a restart: the surface holds its reach.
    static float distances[24 * 24];
    for (int32_t k = 0; k < 2; ++k)
    {
        r = k == 0 ? mvfxInstance_Stop(context, instance, mvfx_stopAndClear)
                   : mvfxInstance_Restart(context, instance);
        PUT(out, r);
        memset(distances, 0, sizeof distances);
        r = mvfxInstance_ReadGas(context, instance, 1, mvfx_gasDistance, distances, 24 * 24);
        PUT(out, r);
        PUT(out, distances);
    }
    mvfxDestroyContext(context);
}

// A water surface pushed by the host, its waves sampled, read and placed.
static void Waters(Out* out)
{
    mvfxContext* context = NewContext(out);
    mvfxWaterDef water = mvfxDefaultWaterDef();
    water.size[0] = 24;
    water.size[1] = 24;
    water.cellSize = 0.1f;
    water.waveSpeed = 0.9f;
    water.space = mvfx_gasLocal;
    mvfxInstanceId instance = NewInstance(out, context, NULL, 0, NULL, 0, &water, 1, false);
    const mvfxWaterPush stones[2] = {{.point = {0.37, 0.0, 0.71}, .radius = 0.31f, .depth = 0.07f},
                                     {.point = {0.1, 0.0, 0.5}, .radius = 0.2f, .depth = 0.03f}};
    mvfxResult r = mvfxInstance_PushWater(context, instance, 0, stones, 2);
    PUT(out, r);
    // A push of a subnormal radius, which is above 0.
    const mvfxWaterPush grain = {.point = {0.2, 0.0, 0.3}, .radius = 0x1p-140f, .depth = 0.01f};
    r = mvfxInstance_PushWater(context, instance, 0, &grain, 1);
    PUT(out, r);
    for (int32_t s = 0; s < 10; ++s)
    {
        Step(out, context);
    }
    static const double points[6] = {0.37, 1.0, 0.71, 0.9, 1.0, 0.2};
    mvfxWaterSample samples[2];
    memset(samples, 0, sizeof samples);
    r = mvfxInstance_SampleWater(context, instance, 0, points, 2, samples);
    PUT(out, r);
    PUT(out, samples);
    static uint16_t heights[24 * 24];
    const mvfxWaterRead read = {
        .field = mvfx_waterHeight, .format = mvfx_formatFloat16, .size = {24, 24}};
    r = mvfxInstance_ReadWater(context, instance, 0, &read, heights, (int64_t)sizeof heights);
    PUT(out, r);
    PUT(out, heights);
    const mvfxWaterRead scaled = {.field = mvfx_waterHeight,
                                  .format = mvfx_formatUnorm16,
                                  .size = {24, 24},
                                  .low = -0.03f,
                                  .high = 0.03f};
    r = mvfxInstance_ReadWater(context, instance, 0, &scaled, heights, (int64_t)sizeof heights);
    PUT(out, r);
    PUT(out, heights);
    mvfxWaterInfo info;
    memset(&info, 0, sizeof info);
    r = mvfxInstance_GetWaterInfo(context, instance, 0, &info);
    PUT(out, r);
    PUT(out, info);
    mvfxDestroyContext(context);
}

// An emitter's plan: what its arrays hold, then the plan with their
// addresses, which differ between runs, taken out.
static void PutEmitterPlan(Out* out, const mvfxEmitterPlan* plan)
{
    static mvfxEmitterPlan copy;
    copy = *plan;
    Put(out, copy.births, (size_t)copy.birthCount * sizeof copy.births[0]);
    Put(out, copy.hostBirths, (size_t)copy.hostBirthCount * sizeof copy.hostBirths[0]);
    Put(out, copy.attachFrames, (size_t)copy.attachFrameCount * 7 * sizeof(float));
    copy.births = NULL;
    copy.hostBirths = NULL;
    copy.attachFrames = NULL;
    PUT(out, copy);
}

// A planned instance's plans: an emitter's, a liquid gas's and shallow
// water's, whole and a step at a time; speeds reported (a negative
// subnormal among them, which the setters refuse) and counts.
static void Plans(Out* out)
{
    mvfxContext* context = NewContext(out);
    mvfxEmitterDef emitters[2] = {Thrown(), mvfxDefaultEmitterDef()};
    emitters[1].capacity = 128;
    emitters[1].rate = 120.0f;
    emitters[1].lifetime = mvfxRangeSource(1000.0f, 1000.0f);
    emitters[1].velocity.speed = (mvfxRange){1.0f, 2.0f};
    emitters[1].motion.gravity[1] = -9.81f;
    emitters[1].liquid.enable = true;
    emitters[1].liquid.solver = mvfx_liquidGrid;
    emitters[1].liquid.gas = 0;
    emitters[1].liquid.spacing = 0.05f;
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.kind = mvfx_gasLiquid;
    gas.size[0] = 16;
    gas.size[1] = 16;
    gas.size[2] = 1;
    gas.cellSize = 0.1f;
    gas.closedWalls = 63;
    gas.courant = 0.3f;
    mvfxWaterDef water = mvfxDefaultWaterDef();
    water.model = mvfx_waterShallow;
    water.size[0] = 16;
    water.size[1] = 16;
    water.cellSize = 0.1f;
    mvfxInstanceId instance = NewInstance(out, context, emitters, 2, &gas, 1, &water, 1, true);
    static const float speeds[3] = {2.7f, -0x1p-140f, 0.37f};
    for (int32_t s = 0; s < 6; ++s)
    {
        int64_t tick = (int64_t)s * STEP_TICKS;
        mvfxResult r = mvfxInstance_SetLiquidSpeed(context, instance, 0, tick, speeds[s % 3]);
        PUT(out, r);
        r = mvfxInstance_SetWaterSpeed(context, instance, 0, tick, speeds[s % 3]);
        PUT(out, r);
        static const double volumes[3] = {0.1, 0.2, 0x1p-1060};
        r = mvfxInstance_SetWaterCounts(context, instance, 0, tick, 0.3f, 2, volumes[s % 3]);
        PUT(out, r);
        Move(out, context, instance, s);
        Step(out, context);
        int32_t steps = 0;
        r = mvfxInstance_GetPlanSteps(context, instance, &steps);
        PUT(out, r);
        PUT(out, steps);
        static mvfxEmitterPlan emitterPlan;
        static mvfxGasPlan gasPlan;
        static mvfxWaterPlan waterPlan;
        for (int32_t k = 0; k < steps; ++k)
        {
            memset(&emitterPlan, 0, sizeof emitterPlan);
            memset(&gasPlan, 0, sizeof gasPlan);
            memset(&waterPlan, 0, sizeof waterPlan);
            r = mvfxInstance_GetEmitterStepPlan(context, instance, 0, k, &emitterPlan);
            PUT(out, r);
            PutEmitterPlan(out, &emitterPlan);
            r = mvfxInstance_GetGasStepPlan(context, instance, 0, k, &gasPlan);
            PUT(out, r);
            PUT(out, gasPlan);
            r = mvfxInstance_GetWaterStepPlan(context, instance, 0, k, &waterPlan);
            PUT(out, r);
            PUT(out, waterPlan);
        }
        memset(&emitterPlan, 0, sizeof emitterPlan);
        memset(&gasPlan, 0, sizeof gasPlan);
        memset(&waterPlan, 0, sizeof waterPlan);
        r = mvfxInstance_GetEmitterPlan(context, instance, 1, &emitterPlan);
        PUT(out, r);
        PutEmitterPlan(out, &emitterPlan);
        r = mvfxInstance_GetGasPlan(context, instance, 0, &gasPlan);
        PUT(out, r);
        PUT(out, gasPlan);
        r = mvfxInstance_GetWaterPlan(context, instance, 0, &waterPlan);
        PUT(out, r);
        PUT(out, waterPlan);
    }
    mvfxWaterInfo info;
    memset(&info, 0, sizeof info);
    mvfxResult r = mvfxInstance_GetWaterInfo(context, instance, 0, &info);
    PUT(out, r);
    PUT(out, info);
    // Without a liquid each step is one plan, read whole: an emitter and a
    // smoke gas each stirred by a turbulence, its noise taken at the plan's
    // start, and the shallow water.
    static const mvfxForce stir = {.kind = mvfx_forceTurbulence,
                                   .strength = 1.3f,
                                   .noise = {.frequency = 0.73f,
                                             .octaves = 3,
                                             .lacunarity = 2.0f,
                                             .gain = 0.5f,
                                             .scroll = {0.37f, -0.11f, 0.23f},
                                             .seed = 5}};
    emitters[0].forces = &stir;
    emitters[0].forceCount = 1;
    mvfxGasDef smoke = mvfxDefaultGasDef();
    smoke.size[0] = 8;
    smoke.size[1] = 8;
    smoke.cellSize = 0.1f;
    smoke.forces = &stir;
    smoke.forceCount = 1;
    mvfxInstanceId whole = NewInstance(out, context, emitters, 1, &smoke, 1, &water, 1, true);
    for (int32_t s = 0; s < 4; ++s)
    {
        Move(out, context, whole, s);
        Step(out, context);
        static mvfxEmitterPlan emitterPlan;
        static mvfxGasPlan gasPlan;
        static mvfxWaterPlan waterPlan;
        memset(&emitterPlan, 0, sizeof emitterPlan);
        memset(&gasPlan, 0, sizeof gasPlan);
        memset(&waterPlan, 0, sizeof waterPlan);
        r = mvfxInstance_GetEmitterPlan(context, whole, 0, &emitterPlan);
        PUT(out, r);
        PutEmitterPlan(out, &emitterPlan);
        r = mvfxInstance_GetGasPlan(context, whole, 0, &gasPlan);
        PUT(out, r);
        PUT(out, gasPlan);
        r = mvfxInstance_GetWaterPlan(context, whole, 0, &waterPlan);
        PUT(out, r);
        PUT(out, waterPlan);
        r = mvfxInstance_GetEmitterStepPlan(context, whole, 0, 0, &emitterPlan);
        PUT(out, r);
        PutEmitterPlan(out, &emitterPlan);
        r = mvfxInstance_GetGasStepPlan(context, whole, 0, 0, &gasPlan);
        PUT(out, r);
        PUT(out, gasPlan);
    }
    mvfxDestroyContext(context);
}

typedef void Case(Out* out);

static void Run(const char* name, Case* run, TestFloatSetting start)
{
    static Out plain;
    static Out hostile;
    memset(&plain, 0, sizeof plain);
    run(&plain);
    bool same = !plain.full && plain.at > 0;
    bool kept = true;
    for (int h = 0; h < TestFloatHostileCount(); ++h)
    {
        TestFloatSetting host = TestFloatHostileAt(h);
        memset(&hostile, 0, sizeof hostile);
        TestFloatSet(host);
        run(&hostile);
        kept = kept && TestFloatSame(TestFloatGet(), host);
        TestFloatSet(start);
        same = same && hostile.at == plain.at && memcmp(hostile.bytes, plain.bytes, plain.at) == 0;
    }
    if (!same || !kept)
    {
        printf("  %s: %s%s\n", name, same ? "" : "bits differ ", kept ? "" : "setting not kept");
    }
    CHECK(same, "the same bits under every host");
    CHECK(kept, "and the host's setting as it set it");
}

int main(void)
{
    TestFloatSetting start = TestFloatGet();
    Run("constants", Constants, start);
    Run("throws", Throws, start);
    Run("files", Files, start);
    Run("meshes", Meshes, start);
    Run("grids", Grids, start);
    Run("strips", Strips, start);
    Run("scenes", Scenes, start);
    Run("gases", Gases, start);
    Run("waters", Waters, start);
    Run("plans", Plans, start);
    CHECK(TestFloatSame(TestFloatGet(), start), "the thread's own setting at the end");
    return s_failures == 0 ? 0 : 1;
}
