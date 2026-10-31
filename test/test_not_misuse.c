// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Refusals that are not misuse. Every call that takes a context, given a
// NULL one with live ids, refuses it with mvfx_errorInvalid, never
// mvfx_errorStale, and counts no misuse on the live context; it clears
// its outputs as its header says: single values, and the caller arrays
// whose size it knows without the context, over their whole capacity,
// while an array its layout sizes is left as it was. The refusals the
// library's own state causes count none either: a full slot table, a
// full host queue, a mesh with no hull a rigid body takes, an effect
// file's fault.

#include "test_harness.h"

#include "maul-vfx/file.h"
#include "maul-vfx/grid.h"
#include "maul-vfx/output.h"
#include "maul-vfx/plan.h"

#include <string.h>

enum
{
    BYTES = 4096,
    MOST = 4,
};

// The live objects: an effect of one emitter, its instance, a mesh, a grid
// and a layout.
typedef struct Scene
{
    mvfxContext* context;
    mvfxEffectId effect;
    mvfxInstanceId instance;
    mvfxMeshId mesh;
    mvfxGridId grid;
    mvfxLayoutId layout;
} Scene;

static const float s_tetrahedron[12] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
static const float s_values[8] = {0, 1, 2, 3, 4, 5, 6, 7};

// The outputs every call is given, filled with 0xEE before each.
static _Alignas(16) uint8_t s_buffer[BYTES];
static _Alignas(16) uint8_t s_scratch[BYTES];
static uint32_t s_indices[MOST];
static float s_floats[3 * MOST];
static float s_normals[3 * MOST];
static mvfxLight s_lights[MOST];
static mvfxDrawItem s_items[MOST];
static mvfxRibbon s_ribbons[MOST];
static mvfxWaterSample s_samples[MOST];

static bool Filled(const void* p, size_t n, uint8_t byte)
{
    const uint8_t* bytes = p;
    for (size_t i = 0; i < n; ++i)
    {
        if (bytes[i] != byte)
        {
            return false;
        }
    }
    return true;
}

static bool Zero(const void* p, size_t n)
{
    return Filled(p, n, 0);
}

static void Spoil(void)
{
    memset(s_buffer, 0xEE, sizeof s_buffer);
    memset(s_scratch, 0xEE, sizeof s_scratch);
    memset(s_indices, 0xEE, sizeof s_indices);
    memset(s_floats, 0xEE, sizeof s_floats);
    memset(s_normals, 0xEE, sizeof s_normals);
    memset(s_lights, 0xEE, sizeof s_lights);
    memset(s_items, 0xEE, sizeof s_items);
    memset(s_ribbons, 0xEE, sizeof s_ribbons);
    memset(s_samples, 0xEE, sizeof s_samples);
}

static Scene NewScene(void)
{
    Scene scene = {0};
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &scene.context) == mvfx_success, "context");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    CHECK(mvfxCreateEffect(scene.context, &effectDef, &scene.effect, NULL) == mvfx_success,
          "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = scene.effect;
    CHECK(mvfxCreateInstance(scene.context, &instanceDef, &scene.instance) == mvfx_success,
          "instance");
    mvfxMeshDef meshDef = mvfxDefaultMeshDef();
    meshDef.positions = s_tetrahedron;
    meshDef.vertexCount = 4;
    CHECK(mvfxCreateMesh(scene.context, &meshDef, &scene.mesh) == mvfx_success, "mesh");
    mvfxGridDef gridDef = mvfxDefaultGridDef();
    gridDef.size[0] = 2;
    gridDef.size[1] = 2;
    gridDef.size[2] = 2;
    gridDef.components = 1;
    gridDef.values = s_values;
    CHECK(mvfxCreateGrid(scene.context, &gridDef, &scene.grid) == mvfx_success, "grid");
    static const mvfxLayoutField position = {
        .field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3};
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = &position;
    layoutDef.fieldCount = 1;
    layoutDef.stride = 16;
    CHECK(mvfxCreateLayout(scene.context, &layoutDef, &scene.layout) == mvfx_success, "layout");
    CHECK(mvfxContext_StepTicks(scene.context, MVFX_TICKS_PER_SECOND / 8) == mvfx_success,
          "a step");
    return scene;
}

