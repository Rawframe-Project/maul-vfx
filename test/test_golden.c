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
    0x393A698C34FDF01Fu, 0x3D3EB5B4CE6F2A6Eu, 0xF3BFA355E29AA53Eu, 0xCC4F236A515ADDCFu,
    0xB66EEA3F872D4888u, 0x1CF80B92A065D980u, 0x364E865E4715989Bu, 0x87992464375B1B10u,
    0x0567C8A0EA1924E3u, 0xE7E619335960E1CEu, 0x6C0B60BD0D80668Au, 0xA1F65F26E04D2291u,
    0x73E7264ADB812CA1u, 0x8285FF08D9C0377Fu, 0x47374B1CEFA8DE8Fu, 0x4F2F02F82EA272E2u,
    0x1EDD450CE9BC70AEu, 0x6C6FA35297A87B93u, 0xBD396011BB1A1A23u, 0x2173527A3561917Eu,
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
