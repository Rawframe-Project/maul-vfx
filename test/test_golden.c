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
    0x47F5DD3D6C967FC1u, 0xA42C4CD307B0FBE0u, 0xE90439DD0C5F9FFCu, 0xCA5C9AAE048D041Au,
    0x593803FAADE3F479u, 0x4E1BBAEA5A423F94u, 0x22A8987D9AF431DAu, 0x84BF5DDA3325BCDFu,
    0xF17D11BF5DD8DBA4u, 0x8F96E471DEBC8986u, 0xCCF9658802B77A0Eu, 0xDAE0372B37E0CE80u,
    0xA9BD5097C3B271BBu, 0x27241BE54C635B47u, 0x10F1C4031D000C4Eu, 0x8C476E415A97BA54u,
    0x3761C5732F51ADDCu, 0xD949ACA8AF51727Eu, 0x6C8AEE678895B791u, 0xCA953AA3D9566787u,
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
