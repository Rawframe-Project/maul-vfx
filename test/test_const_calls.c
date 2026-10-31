// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A call taking a const context runs no host code: every one, called on a
// live context with live objects and answering, makes no allocation, frees
// nothing, and enqueues and finishes no task.

#include "test_harness.h"

#include "maul-vfx/grid.h"
#include "maul-vfx/output.h"
#include "maul-vfx/plan.h"

#include <stdint.h>
#include <stdlib.h>

enum
{
    MOST = 4,
};

// The host code the context has run.
typedef struct Host
{
    int64_t allocs;
    int64_t frees;
    int64_t enqueues;
    int64_t finishes;
} Host;

static Host s_host;

// Memory aligned by hand (not every C library has aligned_alloc), the
// block's start kept before it.
static void* Alloc(size_t size, size_t alignment, void* context)
{
    (void)context;
    s_host.allocs += 1;
    uint8_t* raw = malloc(size + alignment + sizeof(void*));
    if (raw == NULL)
    {
        return NULL;
    }
    uintptr_t at = ((uintptr_t)(raw + sizeof(void*)) + alignment - 1) & ~(uintptr_t)(alignment - 1);
    ((void**)at)[-1] = raw;
    return (void*)at;
}

static void Free(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    s_host.frees += 1;
    free(((void**)memory)[-1]);
}

// Runs every range at once, and hands back a handle for finishTask.
static void* Enqueue(mvfxTaskFunction* task, int32_t itemCount, int32_t minRange, void* taskContext,
                     void* userContext)
{
    (void)minRange;
    s_host.enqueues += 1;
    task(0, itemCount, taskContext);
    return userContext;
}

static void Finish(void* userTask, void* userContext)
{
    (void)userTask;
    (void)userContext;
    s_host.finishes += 1;
}

static const float s_tetrahedron[12] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
static const float s_values[8] = {0, 1, 2, 3, 4, 5, 6, 7};
static const float s_parameter[4] = {1, 2, 3, 4};

// The live objects: an effect of an emitter spawning from a mesh, reading a
// parameter, a smoke gas, a surface gas over the emitter and a water
// surface; one instance it simulates and one it plans; a grid and a layout.
typedef struct Scene
{
    mvfxContext* context;
    mvfxEffectId effect;
    mvfxInstanceId simulated;
    mvfxInstanceId planned;
    mvfxMeshId mesh;
    mvfxGridId grid;
    mvfxLayoutId layout;
} Scene;