static bool IsNull(const void* id, size_t bytes)
{
    return Zero(id, bytes);
}

// The context's own calls and the creations: each refused, its id the null
// id; the value calls answer 0 or false.
static void TestCreations(const Scene* s)
{
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    mvfxEffectId effect = s->effect;
    mvfxEffectRefusal refusal;
    memset(&refusal, 0xEE, sizeof refusal);
    CHECK(mvfxCreateEffect(NULL, &effectDef, &effect, &refusal) == mvfx_errorInvalid &&
              IsNull(&effect, sizeof effect) && refusal.result == mvfx_errorInvalid,
          "mvfxCreateEffect");
    static _Alignas(16) const uint8_t bytes[16] = {0};
    effect = s->effect;
    mvfxFileResult file =
        mvfxCreateEffectFromBytes(NULL, bytes, sizeof bytes, NULL, 0, &effect, NULL);
    CHECK(file.result == mvfx_errorInvalid && IsNull(&effect, sizeof effect),
          "mvfxCreateEffectFromBytes");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = s->effect;
    mvfxInstanceId instance = s->instance;
    CHECK(mvfxCreateInstance(NULL, &instanceDef, &instance) == mvfx_errorInvalid &&
              IsNull(&instance, sizeof instance),
          "mvfxCreateInstance");
    mvfxMeshDef meshDef = mvfxDefaultMeshDef();
    meshDef.positions = s_tetrahedron;
    meshDef.vertexCount = 4;
    mvfxMeshId mesh = s->mesh;
    CHECK(mvfxCreateMesh(NULL, &meshDef, &mesh) == mvfx_errorInvalid && IsNull(&mesh, sizeof mesh),
          "mvfxCreateMesh");
    mvfxGridDef gridDef = mvfxDefaultGridDef();
    gridDef.size[0] = 2;
    gridDef.size[1] = 2;
    gridDef.size[2] = 2;
    gridDef.values = s_values;
    mvfxGridId grid = s->grid;
    CHECK(mvfxCreateGrid(NULL, &gridDef, &grid) == mvfx_errorInvalid && IsNull(&grid, sizeof grid),
          "mvfxCreateGrid");
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    mvfxLayoutId layout = s->layout;
    CHECK(mvfxCreateLayout(NULL, &layoutDef, &layout) == mvfx_errorInvalid &&
              IsNull(&layout, sizeof layout),
          "mvfxCreateLayout");
    CHECK(mvfxDestroyEffect(NULL, s->effect) == mvfx_errorInvalid, "mvfxDestroyEffect");
    CHECK(mvfxDestroyInstance(NULL, s->instance) == mvfx_errorInvalid, "mvfxDestroyInstance");
    CHECK(mvfxDestroyMesh(NULL, s->mesh) == mvfx_errorInvalid, "mvfxDestroyMesh");
    CHECK(mvfxDestroyGrid(NULL, s->grid) == mvfx_errorInvalid, "mvfxDestroyGrid");
    CHECK(mvfxDestroyLayout(NULL, s->layout) == mvfx_errorInvalid, "mvfxDestroyLayout");
    mvfxDestroyContext(NULL);
    CHECK(mvfxContext_GetMisuse(NULL) == 0, "mvfxContext_GetMisuse");
    CHECK(mvfxContext_GetParticles(NULL) == 0, "mvfxContext_GetParticles");
    CHECK(mvfxContext_Step(NULL, 0.125) == mvfx_errorInvalid, "mvfxContext_Step");
    CHECK(mvfxContext_StepTicks(NULL, 1) == mvfx_errorInvalid, "mvfxContext_StepTicks");
    CHECK(!mvfxEffect_IsValid(NULL, s->effect) && !mvfxInstance_IsValid(NULL, s->instance) &&
              !mvfxMesh_IsValid(NULL, s->mesh) && !mvfxGrid_IsValid(NULL, s->grid) &&
              !mvfxLayout_IsValid(NULL, s->layout) && !mvfxInstance_IsFinished(NULL, s->instance),
          "the validity queries answer false");
}

