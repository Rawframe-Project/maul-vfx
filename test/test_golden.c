// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The scene's state hashed after every step against values committed here
// (record mvfx-0001: the same bits on every platform). CI runs it on every
// platform of its matrix; one hash differing anywhere fails. A change meant
// to change results updates the values and says so in the changelog. Run
// it with --print to print the values this build computes.

#include "vfx_scene.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const uint64_t s_golden[SCENE_STEP_COUNT] = {
    0x82F64B3241D779A1u, 0xB56FE88CA9897CE5u, 0x13AEA7D91D2EC42Fu, 0x2C69B5D279FCEE58u,
    0x894B0CECF21794F4u, 0x298E6E087407252Du, 0xAFB77688E4522027u, 0x78DAFD554A86EE7Du,
    0x2F3AB89829212085u, 0x7FDAE7E5E51ED211u, 0xD31BD75021FB9BA2u, 0x6512FAA058083717u,
    0xE65A716B00F70F85u, 0xBFED38B4391D42ACu, 0x85B879AE3680DC87u, 0x5DC7901AE0EF6D37u,
    0x4B2DD5F8F561F1FDu, 0xD448EC257A361ECDu, 0xB210ACC84A31AF01u, 0x99D561E67706ABFCu,
};

int main(int argc, char** argv)
{
    mvfxContextDef def = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_success, "context");
    BuildScene(context);
    bool print = argc > 1 && strcmp(argv[1], "--print") == 0;
    for (int32_t step = 0; step < SCENE_STEP_COUNT; ++step)
    {
        MoveScene(context, step);
        CHECK(mvfxContext_StepTicks(context, s_sceneSteps[step]) == mvfx_success, "step");
        uint64_t hash = HashScene(context);
        if (print)
        {
            printf("    0x%016" PRIX64 "u,\n", hash);
        }
        CHECK(hash == s_golden[step], "the state matches the committed hash");
    }
    mvfxDestroyContext(context);
    return s_failures == 0 ? 0 : 1;
}
