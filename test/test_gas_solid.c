// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A gas's obstacles: a still sphere's cells solid and empty, about as
// many as its area holds; flow past a still box kept free of divergence,
// the box's faces still; a host's sphere moving at a known speed giving
// the faces beside it that speed from the second step (none at the first
// or after a teleport), in steps of either length; a plane moving its
// faces along its normal alone; an obstacle in the instance's frame
// placed through a moved, turned and scaled instance, in a world gas and
// a local one; a turning box giving its faces its turn's
// velocity; a local gas moving past a still box in the world seeing it
// move; an obstacle's grid held by its effect; the obstacles' refusals.

#include "gas_solid.h"
#include "instance.h"
#include "instance_api.h"
#include "test_harness.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/grid.h"
#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

static mvfxContext* s_context;
static mvfxInstanceId s_instance;

// Opens an instance of an effect of one gas; gives its compiled gas and
// state.
static mvfxGasState* Open(const mvfxGasDef* gas, const mvfxCompiledGas** compiled)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.gases = gas;
    def.gasCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
    mvfxInstance* live = mvfxContextInstance(s_context, s_instance);
    *compiled = &live->effect->gases[0];
    return &live->gases[0];
}

// Steps the context by n steps of 1 / rate s.
static void StepsOf(int32_t n, int32_t rate)
{
    for (int32_t i = 0; i < n; ++i)
    {
        CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / rate) == mvfx_success,
              "step");
    }
}

// Steps the context by n steps of 1/64 s.
static void Steps(int32_t n)
{
    StepsOf(n, 64);
}

// A collider of a kind about a center in a space, unturned.
static mvfxCollider Collider(mvfxColliderKind kind, float x, float y, uint8_t space)
{
    return (mvfxCollider){.kind = kind,
                          .space = space,
                          .center = {x, y, 0},
                          .normal = {0, 1, 0},
                          .radius = 0.5f,
                          .halfExtents = {0.4f, 0.3f, 1.0f},
                          .rotation = {0, 0, 0, 1}};
}

static void TestStill(void)
{
    // A 2D gas of 32 by 32 cells of 0.1, a box source pouring density
    // everywhere, a sphere of radius 0.5 at its middle: after 4 steps its
    // solid cells (centers within the sphere) hold nothing and the rest
    // hold density; they number about pi 25, within 8.
    static const mvfxGasSource everywhere = {
        .shape = mvfx_gasBox, .halfExtents = {2, 2, 1}, .density = 1};
    const mvfxCollider sphere = Collider(mvfx_colliderSphere, 0, 0, mvfx_spaceWorld);
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 32;
    def.size[1] = 32;
    def.sources = &everywhere;
    def.sourceCount = 1;
    def.obstacles = &sphere;
    def.obstacleCount = 1;
    const mvfxCompiledGas* gas = NULL;
    mvfxGasState* state = Open(&def, &gas);
    Steps(4);
    int32_t solids = 0;
    bool right = true;
    const float* density = state->scalars[0][state->current];
    for (int32_t i = 0; i < 1024; ++i)
    {
        float x = ((float)(i % 32) + 0.5f - 16.0f) * 0.1f;
        float y = ((float)(i / 32) + 0.5f - 16.0f) * 0.1f;
        bool inside = x * x + y * y < 0.25f;
        solids += state->solid[i] != 0 ? 1 : 0;
        right = right && (state->solid[i] != 0) == inside &&
                (inside ? density[i] == 0.0f : density[i] > 0.0f);
    }
    CHECK(right && solids >= 71 && solids <= 87, "a still sphere's cells solid and empty");
    mvfxDestroyContext(s_context);
}

// The largest divergence's size over a 2D gas's open cells.
static float MaxOpenDivergence(const mvfxCompiledGas* gas, const mvfxGasState* state)
{
    const int32_t* n = gas->def.size;
    const float* u = state->velocity[0][state->current];
    const float* v = state->velocity[1][state->current];
    float most = 0.0f;
    for (int32_t y = 0; y < n[1]; ++y)
    {
        for (int32_t x = 0; x < n[0]; ++x)
        {
            float d = u[y * (n[0] + 1) + x + 1] - u[y * (n[0] + 1) + x] + v[(y + 1) * n[0] + x] -
                      v[y * n[0] + x];
            most = state->solid[y * n[0] + x] == 0 ? fmaxf(most, fabsf(d)) : most;
        }
    }
    return most;
}

