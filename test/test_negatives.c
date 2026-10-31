// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Every index and count a call takes, given -1 and the least integer of
// its type: refused as invalid, never read below an array's start. Where
// -1 names something (every emitter of GetBounds and BreakRibbons), -2
// stands in for it. The calls go to an instance plain and one planned, of
// an effect with three emitters, a parameter, three gases and two water
// surfaces, so each index has something to be past. Then every index and
// count field of the structs the library reads (an effect def's, a mesh,
// grid, layout, instance and context def's, and a call's structs and
// buffers), each from a def made or a call made with the field valid, set
// to both: refused as invalid, an effect def's naming the field. Only
// drawPriority, an order, takes a negative.

#include "test_harness.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/file.h"
#include "maul-vfx/grid.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/mesh.h"
#include "maul-vfx/output.h"
#include "maul-vfx/plan.h"
#include "maul-vfx/water.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
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
    // Gas 1 is a liquid and water 1 shallow, which the liquid's and the
    // water's speeds and counts need before they read their tick; gas 2 a
    // surface, as a surface mesh needs.
    // Emitter 0 takes the host's spawns and emitter 2 draws ribbons, as
    // the spawn request's and the tube's rows need.
    mvfxEmitterDef emitters[3] = {mvfxDefaultEmitterDef(), mvfxDefaultEmitterDef(),
                                  mvfxDefaultEmitterDef()};
    emitters[0].hostSpawnCapacity = 4;
    emitters[2].ribbon.ids = mvfx_ribbonPerBatch;
    emitters[1].liquid.enable = true;
    emitters[1].liquid.solver = mvfx_liquidGrid;
    emitters[1].liquid.gas = 1;
    emitters[1].liquid.spacing = 0.05f;
    mvfxGasDef gases[3] = {mvfxDefaultGasDef(), mvfxDefaultGasDef(), mvfxDefaultGasDef()};
    for (int32_t g = 0; g < 3; ++g)
    {
        gases[g].size[0] = 4;
        gases[g].size[1] = 4;
        gases[g].size[2] = 4;
        gases[g].cellSize = 0.25f;
    }
    gases[1].kind = mvfx_gasLiquid;
    gases[1].closedWalls = 63;
    gases[1].courant = 0.3f;
    gases[2].kind = mvfx_gasSurface;
    gases[2].surface.radius = 0.1f;
    mvfxWaterDef waters[2] = {mvfxDefaultWaterDef(), mvfxDefaultWaterDef()};
    for (int32_t w = 0; w < 2; ++w)
    {
        waters[w].size[0] = 4;
        waters[w].size[1] = 4;
        waters[w].cellSize = 0.25f;
    }
    waters[1].model = mvfx_waterShallow;
    static const float parameter[4] = {1, 0, 0, 0};
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = emitters;
    def.emitterCount = 3;
    def.parameters = parameter;
    def.parameterCount = 1;
    def.gases = gases;
    def.gasCount = 3;
    def.waters = waters;
    def.waterCount = 2;
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

static void TestTicksTaken(void)
{
    // The tick of 0 the tick rows change is taken, so a negative is what
    // they refuse.
    CHECK(mvfxInstance_SetLiquidSpeed(s_context, s_planned, 1, 0, 1.0f) == mvfx_success &&
              mvfxInstance_SetWaterSpeed(s_context, s_planned, 1, 0, 1.0f) == mvfx_success &&
              mvfxInstance_SetWaterCounts(s_context, s_planned, 1, 0, 1.0f, 0, 0.0) ==
                  mvfx_success &&
              mvfxInstance_SetGasHolds(s_context, s_planned, 0, 0, true) == mvfx_success &&
              mvfxInstance_SetWaterHolds(s_context, s_planned, 0, 0, true) == mvfx_success,
          "a tick of 0 taken by each call whose tick a row makes negative");
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
        // Gas 1 a liquid and water 1 shallow, as these calls need.
        Refused(mvfxInstance_SetLiquidSpeed(x, s_planned, 1, m, 1.0f), "SetLiquidSpeed's tick", k);
        Refused(mvfxInstance_SetWaterSpeed(x, s_planned, 1, m, 1.0f), "SetWaterSpeed's tick", k);
        Refused(mvfxInstance_SetWaterCounts(x, s_planned, 1, m, 1.0f, 0, 0.0),
                "SetWaterCounts' tick", k);
    }
}

// The parts of an effect def a row changes: two emitters, a gas and a
// water surface, two parameters, and what a row turns on.
typedef struct Defs
{
    mvfxEmitterDef emitters[2];
    mvfxBurst burst;
    mvfxCurveKey keys[2];
    mvfxColorKey colorKeys[2];
    mvfxAlphaKey alphaKeys[2];
    mvfxForce force;
    mvfxKillRule rule;
    mvfxCustomAttribute custom;
    mvfxGasDef gas;
    mvfxWaterDef water;
    float parameters[8];
    float path[6];
    mvfxEffectDef effect;
} Defs;

static void MakeDefs(Defs* d)
{
    *d = (Defs){0};
    d->emitters[0] = mvfxDefaultEmitterDef();
    d->emitters[1] = mvfxDefaultEmitterDef();
    d->gas = mvfxDefaultGasDef();
    d->gas.size[0] = 4;
    d->gas.size[1] = 4;
    d->gas.size[2] = 4;
    d->gas.cellSize = 0.25f;
    d->water = mvfxDefaultWaterDef();
    d->water.size[0] = 4;
    d->water.size[1] = 4;
    d->water.cellSize = 0.25f;
    d->effect = mvfxDefaultEffectDef();
    d->effect.emitters = d->emitters;
    d->effect.emitterCount = 2;
    d->effect.parameters = d->parameters;
    d->effect.parameterCount = 2;
    d->effect.gases = &d->gas;
    d->effect.gasCount = 1;
    d->effect.waters = &d->water;
    d->effect.waterCount = 1;
}

