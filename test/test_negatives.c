// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Every index and count a call takes, given -1 and the least integer of
// its type: refused as invalid, never read below an array's start. Where
// -1 names something (every emitter of GetBounds and BreakRibbons), -2
// stands in for it.
// An instance plain and one planned, of an effect with an emitter, a
// parameter, a gas and a water surface, so each index has something to
// be past.

#include "test_harness.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/file.h"
#include "maul-vfx/grid.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/mesh.h"
#include "maul-vfx/output.h"
#include "maul-vfx/plan.h"
#include "maul-vfx/water.h"

#include <stdint.h>
#include <string.h>

static mvfxContext* s_context;
static mvfxEffectId s_effect;
static mvfxInstanceId s_plain;
static mvfxInstanceId s_planned;
static mvfxLayoutId s_layout;
static mvfxMeshId s_mesh;
static mvfxGridId s_grid;

static void Open(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.size[0] = 4;
    gas.size[1] = 4;
    gas.size[2] = 4;
    gas.cellSize = 0.25f;
    mvfxWaterDef water = mvfxDefaultWaterDef();
    water.size[0] = 4;
    water.size[1] = 4;
    water.cellSize = 0.25f;
    static const float parameter[4] = {1, 0, 0, 0};
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    def.parameters = parameter;
    def.parameterCount = 1;
    def.gases = &gas;
    def.gasCount = 1;
    def.waters = &water;
    def.waterCount = 1;
    CHECK(mvfxCreateEffect(s_context, &def, &s_effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = s_effect;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_plain) == mvfx_success, "instance");
    instanceDef.planned = true;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_planned) == mvfx_success,
          "a planned instance");
    static const mvfxLayoutField position = {
        .field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3};
    mvfxLayoutDef layoutDef = mvfxDefaultLayoutDef();
    layoutDef.fields = &position;
    layoutDef.fieldCount = 1;
    layoutDef.stride = 16;
    CHECK(mvfxCreateLayout(s_context, &layoutDef, &s_layout) == mvfx_success, "layout");
    static const float corner[12] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
    mvfxMeshDef meshDef = mvfxDefaultMeshDef();
    meshDef.positions = corner;
    meshDef.vertexCount = 4;
    CHECK(mvfxCreateMesh(s_context, &meshDef, &s_mesh) == mvfx_success, "mesh");
    static const float values[8] = {0};
    mvfxGridDef gridDef = mvfxDefaultGridDef();
    gridDef.size[0] = 2;
    gridDef.size[1] = 2;
    gridDef.size[2] = 2;
    gridDef.components = 1;
    gridDef.values = values;
    CHECK(mvfxCreateGrid(s_context, &gridDef, &s_grid) == mvfx_success, "grid");
    CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success, "a step");
}

// The negatives of each width a call is given.
static const int32_t s_negatives[2] = {-1, INT32_MIN};
static const int64_t s_negatives64[2] = {-1, INT64_MIN};

// How many calls took a negative.
static int32_t s_taken;

static void Refused(mvfxResult result, const char* call, int32_t k)
{
    if (result != mvfx_errorInvalid)
    {
        printf("  %s with %s: %s\n", call, k == 0 ? "-1" : "the least integer",
               mvfxGetResultName(result));
        s_taken += 1;
    }
}