// The objects' own calls: grids, meshes, layouts.
static void TestObjects(const Scene* s)
{
    mvfxGridView gridView;
    memset(&gridView, 0xEE, sizeof gridView);
    CHECK(mvfxGrid_GetView(NULL, s->grid, &gridView) == mvfx_errorInvalid &&
              Zero(&gridView, sizeof gridView),
          "mvfxGrid_GetView");
    CHECK(mvfxGrid_SetValues(NULL, s->grid, s_values, 8) == mvfx_errorInvalid,
          "mvfxGrid_SetValues");
    static const int32_t first[3] = {0, 0, 0};
    static const int32_t size[3] = {1, 1, 1};
    CHECK(mvfxGrid_SetValuesIn(NULL, s->grid, first, size, s_values, 1) == mvfx_errorInvalid,
          "mvfxGrid_SetValuesIn");
    mvfxMeshView meshView;
    memset(&meshView, 0xEE, sizeof meshView);
    CHECK(mvfxMesh_GetView(NULL, s->mesh, &meshView) == mvfx_errorInvalid &&
              Zero(&meshView, sizeof meshView),
          "mvfxMesh_GetView");
    CHECK(mvfxMesh_SetPositions(NULL, s->mesh, s_tetrahedron, NULL, 4) == mvfx_errorInvalid,
          "mvfxMesh_SetPositions");
    bool hull = true;
    float center[3] = {1, 1, 1};
    CHECK(mvfxMesh_GetHullCenter(NULL, s->mesh, &hull, center) == mvfx_errorInvalid && !hull &&
              Zero(center, sizeof center),
          "mvfxMesh_GetHullCenter");
    int32_t count = 7;
    CHECK(mvfxMesh_GetHullVertexCount(NULL, s->mesh, &count) == mvfx_errorInvalid && count == 0,
          "mvfxMesh_GetHullVertexCount");
    mvfxLayoutView layoutView;
    memset(&layoutView, 0xEE, sizeof layoutView);
    CHECK(mvfxLayout_GetView(NULL, s->layout, &layoutView) == mvfx_errorInvalid &&
              Zero(&layoutView, sizeof layoutView),
          "mvfxLayout_GetView");
}