// An index or count field of an effect def: the path a refusal of it
// names, and a value the def takes once the row has turned on what the
// field belongs to. A count is named as the array it counts, and a
// maxCount or a last below its minCount or first as that one, whose
// comment states the rule between them. A neighbor source out of range is
// named as the emitters: the emitters' order, refused first, takes it for
// a cycle.
typedef struct Row
{
    const char* path;
    int32_t valid;
    // The first negative tried, where -1 names something; 0 for -1.
    int32_t negative;
} Row;

static const Row s_rows[] = {
    {"emitters", 2, 0},
    {"parameters", 2, 0},
    {"qualityLevels", 0, 0},
    {"gases", 1, 0},
    {"waters", 1, 0},
    {"emitters[0].bursts", 1, 0},
    {"emitters[0].bursts[0].minCount", 1, 0},
    {"emitters[0].bursts[0].minCount", 1, 0},
    {"emitters[0].forces", 1, 0},
    {"emitters[0].killRules", 0, 0},
    {"emitters[0].colliders", 0, 0},
    {"emitters[0].hostModules", 0, 0},
    {"emitters[0].custom", 0, 0},
    {"emitters[0].forces[0].strength.parameter", 1, 0},
    {"emitters[0].size.parameter", 1, 0},
    {"emitters[0].forces[0].gas", 0, 0},
    {"emitters[0].liquid.gas", 0, 0},
    {"emitters[0].gasSplat.gas", 0, 0},
    {"emitters[0].water.water", 0, 0},
    {"emitters[0].beam.start.parameter", 1, 0},
    {"emitters[0].beam.controlCount", 0, 0},
    {"gases[0].surface.emitter", 0, 0},
    {"emitters[0].forces[0].strength.curve.keys", 2, 0},
    {"emitters[0].color.gradient.colorKeys", 2, 0},
    {"emitters[0].color.gradient.alphaKeys", 2, 0},
    {"emitters[1].eventSpawn.minCount", 1, 0},
    {"emitters[1].eventSpawn.minCount", 1, 0},
    {"gases[0].sources", 0, 0},
    {"gases[0].obstacles", 0, 0},
    {"gases[0].forces", 0, 0},
    {"waters[0].sources", 0, 0},
    {"emitters[0].overLife.frame.frameCount", 16, 0},
    {"emitters[0].steering.path.points", 2, 0},
    {"emitters[0].bursts[0].cycles", 1, 0},
    {"emitters[0].forces[0].noise.octaves", 2, 0},
    {"emitters[0].ribbon.pieces", 1, 0},
    {"emitters[0].unit.points", 3, 0},
    {"emitters[0].unit.rows", 2, 0},
    {"emitters[0].unit.substeps", 4, 0},
    {"emitters[0].unit.selfContacts", 4, 0},
    {"emitters[0].unit.attachFrameCapacity", 2, 0},
    {"emitters[0].rigid.substeps", 1, 0},
    {"emitters[0].rigid.bodyContacts", 0, 0},
    {"emitters[0].liquid.substeps", 4, 0},
    {"emitters[0].liquid.iterations", 3, 0},
    {"emitters[0].liquid.tensilePower", 4, 0},
    {"emitters[0].liquid.maxNeighbors", 16, 0},
    {"emitters[0].plexus.maxLinks", 1, 0},
    {"emitters[0].plexus.maxPerParticle", 1, 0},
    {"emitters[0].beam.end.parameter", 1, 0},
    {"emitters[0].beam.controls[0].parameter", 1, 0},
    {"emitters[0].beam.controls[1].parameter", 1, 0},
    {"emitters[0].beam.segments", 4, 0},
    {"emitters[0].light.every", 3, 0},
    {"emitters[0].capacity", 64, 0},
    {"emitters[0].loops", 0, 0},
    {"emitters[0].hostSpawnCapacity", 0, 0},
    {"emitters[0].eventCapacity", 4, 0},
    {"gases[0].surface.smoothing", 0, 0},
    {"gases[0].size[0]", 4, 0},
    {"gases[0].size[1]", 4, 0},
    {"gases[0].size[2]", 4, 0},
    {"gases[0].substeps", 1, 0},
    {"gases[0].pressureCycles", 2, 0},
    {"waters[0].size[0]", 4, 0},
    {"waters[0].size[1]", 4, 0},
    {"emitters[0].killRules[0].collisions", 1, 0},
    {"emitters[1].eventSpawn.source", 0, 0},
    {"emitters[1].eventSpawn.capacity", 4, 0},
    {"emitters", -1, -2},
    {"emitters[0].neighbors.maxNeighbors", 4, 0},
    {"emitters[0].steering.seek.parameter", 1, 0},
    {"emitters[0].steering.flee.parameter", 1, 0},
    {"emitters[0].contacts.iterations", 1, 0},
    {"emitters[0].contacts.maxContacts", 16, 0},
    {"emitters[0].collision.queryCapacity", 4, 0},
    {"emitters[0].collision.depthSamples", 64, 0},
    {"emitters[0].frame.first", 0, 0},
    {"emitters[0].frame.first", 0, 0},
    {"emitters[0].meshIndex.first", 0, 0},
    {"emitters[0].meshIndex.first", 0, 0},
    {"emitters[0].custom[0].components", 1, 0},
};