static void TestFlowPast(void)
{
    // A 2D gas of 48 by 24, open walls, a box of wind at its left blowing
    // right at 2 into a still box in its middle, 8 V cycles: after 10
    // steps the faces beside the box are still, the flow goes around it
    // (some face beside it is not still across the other axis' faces),
    // and its open cells' divergence is at most 1e-3 of the wind's 2 / 0.1
    // a cell.
    static const mvfxGasSource wind = {.shape = mvfx_gasBox,
                                       .center = {-2.0f, 0, 0},
                                       .halfExtents = {0.3f, 1.0f, 1},
                                       .setsVelocity = true,
                                       .velocity = {2, 0, 0}};
    const mvfxCollider box = Collider(mvfx_colliderBox, 0, 0, mvfx_spaceWorld);
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 48;
    def.size[1] = 24;
    def.pressureCycles = 8;
    def.sources = &wind;
    def.sourceCount = 1;
    def.obstacles = &box;
    def.obstacleCount = 1;
    const mvfxCompiledGas* gas = NULL;
    mvfxGasState* state = Open(&def, &gas);
    Steps(10);
    const float* u = state->velocity[0][state->current];
    bool still = true;
    for (int32_t y = 0; y < 24; ++y)
    {
        for (int32_t x = 0; x <= 48; ++x)
        {
            bool beside = (x > 0 && state->solid[y * 48 + x - 1] != 0) ||
                          (x < 48 && state->solid[y * 48 + x] != 0);
            still = still && (!beside || u[y * 49 + x] == 0.0f);
        }
    }
    float most = MaxOpenDivergence(gas, state);
    CHECK(still && most <= 1e-3f * 20.0f, "flow past a still box");
    mvfxDestroyContext(s_context);
}

// The largest and the least velocity across k of the faces beside a 2D
// gas's solid cells.
static void SolidFaces(const mvfxCompiledGas* gas, const mvfxGasState* state, int32_t k,
                       float range[2])
{
    const int32_t* n = gas->def.size;
    const int32_t m[2] = {n[0] + (k == 0 ? 1 : 0), n[1] + (k == 1 ? 1 : 0)};
    const int32_t stride = k == 0 ? 1 : n[0];
    range[0] = INFINITY;
    range[1] = -INFINITY;
    for (int32_t y = 0; y < m[1]; ++y)
    {
        for (int32_t x = 0; x < m[0]; ++x)
        {
            const int32_t at[2] = {x, y};
            int32_t cell = y * n[0] + x;
            bool beside = (at[k] > 0 && state->solid[cell - stride] != 0) ||
                          (at[k] < n[k] && state->solid[cell] != 0);
            float value = state->velocity[k][state->current][y * m[0] + x];
            range[0] = beside ? fminf(range[0], value) : range[0];
            range[1] = beside ? fmaxf(range[1], value) : range[1];
        }
    }
}

