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
    0xEBFBD7607CE588F6u, 0xA2E40A1FCB09027Eu, 0x42DBDEF4A54321CEu, 0x8FC8E3C3D428CB07u,
    0xFA5246E0B31242ECu, 0xE24F31A89A94A105u, 0x08B875398BB2917Au, 0x16E14090F17BFA65u,
    0x2CC0F80E20B1B4BBu, 0xB335446459CF9730u, 0xABAFEDFB22134448u, 0x11365B8EAC5CA1EFu,
    0x7C36A42CC9178F16u, 0x9A276AFBB72B14ABu, 0xAF77D544540641A0u, 0x9241926AA668C802u,
    0x4D737B779C3D78B7u, 0xED6111F298DA41DEu, 0xA813B25968FEE791u, 0x465E76D47924B16Du,
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