// The first row MoreField turns on.
#define MORE_ROWS 33

// Turns on what row r from MORE_ROWS on belongs to, and returns its field.
static int32_t* MoreField(Defs* d, int32_t r)
{
    mvfxEmitterDef* e = d->emitters;
    switch (r - MORE_ROWS)
    {
    case 0:
        d->burst = (mvfxBurst){.minCount = 1, .maxCount = 1, .cycles = 1, .probability = 1};
        e[0].bursts = &d->burst;
        e[0].burstCount = 1;
        return &d->burst.cycles;
    case 1:
        d->force = (mvfxForce){
            .kind = mvfx_forceTurbulence,
            .strength = {.kind = mvfx_sourceRange, .range = {1, 1}},
            .noise = {.frequency = 0.37f, .octaves = 2, .lacunarity = 2.0f, .gain = 0.45f}};
        e[0].forces = &d->force;
        e[0].forceCount = 1;
        return &d->force.noise.octaves;
    case 2:
        e[0].ribbon.ids = mvfx_ribbonPerBatch;
        return &e[0].ribbon.pieces;
    case 3:
    case 5:
    case 7:
        e[0].rate = 0.0f;
        e[0].capacity = 12;
        e[0].hostSpawnCapacity = 2;
        e[0].unit = (mvfxUnitDef){.kind = mvfx_unitChain,
                                  .points = 3,
                                  .spacing = 0.5f,
                                  .direction = {0, -1, 0},
                                  .substeps = 4,
                                  .pinFirst = true,
                                  .attach = r - MORE_ROWS == 7 ? mvfx_attachHost : mvfx_attachNone,
                                  .attachFrameCapacity = 2};
        return r - MORE_ROWS == 3   ? &e[0].unit.points
               : r - MORE_ROWS == 5 ? &e[0].unit.substeps
                                    : &e[0].unit.attachFrameCapacity;
    case 4:
    case 6:
        e[0].rate = 0.0f;
        e[0].capacity = 16;
        e[0].hostSpawnCapacity = 4;
        e[0].unit = (mvfxUnitDef){.kind = mvfx_unitSheet,
                                  .points = 2,
                                  .rows = 2,
                                  .spacing = 0.5f,
                                  .direction = {1, 0, 0},
                                  .downDirection = {0, 0, 1},
                                  .substeps = 8,
                                  .selfThickness = 0.1f,
                                  .selfContacts = 4};
        return r - MORE_ROWS == 4 ? &e[0].unit.rows : &e[0].unit.selfContacts;
    case 8:
    case 9:
        e[0].rigid.enable = true;
        e[0].rigid.shape = mvfx_rigidSphere;
        e[0].rigid.density = 1.0f;
        e[0].rigid.substeps = 1;
        return r - MORE_ROWS == 8 ? &e[0].rigid.substeps : &e[0].rigid.bodyContacts;
    case 10:
    case 11:
    case 12:
    case 13:
    {
        e[0].liquid.enable = true;
        e[0].liquid.spacing = 0.05f;
        int32_t* fields[4] = {&e[0].liquid.substeps, &e[0].liquid.iterations,
                              &e[0].liquid.tensilePower, &e[0].liquid.maxNeighbors};
        return fields[r - MORE_ROWS - 10];
    }
    case 14:
    case 15:
        e[0].plexus = (mvfxPlexusDef){
            .enable = true, .distance = 0.5f, .maxLinks = 1, .maxPerParticle = 1, .width = 0.01f};
        return r - MORE_ROWS == 14 ? &e[0].plexus.maxLinks : &e[0].plexus.maxPerParticle;
    case 16:
    case 17:
    case 18:
    case 19:
        e[0].rate = 0.0f;
        e[0].capacity = 8;
        e[0].ribbon.ids = mvfx_ribbonOne;
        e[0].beam = (mvfxBeamDef){.enable = true,
                                  .segments = 4,
                                  .end = {.fromParameter = true},
                                  .controlCount = 2,
                                  .controls = {{.fromParameter = true}, {.fromParameter = true}}};
        return r - MORE_ROWS == 16   ? &e[0].beam.end.parameter
               : r - MORE_ROWS == 17 ? &e[0].beam.controls[0].parameter
               : r - MORE_ROWS == 18 ? &e[0].beam.controls[1].parameter
                                     : &e[0].beam.segments;
    case 20:
        e[0].light = (mvfxLightDef){.enable = true, .intensity = 1.0f, .radius = 0.5f};
        return &e[0].light.every;
    case 21:
        return &e[0].capacity;
    case 22:
        return &e[0].loops;
    case 23:
        return &e[0].hostSpawnCapacity;
    case 24:
        e[0].events = mvfx_eventMaskBirth;
        return &e[0].eventCapacity;
    case 25:
        d->gas.kind = mvfx_gasSurface;
        d->gas.surface.radius = 0.1f;
        return &d->gas.surface.smoothing;
    case 26:
    case 27:
    case 28:
        return &d->gas.size[r - MORE_ROWS - 26];
    case 29:
        return &d->gas.substeps;
    case 30:
        return &d->gas.pressureCycles;
    case 31:
    case 32:
        return &d->water.size[r - MORE_ROWS - 31];
    case 33:
        d->rule = (mvfxKillRule){.kind = mvfx_killCollisions};
        e[0].killRules = &d->rule;
        e[0].killRuleCount = 1;
        e[0].collision.enable = true;
        return &d->rule.collisions;
    case 34:
    case 35:
        e[0].events = mvfx_eventMaskBirth;
        e[0].eventCapacity = 4;
        e[1].eventSpawn = (mvfxEventSpawn){
            .enable = true, .kind = mvfx_eventBirth, .minCount = 1, .maxCount = 1, .capacity = 4};
        return r - MORE_ROWS == 34 ? &e[1].eventSpawn.source : &e[1].eventSpawn.capacity;
    case 36:
    case 37:
        e[0].neighbors =
            (mvfxNeighborDef){.enable = true, .source = -1, .radius = 1.0f, .maxNeighbors = 4};
        return r - MORE_ROWS == 36 ? &e[0].neighbors.source : &e[0].neighbors.maxNeighbors;
    case 38:
    case 39:
    {
        e[0].steering.enable = true;
        mvfxSteerPoint* point = r - MORE_ROWS == 38 ? &e[0].steering.seek : &e[0].steering.flee;
        *point = (mvfxSteerPoint){.weight = 3.0f, .fromParameter = true, .radius = 2.0f};
        return &point->parameter;
    }
    case 40:
    case 41:
        e[0].contacts = (mvfxContactDef){
            .enable = true, .radiusScale = 0.5f, .iterations = 1, .maxContacts = 16};
        return r - MORE_ROWS == 40 ? &e[0].contacts.iterations : &e[0].contacts.maxContacts;
    case 42:
    case 43:
        e[0].collision = (mvfxCollision){.enable = true,
                                         .enableSceneQueries = true,
                                         .queryCapacity = 4,
                                         .lookahead = 0.1f,
                                         .enableDepthImage = true,
                                         .depthThickness = 0.5f,
                                         .depthSamples = 64};
        return r - MORE_ROWS == 42 ? &e[0].collision.queryCapacity : &e[0].collision.depthSamples;
    case 44:
    case 45:
        e[0].frame.mode = mvfx_frameRandom;
        return r - MORE_ROWS == 44 ? &e[0].frame.first : &e[0].frame.last;
    case 46:
    case 47:
        e[0].meshIndex.mode = mvfx_frameRandom;
        return r - MORE_ROWS == 46 ? &e[0].meshIndex.first : &e[0].meshIndex.last;
    default:
        d->custom = (mvfxCustomAttribute){.components = 1};
        e[0].custom = &d->custom;
        e[0].customCount = 1;
        return &d->custom.components;
    }
}