static void TestMoving(void)
{
    // A 2D gas of 32 by 32 with no pressure, its host's sphere set at x =
    // -0.5 + 0.75 t each step: at the first step its faces are still (no
    // step before); from the second, its X faces move at 0.75 and its Y
    // faces not at all; after a teleport, still again for a step. The same
    // in steps of 1/64 s and of 1/32 s.
    bool right = true;
    for (int32_t rate = 32; rate <= 64; rate += 32)
    {
        mvfxGasDef def = mvfxDefaultGasDef();
        def.size[0] = 32;
        def.size[1] = 32;
        def.pressureCycles = 0;
        def.hostObstacles = true;
        const mvfxCompiledGas* gas = NULL;
        mvfxGasState* state = Open(&def, &gas);
        for (int32_t i = 0; i < 6; ++i)
        {
            mvfxCollider sphere = Collider(
                mvfx_colliderSphere, -0.5f + 0.75f * (float)i / (float)rate, 0, mvfx_spaceWorld);
            CHECK(mvfxInstance_SetColliders(s_context, s_instance, &sphere, 1) == mvfx_success,
                  "colliders");
            if (i == 4)
            {
                const double origin[3] = {0, 0, 0};
                const float rotation[4] = {0, 0, 0, 1};
                CHECK(mvfxInstance_SetTransform(s_context, s_instance, origin, rotation, 1.0f,
                                                true) == mvfx_success,
                      "teleport");
            }
            StepsOf(1, rate);
            float across[2];
            float up[2];
            SolidFaces(gas, state, 0, across);
            SolidFaces(gas, state, 1, up);
            float speed = i == 0 || i == 4 ? 0.0f : 0.75f;
            right = right && fabsf(across[0] - speed) <= 1e-3f &&
                    fabsf(across[1] - speed) <= 1e-3f && up[0] == 0.0f && up[1] == 0.0f;
        }
        mvfxDestroyContext(s_context);
    }
    CHECK(right, "a moving sphere's faces move with it");
}

static void TestPlane(void)
{
    // A host's floor plane rising at 0.5 a second while sliding along X at
    // 0.3 in a 2D gas of 32 by 32: the faces beside it rise with it, the
    // slide along the plane moves none of them.
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 32;
    def.size[1] = 32;
    def.pressureCycles = 0;
    def.hostObstacles = true;
    const mvfxCompiledGas* gas = NULL;
    mvfxGasState* state = Open(&def, &gas);
    bool right = true;
    for (int32_t i = 0; i < 3; ++i)
    {
        mvfxCollider floor = Collider(mvfx_colliderPlane, 0.3f * (float)i / 64.0f,
                                      -0.5f + 0.5f * (float)i / 64.0f, mvfx_spaceWorld);
        CHECK(mvfxInstance_SetColliders(s_context, s_instance, &floor, 1) == mvfx_success,
              "colliders");
        Steps(1);
        float across[2];
        float up[2];
        SolidFaces(gas, state, 0, across);
        SolidFaces(gas, state, 1, up);
        float rise = i == 0 ? 0.0f : 0.5f;
        right = right && fabsf(up[0] - rise) <= 1e-3f && fabsf(up[1] - rise) <= 1e-3f &&
                across[0] == 0.0f && across[1] == 0.0f;
    }
    CHECK(right, "a plane moves along its normal");
    mvfxDestroyContext(s_context);
}

static void TestInstanceFrame(void)
{
    // A sphere of radius 0.3 at (0.2, 0, 0) in the instance's frame, the
    // instance moved to (0.5, 0, 0), turned a quarter about Z and scaled
    // by 1.5. In a world gas (its box where the instance began, at the
    // origin) it is a sphere of 0.45 at (0.5, 0.3); in a local gas centered
    // at (0.3, 0, 0) in the instance's frame, one of 0.3 at (-0.1, 0) in
    // the box's own. Its cells are solid exactly where those hold them.
    const float quarter[4] = {0, 0, 0.70710678f, 0.70710678f};
    const double origin[3] = {0.5, 0, 0};
    bool right = true;
    for (int32_t world = 0; world < 2; ++world)
    {
        mvfxCollider ball = Collider(mvfx_colliderSphere, 0.2f, 0, mvfx_spaceLocal);
        ball.radius = 0.3f;
        mvfxGasDef def = mvfxDefaultGasDef();
        def.size[0] = 32;
        def.size[1] = 32;
        def.space = world != 0 ? mvfx_gasWorld : mvfx_gasLocal;
        def.center[0] = world != 0 ? 0.0f : 0.3f;
        def.obstacles = &ball;
        def.obstacleCount = 1;
        const mvfxCompiledGas* gas = NULL;
        mvfxGasState* state = Open(&def, &gas);
        CHECK(mvfxInstance_SetTransform(s_context, s_instance, origin, quarter, 1.5f, false) ==
                  mvfx_success,
              "moved");
        Steps(1);
        const float center[2] = {world != 0 ? 0.5f : -0.1f, world != 0 ? 0.3f : 0.0f};
        float radius = world != 0 ? 0.45f : 0.3f;
        for (int32_t i = 0; i < 1024; ++i)
        {
            float x = ((float)(i % 32) + 0.5f - 16.0f) * 0.1f - center[0];
            float y = ((float)(i / 32) + 0.5f - 16.0f) * 0.1f - center[1];
            float gap = sqrtf(x * x + y * y) - radius;
            // Cells within float's rounding of the surface are not judged.
            right = right && (fabsf(gap) < 1e-4f || (state->solid[i] != 0) == (gap < 0.0f));
        }
        mvfxDestroyContext(s_context);
    }
    CHECK(right, "an obstacle in the instance's frame");
}

