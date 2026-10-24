// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Forces on a gas: a constant force in the world turned into a turned
// box's frame and over a scaled box's scale, one in the instance's frame
// not; a wind's pull exact over the step; a vortex turning the gas about
// its axis (of any length); a strength from a parameter; turbulence the
// same for the same seed and different for another, and scrolling; a
// vector field's grid held and its field taken; the forces' refusals.

#include "test_harness.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/grid.h"
#include "maul-vfx/instance.h"

#include <math.h>
#include <string.h>

static mvfxContext* s_context;

static const float s_unturned[4] = {0, 0, 0, 1};

// A 2D gas of 16 by 12 cells with no pressure, under the forces.
static mvfxGasDef Pushed(const mvfxForce* forces, int32_t count)
{
    mvfxGasDef def = mvfxDefaultGasDef();
    def.size[0] = 16;
    def.size[1] = 12;
    def.pressureCycles = 0;
    def.forces = forces;
    def.forceCount = count;
    return def;
}

#define CELLS (16 * 12)

// Opens an instance, turned and scaled, of an effect of one gas with the
// parameters given.
static void OpenWith(const mvfxGasDef* gas, const float* parameters, int32_t count,
                     const float rotation[4], float scale, uint32_t seed,
                     mvfxInstanceId* instanceOut)
{
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.gases = gas;
    def.gasCount = 1;
    def.parameters = parameters;
    def.parameterCount = count;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_context, &def, &effect) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    memcpy(instanceDef.rotation, rotation, sizeof instanceDef.rotation);
    instanceDef.scale = scale;
    instanceDef.seed = seed;
    CHECK(mvfxCreateInstance(s_context, &instanceDef, instanceOut) == mvfx_success, "instance");
}

// The gas's velocity along its box's axis at its cells' centers.
static void Velocity(mvfxInstanceId instance, int32_t axis, float out[CELLS])
{
    CHECK(mvfxInstance_ReadGas(s_context, instance, 0, (mvfxGasField)(mvfx_gasVelocityX + axis),
                               out, CELLS) == mvfx_success,
          "velocity read");
}

static void Context(void)
{
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    CHECK(mvfxCreateContext(&contextDef, &s_context) == mvfx_success, "context");
}

// Steps the context by n steps of 1/64 s.
static void Steps(int32_t n)
{
    for (int32_t i = 0; i < n; ++i)
    {
        CHECK(mvfxContext_StepTicks(s_context, MVFX_TICKS_PER_SECOND / 64) == mvfx_success, "step");
    }
}

// Whether every one of count values is within tolerance of value.
static bool AllNear(const float* values, int64_t count, float value, float tolerance)
{
    bool near = true;
    for (int64_t i = 0; i < count; ++i)
    {
        near = near && fabsf(values[i] - value) <= tolerance;
    }
    return near;
}

static void TestConstant(void)
{
    // (0, 2, 0) at a strength of 1.5 in the world, one step of 1/64 s: an
    // unturned box's gas speeds along Y by 3/64; turned a quarter about Z,
    // along its X (its +X is the world's +Y); scaled by 2, by half that. In
    // the instance's frame, scaled by 2, along Y by 3/64 still.
    const float quarter[4] = {0, 0, 0.70710678f, 0.70710678f};
    struct Case
    {
        const float* rotation;
        float scale;
        mvfxSimulationSpace space;
        int32_t axis;
        float speed;
    } cases[4] = {{s_unturned, 1.0f, mvfx_spaceWorld, 1, 3.0f / 64.0f},
                  {quarter, 1.0f, mvfx_spaceWorld, 0, 3.0f / 64.0f},
                  {s_unturned, 2.0f, mvfx_spaceWorld, 1, 1.5f / 64.0f},
                  {s_unturned, 2.0f, mvfx_spaceLocal, 1, 3.0f / 64.0f}};
    bool right = true;
    for (int32_t c = 0; c < 4; ++c)
    {
        Context();
        const mvfxForce force = {.kind = mvfx_forceConstant,
                                 .space = cases[c].space,
                                 .vector = {0, 2, 0},
                                 .strength = 1.5f};
        mvfxGasDef def = Pushed(&force, 1);
        mvfxInstanceId instance = {0};
        OpenWith(&def, NULL, 0, cases[c].rotation, cases[c].scale, 1, &instance);
        Steps(1);
        int32_t a = cases[c].axis;
        float along[CELLS];
        float across[CELLS];
        Velocity(instance, a, along);
        Velocity(instance, 1 - a, across);
        right = right && AllNear(along, CELLS, cases[c].speed, 1e-6f) &&
                AllNear(across, CELLS, 0.0f, 1e-6f);
        mvfxDestroyContext(s_context);
    }
    CHECK(right, "a constant force in its space");
}