// Turns on what row r's field belongs to, and returns the field.
static int32_t* Field(Defs* d, int32_t r)
{
    if (r >= MORE_ROWS)
    {
        return MoreField(d, r);
    }
    mvfxEmitterDef* e = d->emitters;
    switch (r)
    {
    case 0:
        return &d->effect.emitterCount;
    case 1:
        return &d->effect.parameterCount;
    case 2:
        return &d->effect.qualityLevelCount;
    case 3:
        return &d->effect.gasCount;
    case 4:
        return &d->effect.waterCount;
    case 5:
    case 6:
    case 7:
        d->burst = (mvfxBurst){.minCount = 1, .maxCount = 1, .cycles = 1, .probability = 1};
        e[0].bursts = &d->burst;
        e[0].burstCount = 1;
        return r == 5 ? &e[0].burstCount : r == 6 ? &d->burst.minCount : &d->burst.maxCount;
    case 8:
        d->force = (mvfxForce){.kind = mvfx_forceConstant,
                               .strength = {.kind = mvfx_sourceRange, .range = {1, 1}}};
        e[0].forces = &d->force;
        e[0].forceCount = 1;
        return &e[0].forceCount;
    case 9:
        return &e[0].killRuleCount;
    case 10:
        return &e[0].colliderCount;
    case 11:
        return &e[0].hostModuleCount;
    case 12:
        return &e[0].customCount;
    case 13:
        d->force =
            (mvfxForce){.kind = mvfx_forceConstant, .strength = {.kind = mvfx_sourceParameter}};
        e[0].forces = &d->force;
        e[0].forceCount = 1;
        return &d->force.strength.parameter;
    case 14:
        e[0].size = (mvfxVectorSource){.kind = mvfx_sourceParameter};
        return &e[0].size.parameter;
    case 15:
        d->force = (mvfxForce){.kind = mvfx_forceGas,
                               .strength = {.kind = mvfx_sourceRange, .range = {1, 1}},
                               .fieldMode = mvfx_fieldVelocity,
                               .tightness = 8.0f};
        e[0].forces = &d->force;
        e[0].forceCount = 1;
        return &d->force.gas;
    case 16:
        d->gas.kind = mvfx_gasLiquid;
        d->gas.closedWalls = 63;
        d->gas.courant = 0.3f;
        e[0].liquid.enable = true;
        e[0].liquid.solver = mvfx_liquidGrid;
        e[0].liquid.spacing = 0.05f;
        return &e[0].liquid.gas;
    case 17:
        e[0].gasSplat = (mvfxGasSplat){.enable = true, .density = 1.0f};
        return &e[0].gasSplat.gas;
    case 18:
        e[0].water = (mvfxWaterCoupling){.enable = true, .radius = 0.05f};
        return &e[0].water.water;
    case 19:
    case 20:
        e[0].rate = 0.0f;
        e[0].capacity = 8;
        e[0].ribbon.ids = mvfx_ribbonOne;
        e[0].beam = (mvfxBeamDef){.enable = true,
                                  .segments = 4,
                                  .start = {.fromParameter = true},
                                  .end = {.point = {1, 0, 0}}};
        return r == 19 ? &e[0].beam.start.parameter : &e[0].beam.controlCount;
    case 21:
        d->gas.kind = mvfx_gasSurface;
        d->gas.surface.radius = 0.1f;
        return &d->gas.surface.emitter;
    case 22:
        d->keys[0] = (mvfxCurveKey){.time = 0.0f, .value = 1.0f};
        d->keys[1] = (mvfxCurveKey){.time = 1.0f, .value = 2.0f};
        d->force = (mvfxForce){
            .kind = mvfx_forceConstant,
            .strength = {.kind = mvfx_sourceCurve, .range = {1, 1}, .curve = {.keys = d->keys}}};
        e[0].forces = &d->force;
        e[0].forceCount = 1;
        return &d->force.strength.curve.keyCount;
    case 23:
    case 24:
        d->colorKeys[0] = (mvfxColorKey){.time = 0.0f, .red = 1.0f};
        d->colorKeys[1] = (mvfxColorKey){.time = 1.0f, .blue = 1.0f};
        d->alphaKeys[0] = (mvfxAlphaKey){.time = 0.0f, .alpha = 1.0f};
        d->alphaKeys[1] = (mvfxAlphaKey){.time = 1.0f, .alpha = 0.0f};
        e[0].color =
            (mvfxVectorSource){.kind = mvfx_sourceGradient,
                               .gradient = {.colorKeys = d->colorKeys, .alphaKeys = d->alphaKeys}};
        e[0].color.gradient.colorKeyCount = 2;
        e[0].color.gradient.alphaKeyCount = 2;
        return r == 23 ? &e[0].color.gradient.colorKeyCount : &e[0].color.gradient.alphaKeyCount;
    case 25:
    case 26:
        e[0].events = mvfx_eventMaskBirth;
        e[0].eventCapacity = 4;
        e[1].eventSpawn = (mvfxEventSpawn){.enable = true,
                                           .source = 0,
                                           .kind = mvfx_eventBirth,
                                           .minCount = 1,
                                           .maxCount = 1,
                                           .capacity = 4};
        return r == 25 ? &e[1].eventSpawn.minCount : &e[1].eventSpawn.maxCount;
    case 27:
        return &d->gas.sourceCount;
    case 28:
        return &d->gas.obstacleCount;
    case 29:
        return &d->gas.forceCount;
    case 30:
        return &d->water.sourceCount;
    case 31:
        e[0].overLife.frame =
            (mvfxFrameOverLife){.animation = mvfx_frameAtRate, .frameRate = 24.0f, .loop = true};
        return &e[0].overLife.frame.frameCount;
    default:
        d->path[3] = 1.0f;
        e[0].steering = (mvfxSteeringDef){
            .enable = true, .path = {.weight = 1.0f, .points = d->path, .radius = 0.5f}};
        return &e[0].steering.path.pointCount;
    }
}