static Scene NewScene(void)
{
    Scene s = {0};
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    contextDef.allocator = (mvfxAllocator){Alloc, Free, NULL};
    contextDef.enqueueTask = Enqueue;
    contextDef.finishTask = Finish;
    contextDef.userTaskContext = &s_host;
    CHECK(mvfxCreateContext(&contextDef, &s.context) == mvfx_success, "context");
    mvfxMeshDef meshDef = mvfxDefaultMeshDef();
    meshDef.positions = s_tetrahedron;
    meshDef.vertexCount = 4;
    CHECK(mvfxCreateMesh(s.context, &meshDef, &s.mesh) == mvfx_success, "mesh");
    mvfxGridDef gridDef = mvfxDefaultGridDef();
    gridDef.size[0] = 2;
    gridDef.size[1] = 2;
    gridDef.size[2] = 2;
    gridDef.components = 1;
    gridDef.values = s_values;
    CHECK(mvfxCreateGrid(s.context, &gridDef, &s.grid) == mvfx_success, "grid");
    static const mvfxLayoutField position = {
        .field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3};
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = &position;
    layoutDef.fieldCount = 1;
    layoutDef.stride = 16;
    CHECK(mvfxCreateLayout(s.context, &layoutDef, &s.layout) == mvfx_success, "layout");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    emitter.rate = 64.0f;
    emitter.capacity = 64;
    emitter.shape.kind = mvfx_shapeMesh;
    emitter.shape.mesh = s.mesh;
    emitter.shape.meshMode = mvfx_meshVertices;
    mvfxGasDef gases[2] = {mvfxDefaultGasDef(), mvfxDefaultGasDef()};
    for (int32_t g = 0; g < 2; ++g)
    {
        gases[g].size[0] = 8;
        gases[g].size[1] = 8;
        gases[g].size[2] = 8;
        gases[g].cellSize = 0.1f;
    }
    gases[1].kind = mvfx_gasSurface;
    gases[1].surface.emitter = 0;
    gases[1].surface.radius = 0.1f;
    gases[1].surface.reach = 3.0f;
    mvfxWaterDef water = mvfxDefaultWaterDef();
    water.size[0] = 8;
    water.size[1] = 8;
    water.cellSize = 0.1f;
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.emitters = &emitter;
    effectDef.emitterCount = 1;
    effectDef.parameters = s_parameter;
    effectDef.parameterCount = 1;
    effectDef.gases = gases;
    effectDef.gasCount = 2;
    effectDef.waters = &water;
    effectDef.waterCount = 1;
    CHECK(mvfxCreateEffect(s.context, &effectDef, &s.effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = s.effect;
    CHECK(mvfxCreateInstance(s.context, &instanceDef, &s.simulated) == mvfx_success,
          "a simulated instance");
    instanceDef.planned = true;
    CHECK(mvfxCreateInstance(s.context, &instanceDef, &s.planned) == mvfx_success,
          "a planned instance");
    CHECK(mvfxContext_StepTicks(s.context, MVFX_TICKS_PER_SECOND / 8) == mvfx_success, "a step");
    CHECK(s_host.allocs > 0 && s_host.enqueues > 0 && s_host.finishes > 0,
          "the setup ran host code");
    return s;
}

static bool Same(const Host* before)
{
    return s_host.allocs == before->allocs && s_host.frees == before->frees &&
           s_host.enqueues == before->enqueues && s_host.finishes == before->finishes;
}

// Each call answers, and the host's code has not run since the last.
#define ANSWERS(call, name)                                                                        \
    do                                                                                             \
    {                                                                                              \
        Host before = s_host;                                                                      \
        bool answered = (call);                                                                    \
        CHECK(answered && Same(&before), name);                                                    \
    } while (0)

static void TestConstCallsRunNoHostCode(void)
{
    Scene s = NewScene();
    const mvfxContext* c = s.context;
    mvfxInstanceId i = s.simulated;
    mvfxInstanceId p = s.planned;
    ANSWERS(mvfxContext_GetMisuse(c) == 0, "mvfxContext_GetMisuse");
    ANSWERS(mvfxContext_GetParticles(c) == 128, "mvfxContext_GetParticles");
    ANSWERS(mvfxEffect_IsValid(c, s.effect), "mvfxEffect_IsValid");
    ANSWERS(mvfxInstance_IsValid(c, i), "mvfxInstance_IsValid");
    ANSWERS(mvfxMesh_IsValid(c, s.mesh), "mvfxMesh_IsValid");
    ANSWERS(mvfxGrid_IsValid(c, s.grid), "mvfxGrid_IsValid");
    ANSWERS(mvfxLayout_IsValid(c, s.layout), "mvfxLayout_IsValid");
    ANSWERS(!mvfxInstance_IsFinished(c, i), "mvfxInstance_IsFinished");
    mvfxGridView gridView;
    ANSWERS(mvfxGrid_GetView(c, s.grid, &gridView) == mvfx_success, "mvfxGrid_GetView");
    mvfxMeshView meshView;
    ANSWERS(mvfxMesh_GetView(c, s.mesh, &meshView) == mvfx_success, "mvfxMesh_GetView");
    bool hull = false;
    float center[3];
    ANSWERS(mvfxMesh_GetHullCenter(c, s.mesh, &hull, center) == mvfx_success && hull,
            "mvfxMesh_GetHullCenter");
    int32_t count = 0;
    ANSWERS(mvfxMesh_GetHullVertexCount(c, s.mesh, &count) == mvfx_success && count == 4,
            "mvfxMesh_GetHullVertexCount");
    mvfxLayoutView layoutView;
    ANSWERS(mvfxLayout_GetView(c, s.layout, &layoutView) == mvfx_success, "mvfxLayout_GetView");
    float value[4];
    ANSWERS(mvfxInstance_GetParameter(c, i, 0, value) == mvfx_success && value[3] == 4.0f,
            "mvfxInstance_GetParameter");
    int32_t level = -1;
    ANSWERS(mvfxInstance_GetQuality(c, i, &level) == mvfx_success && level == 0,
            "mvfxInstance_GetQuality");
    int64_t bytes = -1;
    ANSWERS(mvfxInstance_GetSortScratchBytes(c, i, 0, &bytes) == mvfx_success,
            "mvfxInstance_GetSortScratchBytes");
    ANSWERS(mvfxInstance_GetSurfaceMeshScratchBytes(c, i, 1, &bytes) == mvfx_success && bytes > 0,
            "mvfxInstance_GetSurfaceMeshScratchBytes");
    mvfxGasInfo gasInfo;
    ANSWERS(mvfxInstance_GetGasInfo(c, i, 0, &gasInfo) == mvfx_success, "mvfxInstance_GetGasInfo");
    static float values[8 * 8 * 8];
    ANSWERS(mvfxInstance_ReadGas(c, i, 0, mvfx_gasDensity, values, 8 * 8 * 8) == mvfx_success,
            "mvfxInstance_ReadGas");
    static _Alignas(16) uint8_t read[8 * 8 * 8 * 4];
    mvfxGasRead gasRead = {.field = mvfx_gasDensity,
                           .format = mvfx_formatFloat32,
                           .size = {8, 8, 8},
                           .high = 1.0f,
                           .rowBytes = 8 * 4,
                           .layerBytes = 8 * 8 * 4};
    ANSWERS(mvfxInstance_ReadGasAs(c, i, 0, &gasRead, read, sizeof read) == mvfx_success,
            "mvfxInstance_ReadGasAs");
    static const double points[3 * MOST] = {0};
    float samples[MOST];
    ANSWERS(mvfxInstance_SampleGas(c, i, 0, mvfx_gasDensity, points, MOST, samples) == mvfx_success,
            "mvfxInstance_SampleGas");
    mvfxWaterInfo waterInfo;
    ANSWERS(mvfxInstance_GetWaterInfo(c, i, 0, &waterInfo) == mvfx_success,
            "mvfxInstance_GetWaterInfo");
    mvfxWaterRead waterRead = {.field = mvfx_waterHeight,
                               .format = mvfx_formatFloat32,
                               .size = {8, 8},
                               .high = 1.0f,
                               .rowBytes = 8 * 4};
    ANSWERS(mvfxInstance_ReadWater(c, i, 0, &waterRead, read, sizeof read) == mvfx_success,
            "mvfxInstance_ReadWater");
    mvfxWaterSample waterSamples[MOST];
    ANSWERS(mvfxInstance_SampleWater(c, i, 0, points, MOST, waterSamples) == mvfx_success,
            "mvfxInstance_SampleWater");
    static mvfxEmitterView emitterView;
    ANSWERS(mvfxEffect_GetEmitterView(c, s.effect, 0, &emitterView) == mvfx_success,
            "mvfxEffect_GetEmitterView");
    ANSWERS(mvfxInstance_GetEmitterMesh(c, i, 0, &meshView) == mvfx_success,
            "mvfxInstance_GetEmitterMesh");
    mvfxGasView gasView;
    ANSWERS(mvfxEffect_GetGasView(c, s.effect, 0, &gasView) == mvfx_success,
            "mvfxEffect_GetGasView");
    mvfxWaterView waterView;
    ANSWERS(mvfxEffect_GetWaterView(c, s.effect, 0, &waterView) == mvfx_success,
            "mvfxEffect_GetWaterView");
    int32_t steps = 0;
    ANSWERS(mvfxInstance_GetPlanSteps(c, p, &steps) == mvfx_success && steps == 1,
            "mvfxInstance_GetPlanSteps");
    static mvfxEmitterPlan emitterPlan;
    ANSWERS(mvfxInstance_GetEmitterPlan(c, p, 0, &emitterPlan) == mvfx_success,
            "mvfxInstance_GetEmitterPlan");
    ANSWERS(mvfxInstance_GetEmitterStepPlan(c, p, 0, 0, &emitterPlan) == mvfx_success,
            "mvfxInstance_GetEmitterStepPlan");
    static mvfxGasPlan gasPlan;
    ANSWERS(mvfxInstance_GetGasPlan(c, p, 0, &gasPlan) == mvfx_success, "mvfxInstance_GetGasPlan");
    ANSWERS(mvfxInstance_GetGasStepPlan(c, p, 0, 0, &gasPlan) == mvfx_success,
            "mvfxInstance_GetGasStepPlan");
    static mvfxWaterPlan waterPlan;
    ANSWERS(mvfxInstance_GetWaterPlan(c, p, 0, &waterPlan) == mvfx_success,
            "mvfxInstance_GetWaterPlan");
    ANSWERS(mvfxInstance_GetWaterStepPlan(c, p, 0, 0, &waterPlan) == mvfx_success,
            "mvfxInstance_GetWaterStepPlan");
    mvfxDestroyContext(s.context);
}

int main(void)
{
    TestConstCallsRunNoHostCode();
    return s_failures == 0 ? 0 : 1;
}