static void TestWind(void)
{
    // A wind of (1.5, 0, 0) pulling at 3 a second: from rest, the gas's X
    // velocity is 1.5 (1 - e^(-3 t)) after each step of 1/64 s.
    Context();
    const mvfxForce wind = {.kind = mvfx_forceWind, .vector = {1.5f, 0, 0}, .strength = 3.0f};
    mvfxGasDef def = Pushed(&wind, 1);
    mvfxInstanceId instance = {0};
    OpenWith(&def, NULL, 0, s_unturned, 1.0f, 1, &instance);
    bool right = true;
    for (int32_t i = 1; i <= 3; ++i)
    {
        Steps(1);
        float expected = 1.5f * (1.0f - expf(-3.0f * (float)i / 64.0f));
        float u[CELLS];
        Velocity(instance, 0, u);
        right = right && AllNear(u, CELLS, expected, 2e-6f);
    }
    CHECK(right, "a wind's pull exact");
    mvfxDestroyContext(s_context);
}

static void TestVortex(void)
{
    // A vortex about Z through the box's middle at a strength of 2: a step
    // later the gas right of the middle rises and that left of it falls,
    // along the rows either side of it; an axis of (0, 0, 2) is the same
    // axis, bit for bit.
    static float kept[CELLS];
    bool right = true;
    for (int32_t r = 0; r < 2; ++r)
    {
        Context();
        const mvfxForce vortex = {
            .kind = mvfx_forceVortex, .vector = {0, 0, r == 0 ? 1.0f : 2.0f}, .strength = 2.0f};
        mvfxGasDef def = Pushed(&vortex, 1);
        mvfxInstanceId instance = {0};
        OpenWith(&def, NULL, 0, s_unturned, 1.0f, 1, &instance);
        Steps(1);
        float v[CELLS];
        Velocity(instance, 1, v);
        for (int32_t row = 5; row <= 6; ++row)
        {
            for (int32_t x = 0; x < 16; ++x)
            {
                float value = v[row * 16 + x];
                right = right && (x < 8 ? value < 0.0f : value > 0.0f);
            }
        }
        right = right && (r == 0 || memcmp(kept, v, sizeof kept) == 0);
        memcpy(kept, v, sizeof kept);
        mvfxDestroyContext(s_context);
    }
    CHECK(right, "a vortex turns the gas");
}

static void TestParameter(void)
{
    // A constant force whose strength is parameter 0's x: its default 1
    // speeds the gas along Y by 2/64 a step; set to 3, by 6/64.
    Context();
    const mvfxForce force = {.kind = mvfx_forceConstant,
                             .vector = {0, 2, 0},
                             .strength = 1.0f,
                             .strengthFromParameter = true,
                             .parameter = 0};
    const float defaults[4] = {1, 0, 0, 0};
    mvfxGasDef def = Pushed(&force, 1);
    mvfxInstanceId instance = {0};
    OpenWith(&def, defaults, 1, s_unturned, 1.0f, 1, &instance);
    Steps(1);
    float v[CELLS];
    Velocity(instance, 1, v);
    bool right = AllNear(v, CELLS, 2.0f / 64.0f, 1e-6f);
    const float three[4] = {3, 0, 0, 0};
    CHECK(mvfxInstance_SetParameter(s_context, instance, 0, three) == mvfx_success, "set");
    Steps(1);
    Velocity(instance, 1, v);
    right = right && AllNear(v, CELLS, 8.0f / 64.0f, 1e-6f);
    CHECK(right, "a strength from a parameter");
    mvfxDestroyContext(s_context);
}

static void TestTurbulence(void)
{
    // Turbulence mixing in each instance's seed: two instances of seed 7
    // stir the gas the same, bit for bit, and not still; one of seed 9
    // differently.
    Context();
    const mvfxForce turbulence = {.kind = mvfx_forceTurbulence,
                                  .strength = 4.0f,
                                  .noise = {.frequency = 2.0f,
                                            .octaves = 2,
                                            .lacunarity = 2,
                                            .gain = 0.5f,
                                            .seedPerInstance = true}};
    mvfxGasDef def = Pushed(&turbulence, 1);
    const uint32_t seeds[3] = {7, 7, 9};
    mvfxInstanceId instances[3] = {0};
    for (int32_t i = 0; i < 3; ++i)
    {
        OpenWith(&def, NULL, 0, s_unturned, 1.0f, seeds[i], &instances[i]);
    }
    Steps(2);
    size_t bytes = sizeof(float) * CELLS;
    static float u[3][CELLS];
    for (int32_t i = 0; i < 3; ++i)
    {
        Velocity(instances[i], 0, u[i]);
    }
    bool same = memcmp(u[0], u[1], bytes) == 0;
    bool other = memcmp(u[0], u[2], bytes) != 0;
    bool stirred = !AllNear(u[0], CELLS, 0.0f, 1e-6f);
    CHECK(same && other && stirred, "turbulence by its seed");
    mvfxDestroyContext(s_context);
    // Scrolling at (1, 0, 0) a second, the same turbulence parts from the
    // still one by the second step, the noise taken at each substep's time.
    Context();
    mvfxForce scrolled = turbulence;
    scrolled.noise.scroll[0] = 1.0f;
    mvfxGasDef moved = Pushed(&scrolled, 1);
    mvfxInstanceId still = {0};
    mvfxInstanceId moving = {0};
    OpenWith(&def, NULL, 0, s_unturned, 1.0f, 7, &still);
    OpenWith(&moved, NULL, 0, s_unturned, 1.0f, 7, &moving);
    Steps(2);
    Velocity(still, 0, u[0]);
    Velocity(moving, 0, u[1]);
    CHECK(memcmp(u[0], u[1], bytes) != 0, "turbulence scrolls in time");
    mvfxDestroyContext(s_context);
}

