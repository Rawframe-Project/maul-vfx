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
    0x946F8410706FCCBFu, 0x1B86685D89AC52A5u, 0xE2DA408B4617E2AFu, 0x4AD3680BB55E0A69u,
    0x8957F877E4ACC257u, 0x49D4AFC3502ED71Eu, 0x20CB8C2E5870827Bu, 0x82F0A69E04DE82C0u,
    0x0B0E63B47D40DEEFu, 0x8B0A57D4BDFA7548u, 0xA4D89B39BCF6C012u, 0xC19071325E717553u,
    0x7C8CB302971D5119u, 0x0F3FCE1BDAE9F7C0u, 0xB64DB185EC6E6B4Bu, 0x052AC44A0DCBB8A1u,
    0x92AA31E7B9470F84u, 0xA66D7531BA21A415u, 0x875B0509B51119D2u, 0x86AEDA72E10698F2u,
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