static void TestInstanceIndices(void)
{
    // Emitters, parameters, gases and water surfaces of both instances.
    static const mvfxView view = {.forward = {0, 0, -1}, .up = {0, 1, 0}};
    static const mvfxTubeDef tube = {.sides = 3};
    static const double point[3] = {0, 0, 0};
    static const float zero[3] = {0, 0, 0};
    static unsigned char records[256];
    static unsigned char scratch[4096];
    static uint32_t indices[64];
    static float floats[64];
    const mvfxExpandBuffers expand = {
        .vertices = records, .vertexCapacity = 4, .indices = indices, .indexCapacity = 6};
    const mvfxRibbonBuffers list = {.points = records,
                                    .pointCapacity = 4,
                                    .ribbons = (mvfxRibbon*)scratch,
                                    .ribbonCapacity = 1};
    const mvfxSurfaceMeshBuffers mesh = {.positions = floats,
                                         .vertexCapacity = 4,
                                         .indices = indices,
                                         .indexCapacity = 6,
                                         .scratch = scratch,
                                         .scratchBytes = sizeof scratch};
    const mvfxGasRead gasRead = {.format = mvfx_formatFloat32, .size = {1, 1, 1}, .high = 1.0f};
    const mvfxWaterRead waterRead = {.format = mvfx_formatFloat32, .size = {1, 1}, .high = 1.0f};
    const mvfxSpawnRequest spawn = {.count = 1, .positions = point};
    const mvfxAttachFrame frame = {.rotation = {0, 0, 0, 1}};
    const mvfxSceneAnswer answer = {0};
    const mvfxWaterPush push = {.radius = 0.1f, .depth = 0.1f};
    for (int32_t k = 0; k < 2; ++k)
    {
        const int32_t n = s_negatives[k];
        // -1 names every emitter where the call reads all of them.
        const int32_t all = k == 0 ? -2 : INT32_MIN;
        mvfxInstanceId i = s_plain;
        mvfxInstanceId p = s_planned;
        int64_t a = 0;
        int64_t b = 0;
        int32_t c = 0;
        const mvfxEvent* events = NULL;
        const mvfxSceneQuery* queries = NULL;
        float value[4];
        mvfxEmitterStats stats;
        mvfxBounds bounds;
        mvfxGasInfo gasInfo;
        mvfxWaterInfo waterInfo;
        static mvfxEmitterView emitterView;
        static mvfxMeshView meshView;
        static mvfxEmitterPlan emitterPlan;
        static mvfxGasView gasView;
        static mvfxGasPlan gasPlan;
        static mvfxWaterView waterView;
        static mvfxWaterPlan waterPlan;
        static mvfxWaterSample sample;
        mvfxContext* x = s_context;
        Refused(mvfxInstance_Spawn(x, i, n, &spawn), "Spawn", k);
        Refused(mvfxInstance_SetAttachFrames(x, i, n, &frame, 1), "SetAttachFrames", k);
        Refused(mvfxInstance_GetSceneQueries(x, i, n, &queries, &c), "GetSceneQueries", k);
        Refused(mvfxInstance_AnswerSceneQueries(x, i, n, &answer, 1), "AnswerSceneQueries", k);
        Refused(mvfxInstance_WakeBodies(x, i, n, zero, 1.0f), "WakeBodies", k);
        Refused(mvfxInstance_GetEvents(x, i, n, &events, &c), "GetEvents", k);
        Refused(mvfxInstance_SetEmitterMesh(x, i, n, s_mesh), "SetEmitterMesh", k);
        Refused(mvfxInstance_SetParameter(x, i, n, value), "SetParameter", k);
        Refused(mvfxInstance_GetParameter(x, i, n, value), "GetParameter", k);
        Refused(mvfxInstance_GetEmitterStats(x, i, n, &stats), "GetEmitterStats", k);
        Refused(mvfxInstance_BreakRibbons(x, i, all), "BreakRibbons", k);
        Refused(mvfxInstance_GetGasInfo(x, i, n, &gasInfo), "GetGasInfo", k);
        Refused(mvfxInstance_ReadGas(x, i, n, mvfx_gasDensity, floats, 64), "ReadGas", k);
        Refused(mvfxInstance_PushWater(x, i, n, &push, 1), "PushWater", k);
        Refused(mvfxInstance_GetWaterInfo(x, i, n, &waterInfo), "GetWaterInfo", k);
        Refused(mvfxInstance_GetSortScratchBytes(x, i, n, &a), "GetSortScratchBytes", k);
        Refused(mvfxInstance_WriteParticles(x, i, n, s_layout, &view, scratch, sizeof scratch,
                                            records, 4, &a),
                "WriteParticles", k);
        Refused(mvfxInstance_WriteRibbons(x, i, n, s_layout, &view, &list, &a, &b), "WriteRibbons",
                k);
        Refused(mvfxInstance_ExpandSprites(x, i, n, s_layout, &view, &expand, &a), "ExpandSprites",
                k);
        Refused(mvfxInstance_ExpandRibbons(x, i, n, s_layout, &view, &expand, &a, &b),
                "ExpandRibbons", k);
        Refused(mvfxInstance_ExpandTubes(x, i, n, s_layout, &view, &tube, &expand, &a, &b),
                "ExpandTubes", k);
        Refused(mvfxInstance_WritePlexus(x, i, n, s_layout, &view, &list, &a, &b), "WritePlexus",
                k);
        Refused(mvfxInstance_ExpandPlexus(x, i, n, s_layout, &view, &expand, &a, &b),
                "ExpandPlexus", k);
        Refused(mvfxInstance_ExpandSheets(x, i, n, s_layout, &view, &expand, &a, &b),
                "ExpandSheets", k);
        Refused(mvfxInstance_WriteLights(x, i, n, &view, (mvfxLight*)records, 1, &a), "WriteLights",
                k);
        Refused(mvfxInstance_GetBounds(x, i, all, &bounds), "GetBounds", k);
        Refused(mvfxInstance_ReadGasAs(x, i, n, &gasRead, floats, sizeof floats), "ReadGasAs", k);
        Refused(mvfxInstance_SampleGas(x, i, n, mvfx_gasDensity, point, 1, floats), "SampleGas", k);
        Refused(mvfxInstance_ReadWater(x, i, n, &waterRead, floats, sizeof floats), "ReadWater", k);
        Refused(mvfxInstance_SampleWater(x, i, n, point, 1, &sample), "SampleWater", k);
        Refused(mvfxInstance_GetSurfaceMeshScratchBytes(x, i, n, &a), "GetSurfaceMeshScratchBytes",
                k);
        Refused(mvfxInstance_WriteSurfaceMesh(x, i, n, point, 0.5f, &mesh, &a, &b),
                "WriteSurfaceMesh", k);
        Refused(mvfxEffect_GetEmitterView(x, s_effect, n, &emitterView), "GetEmitterView", k);
        Refused(mvfxEffect_GetGasView(x, s_effect, n, &gasView), "GetGasView", k);
        Refused(mvfxEffect_GetWaterView(x, s_effect, n, &waterView), "GetWaterView", k);
        Refused(mvfxInstance_GetEmitterMesh(x, i, n, &meshView), "GetEmitterMesh", k);
        Refused(mvfxInstance_GetEmitterPlan(x, p, n, &emitterPlan), "GetEmitterPlan", k);
        Refused(mvfxInstance_GetEmitterStepPlan(x, p, n, 0, &emitterPlan), "GetEmitterStepPlan", k);
        Refused(mvfxInstance_GetEmitterStepPlan(x, p, 0, n, &emitterPlan),
                "GetEmitterStepPlan's step", k);
        Refused(mvfxInstance_GetGasPlan(x, p, n, &gasPlan), "GetGasPlan", k);
        Refused(mvfxInstance_GetGasStepPlan(x, p, n, 0, &gasPlan), "GetGasStepPlan", k);
        Refused(mvfxInstance_GetGasStepPlan(x, p, 0, n, &gasPlan), "GetGasStepPlan's step", k);
        Refused(mvfxInstance_GetWaterPlan(x, p, n, &waterPlan), "GetWaterPlan", k);
        Refused(mvfxInstance_GetWaterStepPlan(x, p, n, 0, &waterPlan), "GetWaterStepPlan", k);
        Refused(mvfxInstance_GetWaterStepPlan(x, p, 0, n, &waterPlan), "GetWaterStepPlan's step",
                k);
        Refused(mvfxInstance_SetGasHolds(x, p, n, 0, true), "SetGasHolds", k);
        Refused(mvfxInstance_SetLiquidSpeed(x, p, n, 0, 1.0f), "SetLiquidSpeed", k);
        Refused(mvfxInstance_SetWaterHolds(x, p, n, 0, true), "SetWaterHolds", k);
        Refused(mvfxInstance_SetWaterSpeed(x, p, n, 0, 1.0f), "SetWaterSpeed", k);
        Refused(mvfxInstance_SetWaterCounts(x, p, n, 0, 1.0f, 0, 0.0), "SetWaterCounts", k);
    }
}

