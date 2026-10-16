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
    0x10C3187933F5F78Bu, 0x241109CFE2303803u, 0x2F6C4D503C5E9B79u, 0x2B75928EA6B83B68u,
    0x0944D64C9563A29Du, 0x5412204B2C7495EBu, 0x18549540FDB43E4Bu, 0x68955EDA037C6565u,
    0xCAA57F41659BA8D8u, 0x16DADE4A60352E2Eu, 0x466725875FD2087Eu, 0xD06BD15996A5FF34u,
    0x900B97883CB398E0u, 0x0F6895DA0399E719u, 0xFD8D4D4B18BFA5C6u, 0x2C2261DBD0E5C03Eu,
    0x701BC37238532216u, 0xE59F4E836FFC2BFAu, 0x6B86ECB557779BB6u, 0x4DB4C64D64872D6Au};

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