// An instance's own calls.
static void TestInstances(const Scene* s)
{
    mvfxContext* none = NULL;
    mvfxInstanceId i = s->instance;
    static const double origin[3] = {0, 0, 0};
    static const float rotation[4] = {0, 0, 0, 1};
    CHECK(mvfxInstance_SetTransform(none, i, origin, rotation, 1.0f, false) == mvfx_errorInvalid,
          "mvfxInstance_SetTransform");
    CHECK(mvfxInstance_SetVisible(none, i, false) == mvfx_errorInvalid, "mvfxInstance_SetVisible");
    CHECK(mvfxInstance_SetOffscreen(none, i, mvfx_offscreenPause, 0) == mvfx_errorInvalid,
          "mvfxInstance_SetOffscreen");
    static const double place[3] = {0, 0, 0};
    mvfxSpawnRequest request = {.count = 1, .positions = place};
    CHECK(mvfxInstance_Spawn(none, i, 0, &request) == mvfx_errorInvalid, "mvfxInstance_Spawn");
    CHECK(mvfxInstance_SetAttachFrames(none, i, 0, NULL, 0) == mvfx_errorInvalid,
          "mvfxInstance_SetAttachFrames");
    double remaining = 5.0;
    CHECK(mvfxInstance_Prewarm(none, i, 1.0, 0.1, 4, &remaining) == mvfx_errorInvalid,
          "mvfxInstance_Prewarm");
    CHECK(mvfxInstance_SetColliders(none, i, NULL, 0) == mvfx_errorInvalid,
          "mvfxInstance_SetColliders");
    const mvfxSceneQuery* queries = (const mvfxSceneQuery*)s_buffer;
    int32_t count = 7;
    CHECK(mvfxInstance_GetSceneQueries(none, i, 0, &queries, &count) == mvfx_errorInvalid &&
              queries == NULL && count == 0,
          "mvfxInstance_GetSceneQueries");
    CHECK(mvfxInstance_AnswerSceneQueries(none, i, 0, NULL, 0) == mvfx_errorInvalid,
          "mvfxInstance_AnswerSceneQueries");
    CHECK(mvfxInstance_SetDepthImage(none, i, NULL) == mvfx_errorInvalid,
          "mvfxInstance_SetDepthImage");
    static const float center[3] = {0, 0, 0};
    CHECK(mvfxInstance_WakeBodies(none, i, 0, center, 1.0f) == mvfx_errorInvalid,
          "mvfxInstance_WakeBodies");
    const mvfxEvent* events = (const mvfxEvent*)s_buffer;
    count = 7;
    CHECK(mvfxInstance_GetEvents(none, i, 0, &events, &count) == mvfx_errorInvalid &&
              events == NULL && count == 0,
          "mvfxInstance_GetEvents");
    CHECK(mvfxInstance_SetEmitterMesh(none, i, 0, s->mesh) == mvfx_errorInvalid,
          "mvfxInstance_SetEmitterMesh");
    static const float value[4] = {1, 1, 1, 1};
    CHECK(mvfxInstance_SetParameter(none, i, 0, value) == mvfx_errorInvalid,
          "mvfxInstance_SetParameter");
    float read[4] = {1, 1, 1, 1};
    CHECK(mvfxInstance_GetParameter(none, i, 0, read) == mvfx_errorInvalid &&
              Zero(read, sizeof read),
          "mvfxInstance_GetParameter");
    mvfxEmitterStats stats;
    memset(&stats, 0xEE, sizeof stats);
    CHECK(mvfxInstance_GetEmitterStats(none, i, 0, &stats) == mvfx_errorInvalid &&
              Zero(&stats, sizeof stats),
          "mvfxInstance_GetEmitterStats");
    CHECK(mvfxInstance_Stop(none, i, mvfx_stopEmitting) == mvfx_errorInvalid, "mvfxInstance_Stop");
    CHECK(mvfxInstance_Restart(none, i) == mvfx_errorInvalid, "mvfxInstance_Restart");
    int32_t level = 7;
    CHECK(mvfxInstance_GetQuality(none, i, &level) == mvfx_errorInvalid && level == 0,
          "mvfxInstance_GetQuality");
    CHECK(mvfxInstance_BreakRibbons(none, i, 0) == mvfx_errorInvalid, "mvfxInstance_BreakRibbons");
    mvfxGasInfo gas;
    memset(&gas, 0xEE, sizeof gas);
    CHECK(mvfxInstance_GetGasInfo(none, i, 0, &gas) == mvfx_errorInvalid && Zero(&gas, sizeof gas),
          "mvfxInstance_GetGasInfo");
    Spoil();
    CHECK(mvfxInstance_ReadGas(none, i, 0, mvfx_gasDensity, s_floats, MOST) == mvfx_errorInvalid &&
              Zero(s_floats, MOST * sizeof(float)),
          "mvfxInstance_ReadGas");
    mvfxWaterPush push = {0};
    CHECK(mvfxInstance_PushWater(none, i, 0, &push, 1) == mvfx_errorInvalid,
          "mvfxInstance_PushWater");
    mvfxWaterInfo water;
    memset(&water, 0xEE, sizeof water);
    CHECK(mvfxInstance_GetWaterInfo(none, i, 0, &water) == mvfx_errorInvalid &&
              Zero(&water, sizeof water),
          "mvfxInstance_GetWaterInfo");
}