// Whether a refusal names the field of a path, as text.
static bool IsPath(const mvfxEffectRefusal* refusal, const char* path)
{
    char text[256];
    int64_t length = -1;
    return mvfxGetEffectFieldPath(refusal, text, sizeof text, &length) == mvfx_success &&
           length == (int64_t)strlen(path) && memcmp(text, path, (size_t)length) == 0;
}

static void TestDefFields(void)
{
    // Each row from a def made with its field valid, then the field -1
    // and the least integer: refused as invalid, naming the field.
    static Defs d;
    int32_t made = 0;
    int32_t named = 0;
    const int32_t rows = (int32_t)(sizeof s_rows / sizeof s_rows[0]);
    for (int32_t r = 0; r < rows; ++r)
    {
        for (int32_t k = 0; k < 2; ++k)
        {
            MakeDefs(&d);
            int32_t* field = Field(&d, r);
            *field = s_rows[r].valid;
            mvfxEffectId effect = {0};
            mvfxEffectRefusal refusal;
            char text[256] = {0};
            int64_t length = 0;
            bool valid =
                mvfxCreateEffect(s_context, &d.effect, &effect, &refusal) == mvfx_success &&
                mvfxDestroyEffect(s_context, effect) == mvfx_success;
            if (!valid)
            {
                (void)mvfxGetEffectFieldPath(&refusal, text, sizeof text, &length);
                printf("  %s's valid def refused, naming %s\n", s_rows[r].path, text);
            }
            *field = k == 0 && s_rows[r].negative != 0 ? s_rows[r].negative : s_negatives[k];
            mvfxResult result = mvfxCreateEffect(s_context, &d.effect, &effect, &refusal);
            bool refused = result == mvfx_errorInvalid && IsPath(&refusal, s_rows[r].path);
            if (!refused)
            {
                (void)mvfxGetEffectFieldPath(&refusal, text, sizeof text, &length);
                printf("  %s with %s: %s naming %s\n", s_rows[r].path,
                       k == 0 ? "-1" : "the least integer", mvfxGetResultName(result), text);
            }
            made += valid ? 1 : 0;
            named += refused ? 1 : 0;
        }
    }
    CHECK(made == 2 * rows, "each row's def made with its field valid");
    CHECK(named == 2 * rows, "each field refused as invalid when negative, and named");
}

