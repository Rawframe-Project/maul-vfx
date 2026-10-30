// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The projection's own handling of solids: nothing within a solid reaches
// the solve. A step sets every face beside or within a solid to the
// solid's velocity before it projects, so only the projection called on
// its own shows the solve ignores what those faces hold.

#include "gas_pressure.h"
#include "instance.h"
#include "instance_api.h"
#include "test_harness.h"

#include "maul-vfx/effect.h"
#include "maul-vfx/instance.h"

#include <string.h>

// A hash's float in [-1, 1).
static float Noise(uint32_t i)
{
    uint32_t h = (i + 1u) * 2654435761u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    return (float)(h & 0xFFFFFFu) / 8388608.0f - 1.0f;
}

// Whether the face across k at index f of a 2D gas of 32 by 32 lies
// between two solid cells.
static bool Buried(const mvfxGasState* state, int32_t k, int32_t f)
{
    const int32_t m0 = 32 + (k == 0 ? 1 : 0);
    const int32_t at[2] = {f % m0, f / m0};
    const int32_t stride = k == 0 ? 1 : 32;
    int32_t cell = at[1] * 32 + at[0];
    return at[k] > 0 && at[k] < 32 && state->solid[cell - stride] != 0 && state->solid[cell] != 0;
}

static void TestBuried(void)
{
    // A 2D gas of 32 by 32, open walls, a still box marked solid by a step:
    // noise for a velocity, projected by 8 cycles from 0 twice, once with
    // the faces between two solid cells 0 and once with noise of 5 there.
    // The faces any gas touches come out the same, bit for bit.
    mvfxContextDef contextDef = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&contextDef, &context) == mvfx_success, "context");
    const mvfxCollider box = {.kind = mvfx_colliderBox,
                              .space = mvfx_spaceWorld,
                              .normal = {0, 1, 0},
                              .radius = 0.5f,
                              .halfExtents = {0.4f, 0.3f, 1.0f},
                              .rotation = {0, 0, 0, 1}};
    mvfxGasDef gasDef = mvfxDefaultGasDef();
    gasDef.size[0] = 32;
    gasDef.size[1] = 32;
    gasDef.pressureCycles = 8;
    gasDef.obstacles = &box;
    gasDef.obstacleCount = 1;
    mvfxEffectDef def = mvfxDefaultEffectDef();
    def.gases = &gasDef;
    def.gasCount = 1;
    mvfxEffectId effect = {0};
    CHECK(mvfxCreateEffect(context, &def, &effect, NULL) == mvfx_success, "effect");
    mvfxInstanceDef instanceDef = mvfxDefaultInstanceDef();
    instanceDef.effect = effect;
    mvfxInstanceId instance = {0};
    CHECK(mvfxCreateInstance(context, &instanceDef, &instance) == mvfx_success, "instance");
    CHECK(mvfxContext_StepTicks(context, MVFX_TICKS_PER_SECOND / 64) == mvfx_success, "step");
    mvfxInstance* live = mvfxContextInstance(context, instance);
    const mvfxCompiledGas* gas = &live->effect->gases[0];
    mvfxGasState* state = &live->gases[0];
    static float kept[2][33 * 32];
    bool right = true;
    for (int32_t r = 0; r < 2; ++r)
    {
        for (int32_t k = 0; k < 2; ++k)
        {
            for (int32_t f = 0; f < 33 * 32; ++f)
            {
                float noise = Noise((uint32_t)(f * 2 + k));
                state->velocity[k][state->current][f] =
                    Buried(state, k, f) ? (r == 0 ? 0.0f : 5.0f * noise) : noise;
            }
        }
        memset(state->levels[0].pressure, 0, sizeof(float) * 1024);
        mvfxProjectGas(gas, state, NULL, NULL);
        for (int32_t k = 0; k < 2; ++k)
        {
            for (int32_t f = 0; f < 33 * 32; ++f)
            {
                float value = state->velocity[k][state->current][f];
                right = right && (r == 0 || Buried(state, k, f) || value == kept[k][f]);
                kept[k][f] = value;
            }
        }
    }
    CHECK(right, "nothing in a solid reaches the solve");
    mvfxDestroyContext(context);
}

int main(void)
{
    TestBuried();
    return s_failures;
}