// The outputs: counts 0, the arrays whose size the call knows cleared over
// their capacity, an array the layout's stride sizes left as it was.
static void TestOutputs(const Scene* s)
{
    mvfxContext* none = NULL;
    mvfxInstanceId i = s->instance;
    mvfxView view = {0};
    view.forward[2] = -1.0f;
    view.up[1] = 1.0f;
    int64_t bytes = 7;
    CHECK(mvfxInstance_GetSortScratchBytes(none, i, 0, &bytes) == mvfx_errorInvalid && bytes == 0,
          "mvfxInstance_GetSortScratchBytes");
    Spoil();
    int64_t count = 7;
    CHECK(mvfxInstance_WriteParticles(none, i, 0, s->layout, &view, s_scratch, BYTES, s_buffer,
                                      MOST, &count) == mvfx_errorInvalid &&
              count == 0 && Filled(s_buffer, sizeof s_buffer, 0xEE),
          "mvfxInstance_WriteParticles");
    Spoil();
    mvfxRibbonBuffers ribbons = {.points = s_buffer,
                                 .pointCapacity = MOST,
                                 .ribbons = s_ribbons,
                                 .ribbonCapacity = MOST,
                                 .scratch = s_scratch,
                                 .scratchBytes = BYTES};
    int64_t points = 7;
    count = 7;
    CHECK(mvfxInstance_WriteRibbons(none, i, 0, s->layout, &view, &ribbons, &points, &count) ==
                  mvfx_errorInvalid &&
              points == 0 && count == 0 && Zero(s_ribbons, sizeof s_ribbons) &&
              Filled(s_buffer, sizeof s_buffer, 0xEE),
          "mvfxInstance_WriteRibbons");
    Spoil();
    points = 7;
    count = 7;
    CHECK(mvfxInstance_WritePlexus(none, i, 0, s->layout, &view, &ribbons, &points, &count) ==
                  mvfx_errorInvalid &&
              points == 0 && count == 0 && Zero(s_ribbons, sizeof s_ribbons) &&
              Filled(s_buffer, sizeof s_buffer, 0xEE),
          "mvfxInstance_WritePlexus");
    mvfxExpandBuffers expand = {.vertices = s_buffer,
                                .vertexCapacity = MOST,
                                .indices = s_indices,
                                .indexCapacity = MOST,
                                .scratch = s_scratch,
                                .scratchBytes = BYTES};
    Spoil();
    count = 7;
    CHECK(mvfxInstance_ExpandSprites(none, i, 0, s->layout, &view, &expand, &count) ==
                  mvfx_errorInvalid &&
              count == 0 && Zero(s_indices, sizeof s_indices) &&
              Filled(s_buffer, sizeof s_buffer, 0xEE),
          "mvfxInstance_ExpandSprites");
    int64_t vertices = 7;
    int64_t indices = 7;
    Spoil();
    CHECK(mvfxInstance_ExpandRibbons(none, i, 0, s->layout, &view, &expand, &vertices, &indices) ==
                  mvfx_errorInvalid &&
              vertices == 0 && indices == 0 && Zero(s_indices, sizeof s_indices) &&
              Filled(s_buffer, sizeof s_buffer, 0xEE),
          "mvfxInstance_ExpandRibbons");
    static const mvfxTubeDef tube = {.sides = 4, .caps = false};
    vertices = 7;
    indices = 7;
    Spoil();
    CHECK(mvfxInstance_ExpandTubes(none, i, 0, s->layout, &view, &tube, &expand, &vertices,
                                   &indices) == mvfx_errorInvalid &&
              vertices == 0 && indices == 0 && Zero(s_indices, sizeof s_indices) &&
              Filled(s_buffer, sizeof s_buffer, 0xEE),
          "mvfxInstance_ExpandTubes");
    vertices = 7;
    indices = 7;
    Spoil();
    CHECK(mvfxInstance_ExpandPlexus(none, i, 0, s->layout, &view, &expand, &vertices, &indices) ==
                  mvfx_errorInvalid &&
              vertices == 0 && indices == 0 && Zero(s_indices, sizeof s_indices) &&
              Filled(s_buffer, sizeof s_buffer, 0xEE),
          "mvfxInstance_ExpandPlexus");
    vertices = 7;
    indices = 7;
    Spoil();
    CHECK(mvfxInstance_ExpandSheets(none, i, 0, s->layout, &view, &expand, &vertices, &indices) ==
                  mvfx_errorInvalid &&
              vertices == 0 && indices == 0 && Zero(s_indices, sizeof s_indices) &&
              Filled(s_buffer, sizeof s_buffer, 0xEE),
          "mvfxInstance_ExpandSheets");
    Spoil();
    count = 7;
    CHECK(mvfxInstance_WriteLights(none, i, 0, &view, s_lights, MOST, &count) ==
                  mvfx_errorInvalid &&
              count == 0 && Zero(s_lights, sizeof s_lights),
          "mvfxInstance_WriteLights");
    mvfxBounds bounds;
    memset(&bounds, 0xEE, sizeof bounds);
    CHECK(mvfxInstance_GetBounds(none, i, 0, &bounds) == mvfx_errorInvalid && bounds.empty &&
              Zero(bounds.min, sizeof bounds.min) && Zero(bounds.max, sizeof bounds.max),
          "mvfxInstance_GetBounds");
    mvfxGasRead gasRead = {.field = mvfx_gasDensity,
                           .format = mvfx_formatFloat32,
                           .size = {1, 1, 1},
                           .high = 1.0f,
                           .rowBytes = 4,
                           .layerBytes = 4};
    Spoil();
    CHECK(mvfxInstance_ReadGasAs(none, i, 0, &gasRead, s_buffer, 16) == mvfx_errorInvalid &&
              Zero(s_buffer, 16),
          "mvfxInstance_ReadGasAs");
    static const double where[3 * MOST] = {0};
    Spoil();
    CHECK(mvfxInstance_SampleGas(none, i, 0, mvfx_gasDensity, where, MOST, s_floats) ==
                  mvfx_errorInvalid &&
              Zero(s_floats, MOST * sizeof(float)),
          "mvfxInstance_SampleGas");
    mvfxWaterRead waterRead = {.field = mvfx_waterHeight,
                               .format = mvfx_formatFloat32,
                               .size = {1, 1},
                               .high = 1.0f,
                               .rowBytes = 4};
    Spoil();
    CHECK(mvfxInstance_ReadWater(none, i, 0, &waterRead, s_buffer, 16) == mvfx_errorInvalid &&
              Zero(s_buffer, 16),
          "mvfxInstance_ReadWater");
    Spoil();
    CHECK(mvfxInstance_SampleWater(none, i, 0, where, MOST, s_samples) == mvfx_errorInvalid &&
              Zero(s_samples, sizeof s_samples),
          "mvfxInstance_SampleWater");
    static const double eye[3] = {0, 0, 0};
    mvfxFrustum frustum = {0};
    Spoil();
    count = 7;
    CHECK(mvfxContext_OrderEmitters(none, eye, &frustum, s_items, MOST, &count) ==
                  mvfx_errorInvalid &&
              count == 0 && Zero(s_items, sizeof s_items),
          "mvfxContext_OrderEmitters");
    bytes = 7;
    CHECK(mvfxInstance_GetSurfaceMeshScratchBytes(none, i, 0, &bytes) == mvfx_errorInvalid &&
              bytes == 0,
          "mvfxInstance_GetSurfaceMeshScratchBytes");
    mvfxSurfaceMeshBuffers mesh = {.positions = s_floats,
                                   .normals = s_normals,
                                   .vertexCapacity = MOST,
                                   .indices = s_indices,
                                   .indexCapacity = MOST,
                                   .scratch = s_scratch,
                                   .scratchBytes = BYTES};
    vertices = 7;
    indices = 7;
    Spoil();
    CHECK(mvfxInstance_WriteSurfaceMesh(none, i, 0, eye, 0.5f, &mesh, &vertices, &indices) ==
                  mvfx_errorInvalid &&
              vertices == 0 && indices == 0 && Zero(s_floats, sizeof s_floats) &&
              Zero(s_normals, sizeof s_normals) && Zero(s_indices, sizeof s_indices),
          "mvfxInstance_WriteSurfaceMesh");
}

