// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Planned water surfaces' bookkeeping that only the CPU path holds: the
// reach a report replaces and a clear empties, the latest speed reported
// (an older one ignored), and shallow water's terrain in the plan as the
// CPU path resolves it. What a host sees of them is tested in
// test_plan_water.c.

#include "instance.h"
#include "instance_api.h"
#include "plan_resolved.h"
#include "test_harness.h"
#include "water.h"
#include "water_ground.h"

#include "maul-vfx/grid.h"
#include "maul-vfx/plan.h"

#include <string.h>

#define STEP_TICKS (MVFX_TICKS_PER_SECOND / 60)

static mvfxContext* s_context;
static mvfxInstanceId s_cpu;
static mvfxInstanceId s_planned;

// A shallow lake of 40 by 40 cells of 0.1 over a sloping heightfield of
// the instance's frame, a CPU instance and a planned one of it.
static void Open(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    static float heights[33 * 33];
    for (int32_t i = 0; i < 33 * 33; ++i)
    {
        heights[i] = 0.25f * (-2.0f + (float)(i % 33) / 8.0f);
    }
    mvfxGridDef grid = mvfxDefaultGridDef();
    grid.size[0] = 33;
    grid.size[1] = 1;
    grid.size[2] = 33;
    grid.values = heights;
    mvfxGridId id = {0};
    CHECK(mvfxCreateGrid(s_context, &grid, &id) == mvfx_success, "grid");
    mvfxWaterDef water = mvfxDefaultWaterDef();
    water.model = mvfx_waterShallow;
    water.size[0] = 40;
    water.size[1] = 40;
    water.cellSize = 0.1f;
    water.terrain = (mvfxCollider){.kind = mvfx_colliderHeightfield,
                                   .space = 1,
                                   .halfExtents = {2.0f, 1.0f, 2.0f},
                                   .rotation = {0, 0, 0, 1},
                                   .grid = id};
    water.enableTerrain = true;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.waters = &water;
    def.waterCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_context, &def, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_cpu) == mvfx_success, "a CPU instance");
    instanceDef.planned = true;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_planned) == mvfx_success,
          "a planned instance");
}

static const mvfxWaterState* Water(mvfxInstanceId instance)
{
    return &mvfxContextInstance(s_context, instance)->waters[0];
}

static void TestReach(void)
{
    // The latest tick's reach replaces the one before; a clear empties it.
    Open();
    CHECK(mvfxContext_StepTicks(s_context, 2 * STEP_TICKS) == mvfx_success, "stepped");
    CHECK(mvfxInstance_SetWaterCounts(s_context, s_planned, 0, STEP_TICKS, 0.5f, 3, 0.25) ==
                  mvfx_success &&
              mvfxInstance_SetWaterCounts(s_context, s_planned, 0, 2 * STEP_TICKS, 0.2f, 4, 0.5) ==
                  mvfx_success &&
              mvfxInstance_SetWaterCounts(s_context, s_planned, 0, STEP_TICKS, 0.9f, 9, 9.0) ==
                  mvfx_success &&
              Water(s_planned)->reach == 0.2f,
          "the latest reach");
    CHECK(mvfxInstance_Stop(s_context, s_planned, mvfx_stopAndClear) == mvfx_success &&
              Water(s_planned)->reach == 0.0f,
          "a clear empties it");
    mvfxDestroyContext(s_context);
}

static void TestReportedSpeed(void)
{
    // An older report than the latest is ignored.
    Open();
    CHECK(mvfxContext_StepTicks(s_context, STEP_TICKS) == mvfx_success, "stepped");
    CHECK(mvfxInstance_SetWaterSpeed(s_context, s_planned, 0, STEP_TICKS, 3.0f) == mvfx_success &&
              mvfxInstance_SetWaterSpeed(s_context, s_planned, 0, 0, 5.0f) == mvfx_success &&
              Water(s_planned)->reportedSpeed == 3.0f,
          "the latest report kept");
    mvfxDestroyContext(s_context);
}

static void TestTerrain(void)
{
    // Each step's plan carries the terrain as the CPU path resolves it.
    Open();
    mvfxResolvedCollider resolved = {0};
    mvfxGridId grid = {0};
    CHECK(mvfxResolveWaterTerrain(mvfxContextInstance(s_context, s_cpu), 0, &resolved, &grid),
          "a terrain");
    mvfxPlannedCollider terrain = mvfxPlannedColliderOf(&resolved, grid);
    int32_t wrong = 0;
    for (int32_t s = 0; s < 8; ++s)
    {
        CHECK(mvfxContext_StepTicks(s_context, 4 * STEP_TICKS) == mvfx_success, "stepped");
        static mvfxWaterPlan plan;
        CHECK(mvfxInstance_GetWaterPlan(s_context, s_planned, 0, &plan) == mvfx_success, "a plan");
        wrong += !plan.enableTerrain ||
                 memcmp(plan.terrain.center, terrain.center, sizeof terrain.center) != 0 ||
                 memcmp(plan.terrain.halfExtents, terrain.halfExtents,
                        sizeof terrain.halfExtents) != 0 ||
                 plan.terrain.grid.index1 != terrain.grid.index1;
    }
    CHECK(wrong == 0, "the terrain as the CPU path resolves it");
    mvfxDestroyContext(s_context);
}

int main(void)
{
    TestReach();
    TestReportedSpeed();
    TestTerrain();
    return s_failures == 0 ? 0 : 1;
}
