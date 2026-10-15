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
    0x3E6B3D35F52769C1u, 0xC8926D2786048372u, 0x69285D6B58F2F1C6u, 0x642954B21D540B9Cu,
    0x481C4E11F7E89FBAu, 0xB2C84A96220BA56Au, 0xCFDC9FB21C0ECD6Fu, 0x3667E9ADD380B47Cu,
    0xE2253C9C904C6B8Au, 0x59C1541D0F1EB260u, 0xA313B0B176EC5EB1u, 0xC12BCF5F2B684729u,
    0xC5B57030D97B761Bu, 0x071E4559CBCDD4D1u, 0xE99FADD156B8814Cu, 0x416CA431389B0C6Fu,
    0x8D94B821D3D6E46Cu, 0x55B0F26DAECC2AC9u, 0x5AF5B8E3212742A7u, 0x00C94AE9BAB03298u,
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