// The plans and views.
static void TestPlans(const Scene* s)
{
    const mvfxContext* none = NULL;
    mvfxInstanceId i = s->instance;
    mvfxEmitterView emitterView;
    memset(&emitterView, 0xEE, sizeof emitterView);
    CHECK(mvfxEffect_GetEmitterView(none, s->effect, 0, &emitterView) == mvfx_errorInvalid &&
              Zero(&emitterView, sizeof emitterView),
          "mvfxEffect_GetEmitterView");
    mvfxMeshView meshView;
    memset(&meshView, 0xEE, sizeof meshView);
    CHECK(mvfxInstance_GetEmitterMesh(none, i, 0, &meshView) == mvfx_errorInvalid &&
              Zero(&meshView, sizeof meshView),
          "mvfxInstance_GetEmitterMesh");
    int32_t steps = 7;
    CHECK(mvfxInstance_GetPlanSteps(none, i, &steps) == mvfx_errorInvalid && steps == 0,
          "mvfxInstance_GetPlanSteps");
    static mvfxEmitterPlan emitterPlan;
    memset(&emitterPlan, 0xEE, sizeof emitterPlan);
    CHECK(mvfxInstance_GetEmitterStepPlan(none, i, 0, 0, &emitterPlan) == mvfx_errorInvalid &&
              Zero(&emitterPlan, sizeof emitterPlan),
          "mvfxInstance_GetEmitterStepPlan");
    memset(&emitterPlan, 0xEE, sizeof emitterPlan);
    CHECK(mvfxInstance_GetEmitterPlan(none, i, 0, &emitterPlan) == mvfx_errorInvalid &&
              Zero(&emitterPlan, sizeof emitterPlan),
          "mvfxInstance_GetEmitterPlan");
    mvfxGasView gasView;
    memset(&gasView, 0xEE, sizeof gasView);
    CHECK(mvfxEffect_GetGasView(none, s->effect, 0, &gasView) == mvfx_errorInvalid &&
              Zero(&gasView, sizeof gasView),
          "mvfxEffect_GetGasView");
    static mvfxGasPlan gasPlan;
    memset(&gasPlan, 0xEE, sizeof gasPlan);
    CHECK(mvfxInstance_GetGasPlan(none, i, 0, &gasPlan) == mvfx_errorInvalid &&
              Zero(&gasPlan, sizeof gasPlan),
          "mvfxInstance_GetGasPlan");
    memset(&gasPlan, 0xEE, sizeof gasPlan);
    CHECK(mvfxInstance_GetGasStepPlan(none, i, 0, 0, &gasPlan) == mvfx_errorInvalid &&
              Zero(&gasPlan, sizeof gasPlan),
          "mvfxInstance_GetGasStepPlan");
    mvfxWaterView waterView;
    memset(&waterView, 0xEE, sizeof waterView);
    CHECK(mvfxEffect_GetWaterView(none, s->effect, 0, &waterView) == mvfx_errorInvalid &&
              Zero(&waterView, sizeof waterView),
          "mvfxEffect_GetWaterView");
    static mvfxWaterPlan waterPlan;
    memset(&waterPlan, 0xEE, sizeof waterPlan);
    CHECK(mvfxInstance_GetWaterPlan(none, i, 0, &waterPlan) == mvfx_errorInvalid &&
              Zero(&waterPlan, sizeof waterPlan),
          "mvfxInstance_GetWaterPlan");
    memset(&waterPlan, 0xEE, sizeof waterPlan);
    CHECK(mvfxInstance_GetWaterStepPlan(none, i, 0, 0, &waterPlan) == mvfx_errorInvalid &&
              Zero(&waterPlan, sizeof waterPlan),
          "mvfxInstance_GetWaterStepPlan");
    CHECK(mvfxInstance_SetGasHolds(NULL, i, 0, 0, false) == mvfx_errorInvalid,
          "mvfxInstance_SetGasHolds");
    CHECK(mvfxInstance_SetLiquidSpeed(NULL, i, 0, 0, 0.0f) == mvfx_errorInvalid,
          "mvfxInstance_SetLiquidSpeed");
    CHECK(mvfxInstance_SetWaterHolds(NULL, i, 0, 0, false) == mvfx_errorInvalid,
          "mvfxInstance_SetWaterHolds");
    CHECK(mvfxInstance_SetWaterSpeed(NULL, i, 0, 0, 0.0f) == mvfx_errorInvalid,
          "mvfxInstance_SetWaterSpeed");
    CHECK(mvfxInstance_SetWaterCounts(NULL, i, 0, 0, 0.0f, 0, 0.0) == mvfx_errorInvalid,
          "mvfxInstance_SetWaterCounts");
}