static void TestInstanceAndContextDefs(void)
{
    // An instance def's quality level and throttle interval, and each of
    // a context's limits, from a def made, refused as invalid when
    // negative.
    int32_t made = 0;
    int32_t refused = 0;
    for (int32_t f = 0; f < 8; ++f)
    {
        for (int32_t k = 0; k < 2; ++k)
        {
            mvfxInstanceDef instance = mvfxDefaultInstanceDef();
            instance.effect = s_effect;
            instance.offscreen = mvfx_offscreenThrottle;
            instance.offscreenInterval = 2;
            mvfxContextDef context = mvfxDefaultContextDef();
            int32_t* fields[7] = {&instance.quality,       &instance.offscreenInterval,
                                  &context.limits.effects, &context.limits.instances,
                                  &context.limits.meshes,  &context.limits.grids,
                                  &context.limits.layouts};
            for (int32_t pass = 0; pass < 2; ++pass)
            {
                if (f < 7)
                {
                    int32_t saved = *fields[f];
                    *fields[f] = pass == 0 ? saved : s_negatives[k];
                    mvfxResult result;
                    if (f < 2)
                    {
                        mvfxInstanceId id = {0};
                        result = mvfxCreateInstance(s_context, &instance, &id);
                        (void)(result == mvfx_success ? mvfxDestroyInstance(s_context, id) : 0);
                    }
                    else
                    {
                        mvfxContext* made1 = NULL;
                        result = mvfxCreateContext(&context, &made1);
                        mvfxDestroyContext(made1);
                    }
                    made += pass == 0 && result == mvfx_success ? 1 : 0;
                    refused += pass == 1 && result == mvfx_errorInvalid ? 1 : 0;
                    if (pass == 1)
                    {
                        Refused(result, f < 2 ? "an instance def's field" : "a context's limit", k);
                    }
                    *fields[f] = saved;
                }
                else
                {
                    context.limits.particles = pass == 0 ? 0 : s_negatives64[k];
                    mvfxContext* made1 = NULL;
                    mvfxResult result = mvfxCreateContext(&context, &made1);
                    mvfxDestroyContext(made1);
                    made += pass == 0 && result == mvfx_success ? 1 : 0;
                    refused += pass == 1 && result == mvfx_errorInvalid ? 1 : 0;
                    if (pass == 1)
                    {
                        Refused(result, "a context's particle budget", k);
                    }
                }
            }
        }
    }
    CHECK(made == 16 && refused == 16, "an instance def's and a context's counts");
}

static void TestCallStructs(void)
{
    // The counts and corners a call's struct carries: a spawn request's
    // count, a tube's sides, a gas and a water read's first and size, a
    // frustum's planes and a depth image's sizes and strides. Each from
    // the call shown to succeed with them valid.
    static const mvfxView view = {.forward = {0, 0, -1}, .up = {0, 1, 0}};
    static const double point[3] = {0, 0, 0};
    static unsigned char records[256];
    static uint32_t indices[64];
    static float floats[64];
    static const float pixel[3] = {0.5f, 0.0f, 1.0f};
    // The ribbons' sort scratch, as the tubes take it.
    int64_t sortBytes = 0;
    CHECK(mvfxInstance_GetSortScratchBytes(s_context, s_plain, 2, &sortBytes) == mvfx_success,
          "the ribbons' scratch");
    void* sort = malloc((size_t)sortBytes + 1);
    int32_t made = 0;
    int32_t refused = 0;
    for (int32_t f = 0; f < 17; ++f)
    {
        for (int32_t k = 0; k < 2; ++k)
        {
            mvfxSpawnRequest spawn = {.count = 1, .positions = point};
            mvfxTubeDef tube = {.sides = 3};
            mvfxGasRead gas = {.format = mvfx_formatFloat32, .size = {1, 1, 1}, .high = 1.0f};
            mvfxWaterRead water = {.format = mvfx_formatFloat32, .size = {1, 1}, .high = 1.0f};
            mvfxFrustum frustum = {.planeCount = 0};
            mvfxDepthImage depth = {.pixels = pixel,
                                    .byteCount = sizeof(float),
                                    .width = 1,
                                    .height = 1,
                                    .stride = sizeof(float),
                                    .format = mvfx_depthFloat32,
                                    .projection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
                                    .rotation = {0, 0, 0, 1},
                                    .normals = pixel,
                                    .normalByteCount = 3 * sizeof(float),
                                    .normalStride = 3 * sizeof(float)};
            int32_t* fields[17] = {&spawn.count,        &tube.sides,        &gas.first[0],
                                   &gas.first[1],       &gas.first[2],      &gas.size[0],
                                   &gas.size[1],        &gas.size[2],       &water.first[0],
                                   &water.first[1],     &water.size[0],     &water.size[1],
                                   &frustum.planeCount, &depth.width,       &depth.height,
                                   &depth.stride,       &depth.normalStride};
            for (int32_t pass = 0; pass < 2; ++pass)
            {
                int32_t saved = *fields[f];
                *fields[f] = pass == 0 ? saved : s_negatives[k];
                mvfxExpandBuffers expand = {.vertices = records,
                                            .vertexCapacity = 4,
                                            .indices = indices,
                                            .indexCapacity = 6,
                                            .scratch = sort,
                                            .scratchBytes = sortBytes};
                int64_t a = 0;
                int64_t b = 0;
                mvfxInstanceId i = s_plain;
                mvfxResult result =
                    f == 0   ? mvfxInstance_Spawn(s_context, i, 0, &spawn)
                    : f == 1 ? mvfxInstance_ExpandTubes(s_context, i, 2, s_layout, &view, &tube,
                                                        &expand, &a, &b)
                    : f < 8  ? mvfxInstance_ReadGasAs(s_context, i, 0, &gas, floats, sizeof floats)
                    : f < 12
                        ? mvfxInstance_ReadWater(s_context, i, 0, &water, floats, sizeof floats)
                    : f == 12 ? mvfxContext_OrderEmitters(s_context, point, &frustum,
                                                          (mvfxDrawItem*)records, 4, &a)
                              : mvfxInstance_SetDepthImage(s_context, i, &depth);
                made += pass == 0 && result == mvfx_success ? 1 : 0;
                refused += pass == 1 && result == mvfx_errorInvalid ? 1 : 0;
                if (pass == 0 && result != mvfx_success)
                {
                    printf("  call struct %d's valid call: %s\n", f, mvfxGetResultName(result));
                }
                if (pass == 1)
                {
                    char what[32];
                    snprintf(what, sizeof what, "call struct %d", f);
                    Refused(result, what, k);
                }
                *fields[f] = saved;
            }
        }
    }
    free(sort);
    CHECK(made == 34 && refused == 34, "a call struct's counts");
}