static void TestField(void)
{
    // A vector field of (0.5, 0, 0) everywhere in a box larger than the
    // gas, at a strength of 2: the gas speeds along X by 1/64 a step; the
    // effect holds its grid until it is destroyed.
    Context();
    static const float values[2 * 2 * 2 * 3] = {0.5f, 0, 0, 0.5f, 0, 0, 0.5f, 0, 0, 0.5f, 0, 0,
                                                0.5f, 0, 0, 0.5f, 0, 0, 0.5f, 0, 0, 0.5f, 0, 0};
    mvfxGridDef gridDef = mvfxDefaultGridDef();
    gridDef.size[0] = 2;
    gridDef.size[1] = 2;
    gridDef.size[2] = 2;
    gridDef.components = 3;
    gridDef.values = values;
    mvfxGridId grid = {0};
    CHECK(mvfxCreateGrid(s_context, &gridDef, &grid) == mvfx_success, "grid");
    const mvfxForce field = {.kind = mvfx_forceVectorField,
                             .strength = 2.0f,
                             .grid = grid,
                             .boxSize = {4, 4, 4},
                             .boxRotation = {0, 0, 0, 1}};
    mvfxGasDef def = Pushed(&field, 1);
    mvfxEffectDef effectDef = mvfxDefaultEffectDef();
    effectDef.gases = &def;
    effectDef.gasCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(s_context, &effectDef, &effect) == mvfx_success, "effect");
    bool right = mvfxDestroyGrid(s_context, grid) == mvfx_errorBusy;
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(s_context, &instanceDef, &instance) == mvfx_success, "instance");
    Steps(1);
    float u[CELLS];
    Velocity(instance, 0, u);
    right = right && AllNear(u, CELLS, 1.0f / 64.0f, 1e-6f);
    CHECK(mvfxDestroyInstance(s_context, instance) == mvfx_success, "instance gone");
    CHECK(mvfxDestroyEffect(s_context, effect) == mvfx_success, "effect gone");
    right = right && mvfxDestroyGrid(s_context, grid) == mvfx_success;
    CHECK(right, "a vector field on a gas");
    mvfxDestroyContext(s_context);
}

static void TestRefusals(void)
{
    // Too many forces, NULL forces, a kind out of range and a strength from
    // a parameter the effect lacks are refused.
    Context();
    mvfxForce forces[MVFX_MAX_FORCES + 1];
    for (int32_t f = 0; f <= MVFX_MAX_FORCES; ++f)
    {
        forces[f] = (mvfxForce){.kind = mvfx_forceConstant, .vector = {0, 1, 0}};
    }
    bool right = true;
    for (int32_t c = 0; c < 4; ++c)
    {
        mvfxGasDef gas = Pushed(c == 1 ? NULL : forces, c == 0 ? MVFX_MAX_FORCES + 1 : 1);
        forces[0].kind = c == 2 ? 9 : mvfx_forceConstant;
        forces[0].strengthFromParameter = c == 3;
        mvfxEffectDef def = mvfxDefaultEffectDef();
        def.gases = &gas;
        def.gasCount = 1;
        mvfxEffectId effect = {0};
        right = right && mvfxCreateEffect(s_context, &def, &effect) == mvfx_errorInvalid;
    }
    forces[0].kind = mvfx_forceConstant;
    forces[0].strengthFromParameter = false;
    mvfxGasDef gas = Pushed(forces, MVFX_MAX_FORCES);
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.gases = &gas;
    def.gasCount = 1;
    mvfxEffectId effect = {0};
    right = right && mvfxCreateEffect(s_context, &def, &effect) == mvfx_success;
    CHECK(right, "the forces' refusals");
    mvfxDestroyContext(s_context);
}

int main(void)
{
    TestConstant();
    TestWind();
    TestVortex();
    TestParameter();
    TestTurbulence();
    TestField();
    TestRefusals();
    return s_failures;
}