static void TestTurning(void)
{
    // A host's box turning about Z at 2 radians a second about the middle
    // of a 2D gas of 32 by 32: each face beside it moves as w x r, within
    // 2% of the largest speed (1, at its corners): the velocity is the
    // step's chord over its span, turned from the face's tangent by half
    // the step's turn, 1/64 radian (1.26% seen).
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 32;
    def.size[1] = 32;
    def.pressureCycles = 0;
    def.hostObstacles = true;
    const mvfxCompiledGas* gas = NULL;
    mvfxGasState* state = Open(&def, &gas);
    float worst = 0.0f;
    for (int32_t i = 0; i < 3; ++i)
    {
        mvfxCollider box = Collider(mvfx_colliderBox, 0, 0, mvfx_spaceWorld);
        float half = (float)i / 64.0f;
        box.rotation[2] = sinf(half);
        box.rotation[3] = cosf(half);
        CHECK(mvfxInstance_SetColliders(s_context, s_instance, &box, 1) == mvfx_success,
              "colliders");
        Steps(1);
    }
    for (int32_t k = 0; k < 2; ++k)
    {
        const int32_t m[2] = {32 + (k == 0 ? 1 : 0), 32 + (k == 1 ? 1 : 0)};
        const int32_t stride = k == 0 ? 1 : 32;
        for (int32_t y = 0; y < m[1]; ++y)
        {
            for (int32_t x = 0; x < m[0]; ++x)
            {
                const int32_t at[2] = {x, y};
                int32_t cell = y * 32 + x;
                bool beside = (at[k] > 0 && state->solid[cell - stride] != 0) ||
                              (at[k] < 32 && state->solid[cell] != 0);
                float px = ((float)x + (k == 0 ? 0.0f : 0.5f) - 16.0f) * 0.1f;
                float py = ((float)y + (k == 1 ? 0.0f : 0.5f) - 16.0f) * 0.1f;
                float expected = k == 0 ? -2.0f * py : 2.0f * px;
                float value = state->velocity[k][state->current][y * m[0] + x];
                worst = beside ? fmaxf(worst, fabsf(value - expected)) : worst;
            }
        }
    }
    CHECK(worst <= 0.02f * 2.0f * 0.5f, "a turning box's faces turn with it");
    mvfxDestroyContext(s_context);
}

static void TestPassing(void)
{
    // A local 2D gas whose instance moves right at 1.5 a second past a
    // still box in the world: in the box's frame the obstacle moves left,
    // so its X faces take -1.5 from the second step.
    const mvfxCollider box = Collider(mvfx_colliderBox, 0.2f, 0, mvfx_spaceWorld);
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 32;
    def.size[1] = 32;
    def.pressureCycles = 0;
    def.obstacles = &box;
    def.obstacleCount = 1;
    const mvfxCompiledGas* gas = NULL;
    mvfxGasState* state = Open(&def, &gas);
    bool right = true;
    for (int32_t i = 0; i < 4; ++i)
    {
        const double origin[3] = {1.5 * (double)i / 64.0, 0, 0};
        const float rotation[4] = {0, 0, 0, 1};
        CHECK(mvfxInstance_SetTransform(s_context, s_instance, origin, rotation, 1.0f, false) ==
                  mvfx_success,
              "moved");
        Steps(1);
        float across[2];
        SolidFaces(gas, state, 0, across);
        float speed = i == 0 ? 0.0f : -1.5f;
        right = right && fabsf(across[0] - speed) <= 1e-3f && fabsf(across[1] - speed) <= 1e-3f;
    }
    CHECK(right, "a still wall moves past a moving gas");
    mvfxDestroyContext(s_context);
}

