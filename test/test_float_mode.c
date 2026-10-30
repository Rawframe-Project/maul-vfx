// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The library's floating-point mode through the main step: a gas scene
// stepped with the host's thread in flush-to-zero, denormals-are-zero and
// round toward zero gives the bits it gives in the default mode, both a
// simulated instance's gas and a planned instance's gas plan, and the
// host's mode is left as the host set it.

#include "host_mode.h"
#include "test_harness.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/gas.h"
#include "maul-vfx/instance.h"
#include "maul-vfx/plan.h"

#include <string.h>

enum
{
    SIDE = 32,
    STEPS = 30
};

// What a run of the scene leaves: a hash of its gas's density and
// temperature after every step, and its planned twin's last gas plan's
// gravity and obstacle in the box's frame.
typedef struct Run
{
    uint64_t hash;
    float gravity[3];
    float center[3];
    float rotation[4];
    float halfExtents[3];
} Run;

// A smoke gas of SIDE by SIDE cells in the world, a box source of hot,
// dense fuel that burns and expands, buoyancy, confinement, MacCormack's
// advection and a box obstacle, in an instance and a planned twin turned
// and scaled by amounts no float holds exactly; stepped at 60 a second,
// in the host's mode when hostMode.
static Run Scene(bool hostMode)
{
    static const mvfxGasSource source = {.shape = mvfx_gasBox,
                                         .center = {-0.6f, -1.0f, 0.0f},
                                         .halfExtents = {0.3f, 0.2f, 1.0f},
                                         .density = 1.0f,
                                         .temperature = 3.0f,
                                         .fuel = 1.0f};
    static const mvfxCollider obstacle = {.kind = mvfx_colliderBox,
                                          .center = {0.3f, 0.2f, 0.0f},
                                          .halfExtents = {0.3f, 0.2f, 1.0f},
                                          .rotation = {0.0f, 0.0f, 0.0998334f, 0.9950042f}};
    mvfxGasDef gas = mvfxDefaultGasDef();
    gas.size[0] = SIDE;
    gas.size[1] = SIDE;
    gas.space = mvfx_gasWorld;
    gas.sources = &source;
    gas.sourceCount = 1;
    gas.heatLift = 0.7f;
    gas.densityWeight = 0.3f;
    gas.vorticity = 1.5f;
    gas.advection = mvfx_gasMacCormack;
    gas.ignitionTemperature = 1.0f;
    gas.burnRate = 2.0f;
    gas.burnHeat = 3.0f;
    gas.burnSmoke = 0.5f;
    gas.burnExpansion = 0.3f;
    gas.densityDecay = 0.1f;
    gas.obstacles = &obstacle;
    gas.obstacleCount = 1;
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.gases = &gas;
    def.gasCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceId instances[2] = {{0}, {0}};
    for (int32_t k = 0; k < 2; ++k)
    {
        mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
        instanceDef.effect = effect;
        instanceDef.planned = k == 1;
        instanceDef.origin[0] = 0.1;
        instanceDef.rotation[2] = 0.2955202f;
        instanceDef.rotation[3] = 0.9553365f;
        instanceDef.scale = 1.3f;
        CHECK(mvfxCreateInstance(context, &instanceDef, &instances[k]) == mvfx_success, "instance");
    }
    Run run = {.hash = 0};
    static float density[SIDE * SIDE];
    static float temperature[SIDE * SIDE];
    for (int32_t s = 0; s < STEPS; ++s)
    {
#if HOST_MODE_SETTABLE
        uint64_t saved = hostMode ? SetHostMode() : 0;
        uint64_t set = ReadHostMode();
#endif
        CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 60) == mvfx_success, "step");
#if HOST_MODE_SETTABLE
        CHECK(ReadHostMode() == set, "the host's mode left as it was");
        if (hostMode)
        {
            RestoreHostMode(saved);
        }
#else
        (void)hostMode;
#endif
        CHECK(mvfxInstance_ReadGas(context, instances[0], 0, mvfx_gasDensity, density,
                                   SIDE * SIDE) == mvfx_success &&
                  mvfxInstance_ReadGas(context, instances[0], 0, mvfx_gasTemperature, temperature,
                                       SIDE * SIDE) == mvfx_success,
              "read");
        run.hash = mvfxHash64(run.hash, density, sizeof density);
        run.hash = mvfxHash64(run.hash, temperature, sizeof temperature);
    }
    static mvfxGasPlan plan;
    CHECK(mvfxInstance_GetGasPlan(context, instances[1], 0, &plan) == mvfx_success &&
              plan.obstacleCount == 1,
          "plan");
    memcpy(run.gravity, plan.gravity, sizeof run.gravity);
    memcpy(run.center, plan.obstacles[0].center, sizeof run.center);
    memcpy(run.rotation, plan.obstacles[0].rotation, sizeof run.rotation);
    memcpy(run.halfExtents, plan.obstacles[0].halfExtents, sizeof run.halfExtents);
    mvfxDestroyContext(context);
    return run;
}

static void TestGasStep(void)
{
    Run plain = Scene(false);
    Run host = Scene(true);
    CHECK(plain.hash == host.hash, "a gas stepped in the library's mode");
    CHECK(memcmp(plain.gravity, host.gravity, sizeof plain.gravity) == 0 &&
              memcmp(plain.center, host.center, sizeof plain.center) == 0 &&
              memcmp(plain.rotation, host.rotation, sizeof plain.rotation) == 0 &&
              memcmp(plain.halfExtents, host.halfExtents, sizeof plain.halfExtents) == 0,
          "a gas planned in the library's mode");
}

int main(void)
{
    TestGasStep();
    return s_failures;
}