static void TestCallBuffers(void)
{
    // The capacities and byte counts of a call's buffers: ribbons', an
    // expansion's and a surface mesh's, and a depth image's bytes, each
    // from the call shown to succeed with them valid.
    static const mvfxView view = {.forward = {0, 0, -1}, .up = {0, 1, 0}};
    static const double origin[3] = {0, 0, 0};
    static const mvfxTubeDef tube = {.sides = 3};
    static unsigned char points[256];
    static mvfxRibbon ribbons[4];
    static unsigned char vertices[256];
    static uint32_t indices[64];
    static float positions[64];
    static const float pixel[3] = {0.5f, 0.0f, 1.0f};
    int64_t sortBytes = 0;
    int64_t meshBytes = 0;
    CHECK(mvfxInstance_GetSortScratchBytes(s_context, s_plain, 2, &sortBytes) == mvfx_success &&
              mvfxInstance_GetSurfaceMeshScratchBytes(s_context, s_plain, 2, &meshBytes) ==
                  mvfx_success,
          "the scratch sizes");
    void* sort = malloc((size_t)sortBytes + 1);
    void* meshScratch = malloc((size_t)meshBytes + 1);
    int32_t made = 0;
    int32_t refused = 0;
    for (int32_t f = 0; f < 11; ++f)
    {
        for (int32_t k = 0; k < 2; ++k)
        {
            mvfxRibbonBuffers list = {.points = points,
                                      .pointCapacity = 4,
                                      .ribbons = ribbons,
                                      .ribbonCapacity = 4,
                                      .scratch = sort,
                                      .scratchBytes = sortBytes};
            mvfxExpandBuffers expand = {.vertices = vertices,
                                        .vertexCapacity = 4,
                                        .indices = indices,
                                        .indexCapacity = 6,
                                        .scratch = sort,
                                        .scratchBytes = sortBytes};
            mvfxSurfaceMeshBuffers mesh = {.positions = positions,
                                           .vertexCapacity = 4,
                                           .indices = indices,
                                           .indexCapacity = 6,
                                           .scratch = meshScratch,
                                           .scratchBytes = meshBytes};
            mvfxDepthImage depth = {.pixels = pixel,
                                    .byteCount = sizeof(float),
                                    .width = 1,
                                    .height = 1,
                                    .stride = sizeof(float),
                                    .format = mvfx_depthFloat32,
                                    .projection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
                                    .rotation = {0, 0, 0, 1},
                                    .normals = pixel,
                                    .normalByteCount = 3 * sizeof(float),
                                    .normalStride = 3 * sizeof(float)};
            int64_t* fields[11] = {
                &list.pointCapacity,    &list.ribbonCapacity,  &list.scratchBytes,
                &expand.vertexCapacity, &expand.indexCapacity, &expand.scratchBytes,
                &mesh.vertexCapacity,   &mesh.indexCapacity,   &mesh.scratchBytes,
                &depth.byteCount,       &depth.normalByteCount};
            for (int32_t pass = 0; pass < 2; ++pass)
            {
                int64_t saved = *fields[f];
                *fields[f] = pass == 0 ? saved : s_negatives64[k];
                int64_t a = 0;
                int64_t b = 0;
                mvfxInstanceId i = s_plain;
                mvfxResult result =
                    f < 3
                        ? mvfxInstance_WriteRibbons(s_context, i, 2, s_layout, &view, &list, &a, &b)
                    : f < 6 ? mvfxInstance_ExpandTubes(s_context, i, 2, s_layout, &view, &tube,
                                                       &expand, &a, &b)
                    : f < 9 ? mvfxInstance_WriteSurfaceMesh(s_context, i, 2, origin, 0.5f, &mesh,
                                                            &a, &b)
                            : mvfxInstance_SetDepthImage(s_context, i, &depth);
                made += pass == 0 && result == mvfx_success ? 1 : 0;
                refused += pass == 1 && result == mvfx_errorInvalid ? 1 : 0;
                if (pass == 0 && result != mvfx_success)
                {
                    printf("  call buffer %d's valid call: %s\n", f, mvfxGetResultName(result));
                }
                if (pass == 1)
                {
                    Refused(result, "a call's buffer size", k);
                }
                *fields[f] = saved;
            }
        }
    }
    free(sort);
    free(meshScratch);
    CHECK(made == 22 && refused == 22, "a call's buffer sizes");
}

