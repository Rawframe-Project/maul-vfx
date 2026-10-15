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
    0xACB63EDBDAFB4536u, 0x11F9A0AAF37C6AE1u, 0x472948433AD01074u, 0x1290B54C6B19C71Eu,
    0x559E23F0352125A2u, 0x3B42883D25313E9Au, 0xBB3CE034EA1624E5u, 0xD8EAE6F88837725Au,
    0x88A5616F768809E1u, 0x373E64D0A350037Au, 0x4AC0AC2D95F78FE3u, 0xEA019CDA6E5DE44Eu,
    0xD4D5F0296EEEB883u, 0xF91CF3FCB9A1A5AEu, 0x9ED47EBC1E373E20u, 0xCF05508ED2199BA4u,
    0x9AEDEE960394FBB4u, 0xC163783C0F2DFEC6u, 0x9E346B8F9A7B00A1u, 0xE483C4F01E13CF52u,
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
