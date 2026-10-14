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
    0xF18966AEC9738FF3u, 0x30499980426E5478u, 0x982A5266F3C12C77u, 0x1B6CF2D1A1D418B2u,
    0xB963AFA1405A06C7u, 0x9E5434F4B7CF0A82u, 0x2952ED791245DA01u, 0xBE0D2E818133E1F2u,
    0x685B484BE94AEB9Bu, 0xBFEB3DB78FDAC382u, 0xEDC3407893CB8AE5u, 0x40A6467CDF2EEACDu,
    0xE52494A00D03425Bu, 0xB7176A5B8E75E20Cu, 0x56CA91108EE51D72u, 0x1020C0477FB9F741u,
    0xF68F544796E99A92u, 0x4B8A469DECFC4397u, 0xA71A2DF472C50151u, 0xFE671BB793F4FA39u,
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