static void TestAssetNames(void)
{
    // An asset's name length, in the tables the file calls take: each call
    // shown to succeed with a valid table, then refused as invalid with the
    // length -1 and the least integer.
    mvfxEmitterDef emitter = mvfxDefaultEmitterDef();
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.emitters = &emitter;
    def.emitterCount = 1;
    static unsigned char bytes[4096];
    static unsigned char memory[1 << 16];
    static unsigned char written[4096];
    int64_t size = 0;
    CHECK(mvfxWriteEffect(&def, NULL, 0, bytes, sizeof bytes, &size, NULL).result == mvfx_success,
          "a file to read");
    int32_t made = 0;
    int32_t refused = 0;
    for (int32_t f = 0; f < 3; ++f)
    {
        for (int32_t k = 0; k < 2; ++k)
        {
            mvfxEffectAsset asset = {
                .kind = mvfx_assetMesh, .name = "corner", .nameLength = 6, .mesh = s_mesh};
            for (int32_t pass = 0; pass < 2; ++pass)
            {
                asset.nameLength = pass == 0 ? 6 : s_negatives[k];
                int64_t length = 0;
                mvfxEffectDef read;
                mvfxEffectId effect = {0};
                mvfxResult result = f == 0   ? mvfxWriteEffect(&def, &asset, 1, written,
                                                               sizeof written, &length, NULL)
                                                   .result
                                    : f == 1 ? mvfxReadEffect(bytes, size, &asset, 1, memory,
                                                              sizeof memory, &length, &read, NULL)
                                                   .result
                                             : mvfxCreateEffectFromBytes(s_context, bytes, size,
                                                                         &asset, 1, &effect, NULL)
                                                   .result;
                (void)(f == 2 && result == mvfx_success ? mvfxDestroyEffect(s_context, effect) : 0);
                made += pass == 0 && result == mvfx_success ? 1 : 0;
                refused += pass == 1 && result == mvfx_errorInvalid ? 1 : 0;
                if (pass == 0 && result != mvfx_success)
                {
                    printf("  file call %d's valid call: %s\n", f, mvfxGetResultName(result));
                }
                if (pass == 1)
                {
                    Refused(result, "an asset's name length", k);
                }
            }
        }
    }
    CHECK(made == 6 && refused == 6, "an asset's name length in each file call");
}

static void TestOtherDefCounts(void)
{
    // The counts of a mesh, a grid and a layout def, each from a def made.
    static const float corner[12] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1};
    static const uint32_t faces[3] = {0, 1, 2};
    static const uint32_t solid[4] = {0, 1, 2, 3};
    static const float values[8] = {0};
    static const mvfxLayoutField position = {
        .field = mvfx_recordPosition, .format = mvfx_formatFloat32, .components = 3};
    int32_t refused = 0;
    int32_t made = 0;
    for (int32_t f = 0; f < 8; ++f)
    {
        for (int32_t k = 0; k < 2; ++k)
        {
            mvfxMeshDef mesh = mvfxDefaultMeshDef();
            mesh.positions = corner;
            mesh.vertexCount = 4;
            mesh.triangles = faces;
            mesh.triangleCount = 1;
            mesh.tetrahedra = solid;
            mesh.tetrahedronCount = 1;
            mvfxGridDef grid = mvfxDefaultGridDef();
            grid.size[0] = 2;
            grid.size[1] = 2;
            grid.size[2] = 2;
            grid.components = 1;
            grid.values = values;
            mvfxLayoutDef layout = mvfxDefaultLayoutDef();
            layout.fields = &position;
            layout.fieldCount = 1;
            layout.stride = 16;
            int32_t* fields[8] = {&mesh.vertexCount, &mesh.triangleCount, &mesh.tetrahedronCount,
                                  &grid.size[0],     &grid.size[1],       &grid.size[2],
                                  &grid.components,  &layout.fieldCount};
            mvfxMeshId meshId = {0};
            mvfxGridId gridId = {0};
            mvfxLayoutId layoutId = {0};
            for (int32_t pass = 0; pass < 2; ++pass)
            {
                int32_t saved = *fields[f];
                *fields[f] = pass == 0 ? saved : s_negatives[k];
                mvfxResult result = f < 3   ? mvfxCreateMesh(s_context, &mesh, &meshId)
                                    : f < 7 ? mvfxCreateGrid(s_context, &grid, &gridId)
                                            : mvfxCreateLayout(s_context, &layout, &layoutId);
                if (pass == 0)
                {
                    made += result == mvfx_success ? 1 : 0;
                    (void)(f < 3   ? mvfxDestroyMesh(s_context, meshId)
                           : f < 7 ? mvfxDestroyGrid(s_context, gridId)
                                   : mvfxDestroyLayout(s_context, layoutId));
                }
                else
                {
                    Refused(result,
                            f < 3   ? "a mesh def's count"
                            : f < 7 ? "a grid def's size"
                                    : "a layout def's fields",
                            k);
                    refused += result == mvfx_errorInvalid ? 1 : 0;
                }
                *fields[f] = saved;
            }
        }
    }
    CHECK(made == 16 && refused == 16, "a mesh, a grid and a layout def's counts");
}

int main(void)
{
    Open();
    TestInstanceIndices();
    TestTicksTaken();
    TestCounts();
    TestDefFields();
    TestOtherDefCounts();
    TestInstanceAndContextDefs();
    TestCallStructs();
    TestCallBuffers();
    TestAssetNames();
    CHECK(s_taken == 0, "every negative index and count refused as invalid");
    mvfxDestroyContext(s_context);
    return s_failures == 0 ? 0 : 1;
}