static void TestNullContext(void)
{
    Scene scene = NewScene();
    uint64_t misuse = mvfxContext_GetMisuse(scene.context);
    TestCreations(&scene);
    TestObjects(&scene);
    TestInstances(&scene);
    TestOutputs(&scene);
    TestPlans(&scene);
    CHECK(mvfxContext_GetMisuse(scene.context) == misuse, "no misuse counted on the live context");
    CHECK(mvfxEffect_IsValid(scene.context, scene.effect) &&
              mvfxInstance_IsValid(scene.context, scene.instance) &&
              mvfxMesh_IsValid(scene.context, scene.mesh) &&
              mvfxGrid_IsValid(scene.context, scene.grid) &&
              mvfxLayout_IsValid(scene.context, scene.layout),
          "every id still live");
    mvfxDestroyContext(scene.context);
}

// The refusals the library's own state causes, which a program cannot rule
// out ahead of time: each returns its status and counts no misuse.
static void TestStateRefusals(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    contextDef.limits.instances = 1;
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 0.0f;
    emitter.hostSpawnCapacity = 2;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    uint64_t misuse = mvfxContext_GetMisuse(context);

    // A full slot table: the context holds its one instance.
    mvfxInstanceId more = instance;
    CHECK(mvfxCreateInstance(context, &instanceDef, &more) == mvfx_errorCapacity &&
              IsNull(&more, sizeof more) && mvfxContext_GetMisuse(context) == misuse,
          "a full slot table refused, not misuse");

    // A full host queue: two particles fit before the step, a third not.
    static const double places[6] = {0, 0, 0, 1, 0, 0};
    mvfxSpawnRequest two = {.count = 2, .positions = places};
    mvfxSpawnRequest one = {.count = 1, .positions = places};
    CHECK(mvfxInstance_Spawn(context, instance, 0, &two) == mvfx_success &&
              mvfxInstance_Spawn(context, instance, 0, &one) == mvfx_errorCapacity &&
              mvfxContext_GetMisuse(context) == misuse,
          "a full host queue refused, not misuse");

    // A mesh whose vertices span no volume has no hull a rigid body takes.
    static const float flat[12] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0};
    mvfxMeshDef meshDef = mvfxDefaultMeshDef();
    meshDef.positions = flat;
    meshDef.vertexCount = 4;
    mvfxMeshId plane = {0};
    CHECK(mvfxCreateMesh(context, &meshDef, &plane) == mvfx_success, "a flat mesh");
    mvfxEmitterDef rigid = mvfxDefaultEmitterDef();
    rigid.rate = 0.0f;
    rigid.capacity = 4;
    rigid.hostSpawnCapacity = 4;
    rigid.rigid = (mvfxRigidDef){.enable = true,
                                 .shape = mvfx_rigidHull,
                                 .density = 1.0f,
                                 .substeps = 8,
                                 .staticFriction = 0.6f,
                                 .dynamicFriction = 0.4f,
                                 .hull = plane};
    effectDef.emitters = &rigid;
    mvfxEffectId refused = {0};
    CHECK(mvfxCreateEffect(context, &effectDef, &refused, NULL) == mvfx_errorInvalid &&
              IsNull(&refused, sizeof refused) && mvfxContext_GetMisuse(context) == misuse,
          "a mesh with no hull refused for a rigid body, not misuse");

    // An effect file's fault: bytes that are no effect file.
    static _Alignas(16) const uint8_t junk[64] = {'n', 'o', 't', ' ', 'a', ' ', 'f', 'x'};
    refused = effect;
    mvfxFileResult file =
        mvfxCreateEffectFromBytes(context, junk, sizeof junk, NULL, 0, &refused, NULL);
    CHECK(file.result != mvfx_success && IsNull(&refused, sizeof refused) &&
              mvfxContext_GetMisuse(context) == misuse,
          "an effect file's fault refused, not misuse");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestNullContext();
    TestStateRefusals();
    return s_failures == 0 ? 0 : 1;
}