static void TestCounts(void)
{
    // Counts, capacities, sizes and ticks, of 32 and of 64 bits.
    static const mvfxView view = {.forward = {0, 0, -1}, .up = {0, 1, 0}};
    static const double point[3] = {0, 0, 0};
    static unsigned char records[256];
    static unsigned char scratch[4096];
    static float floats[4 * 4 * 4 * 3];
    static unsigned char bytes[64];
    static char text[16];
    const mvfxAttachFrame frame = {.rotation = {0, 0, 0, 1}};
    const mvfxSceneAnswer answer = {0};
    const mvfxWaterPush push = {.radius = 0.1f, .depth = 0.1f};
    const mvfxCollider collider = {.kind = mvfx_colliderPlane, .normal = {0, 1, 0}};
    const mvfxNoise noise = {.frequency = 0.37f, .octaves = 2, .lacunarity = 2.0f, .gain = 0.45f};
    static const int32_t size[3] = {1, 1, 1};
    static const int32_t cube[3] = {4, 4, 4};
    static const int32_t first[3] = {0, 0, 0};
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    mvfxEffectRefusal refusal;
    memset(&refusal, 0, sizeof refusal);
    for (int32_t k = 0; k < 2; ++k)
    {
        const int32_t n = s_negatives[k];
        const int64_t m = s_negatives64[k];
        mvfxContext* x = s_context;
        mvfxInstanceId i = s_plain;
        int64_t a = 0;
        double remaining = 0.0;
        mvfxEffectDef read;
        mvfxEffectId effect;
        static mvfxWaterSample sample;
        Refused(mvfxGetEffectFieldPath(&refusal, text, m, &a), "GetEffectFieldPath", k);
        Refused(mvfxBakeCurlNoise(&noise, n, cube, floats, 4 * 4 * 4 * 3), "BakeCurlNoise's period",
                k);
        Refused(mvfxBakeCurlNoise(&noise, 4, cube, floats, m), "BakeCurlNoise's count", k);
        Refused(mvfxWriteEffect(&def, NULL, n, bytes, sizeof bytes, &a, NULL).result,
                "WriteEffect's assets", k);
        Refused(mvfxWriteEffect(&def, NULL, 0, bytes, m, &a, NULL).result, "WriteEffect's capacity",
                k);
        Refused(mvfxReadEffect(bytes, m, NULL, 0, scratch, sizeof scratch, &a, &read, NULL).result,
                "ReadEffect's size", k);
        Refused(
            mvfxReadEffect(bytes, sizeof bytes, NULL, n, scratch, sizeof scratch, &a, &read, NULL)
                .result,
            "ReadEffect's assets", k);
        Refused(mvfxReadEffect(bytes, sizeof bytes, NULL, 0, scratch, m, &a, &read, NULL).result,
                "ReadEffect's capacity", k);
        Refused(mvfxCreateEffectFromBytes(x, bytes, m, NULL, 0, &effect, NULL).result,
                "CreateEffectFromBytes's size", k);
        Refused(mvfxCreateEffectFromBytes(x, bytes, sizeof bytes, NULL, n, &effect, NULL).result,
                "CreateEffectFromBytes's assets", k);
        Refused(mvfxGrid_SetValues(x, s_grid, floats, m), "Grid_SetValues", k);
        Refused(mvfxGrid_SetValuesIn(x, s_grid, first, size, floats, m), "Grid_SetValuesIn", k);
        Refused(mvfxInstance_SetOffscreen(x, i, mvfx_offscreenThrottle, n), "SetOffscreen", k);
        Refused(mvfxInstance_SetAttachFrames(x, i, 0, &frame, n), "SetAttachFrames' count", k);
        Refused(mvfxInstance_Prewarm(x, i, 1.0, 0.1, n, &remaining), "Prewarm", k);
        Refused(mvfxInstance_SetColliders(x, i, &collider, n), "SetColliders", k);
        Refused(mvfxInstance_AnswerSceneQueries(x, i, 0, &answer, n), "AnswerSceneQueries' count",
                k);
        Refused(mvfxContext_StepTicks(x, m), "StepTicks", k);
        Refused(mvfxInstance_ReadGas(x, i, 0, mvfx_gasDensity, floats, m), "ReadGas's count", k);
        Refused(mvfxInstance_PushWater(x, i, 0, &push, n), "PushWater's count", k);
        Refused(mvfxMesh_SetPositions(x, s_mesh, floats, NULL, n), "SetPositions", k);
        Refused(mvfxInstance_WriteParticles(x, i, 0, s_layout, &view, scratch, m, records, 4, &a),
                "WriteParticles' scratch", k);
        Refused(mvfxInstance_WriteParticles(x, i, 0, s_layout, &view, scratch, sizeof scratch,
                                            records, m, &a),
                "WriteParticles' capacity", k);
        Refused(mvfxInstance_WriteLights(x, i, 0, &view, (mvfxLight*)records, m, &a),
                "WriteLights' capacity", k);
        Refused(mvfxInstance_SampleGas(x, i, 0, mvfx_gasDensity, point, m, floats),
                "SampleGas's count", k);
        Refused(mvfxInstance_SampleWater(x, i, 0, point, m, &sample), "SampleWater's count", k);
        Refused(mvfxContext_OrderEmitters(x, point, NULL, (mvfxDrawItem*)records, m, &a),
                "OrderEmitters", k);
        Refused(mvfxInstance_SetGasHolds(x, s_planned, 0, m, true), "SetGasHolds' tick", k);
        Refused(mvfxInstance_SetWaterHolds(x, s_planned, 0, m, true), "SetWaterHolds' tick", k);
    }
}

int main(void)
{
    Open();
    TestInstanceIndices();
    TestCounts();
    CHECK(s_taken == 0, "every negative index and count refused as invalid");
    mvfxDestroyContext(s_context);
    return s_failures == 0 ? 0 : 1;
}