static void TestHeld(void)
{
    // A distance grid as an obstacle: its effect holds it (destroying it
    // is refused as busy) until the effect is destroyed; cells where it is
    // negative are solid.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    static float values[8 * 8];
    for (int32_t i = 0; i < 64; ++i)
    {
        values[i] = (float)(i % 8) - 3.5f;
    }
    mvfxGridDef gridDef = mvfxDefaultGridDef();
    gridDef.size[0] = 8;
    gridDef.size[1] = 8;
    gridDef.values = values;
    mvfxGridId grid = {0};
    CHECK(mvfxCreateGrid(s_context, &gridDef, &grid) == mvfx_success, "grid");
    mvfxCollider field = Collider(mvfx_colliderDistance, 0, 0, mvfx_spaceWorld);
    field.halfExtents[0] = 1.0f;
    field.halfExtents[1] = 1.0f;
    field.halfExtents[2] = 1.0f;
    field.grid = grid;
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.size[0] = 16;
    gas.size[1] = 16;
    gas.obstacles = &field;
    gas.obstacleCount = 1;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.gases = &gas;
    def.gasCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_context, &def, &effect) == mvfx_success, "effect");
    bool right = mvfxDestroyGrid(s_context, grid) == mvfx_errorBusy;
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &s_instance) == mvfx_success, "instance");
    Steps(1);
    const mvfxGasState* state = &mvfxContextInstance(s_context, s_instance)->gases[0];
    // The field crosses 0 at the box's middle along X: its left half solid.
    for (int32_t i = 0; i < 256; ++i)
    {
        int32_t x = i % 16;
        right = right && (state->solid[i] != 0) == (x < 8);
    }
    CHECK(mvfxDestroyInstance(s_context, s_instance) == mvfx_success, "instance gone");
    CHECK(mvfxDestroyEffect(s_context, effect) == mvfx_success, "effect gone");
    right = right && mvfxDestroyGrid(s_context, grid) == mvfx_success;
    CHECK(right, "an obstacle's grid held");
    mvfxDestroyContext(s_context);
}

static void TestRefusals(void)
{
    // Too many obstacles, NULL obstacles, and an invalid one are refused;
    // the most are taken.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
    mvfxCollider many[MVFX_MAX_COLLIDERS + 1];
    for (int32_t c = 0; c <= MVFX_MAX_COLLIDERS; ++c)
    {
        many[c] = Collider(mvfx_colliderSphere, 0, 0, mvfx_spaceWorld);
    }
    bool right = true;
    for (int32_t c = 0; c < 3; ++c)
    {
        mvfxGasDef gas = mvfxDefaultGasDef();
        gas.obstacles = c == 1 ? NULL : many;
        gas.obstacleCount = c == 0 ? MVFX_MAX_COLLIDERS + 1 : 1;
        if (c == 2)
        {
            many[0].radius = 0.0f;
        }
        mvfxEffectDef def = mvfxDefaultEffectDef();
        def.gases = &gas;
        def.gasCount = 1;
        mvfxEffectId effect = {0};
        mvfxResult made = mvfxCreateEffect(s_context, &def, &effect);
        right = right && made == mvfx_errorInvalid;
        many[0].radius = 0.5f;
    }
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.obstacles = many;
    gas.obstacleCount = MVFX_MAX_COLLIDERS;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.gases = &gas;
    def.gasCount = 1;
    mvfxEffectId effect = {0};
    right = right && mvfxCreateEffect(s_context, &def, &effect) == mvfx_success;
    CHECK(right, "the obstacles' refusals");
    mvfxDestroyContext(s_context);
}

int main(void)
{
    TestStill();
    TestFlowPast();
    TestMoving();
    TestPlane();
    TestInstanceFrame();
    TestTurning();
    TestPassing();
    TestHeld();
    TestRefusals();
    return s_failures;
}
