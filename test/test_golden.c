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
    0x2D043A561868B84Fu, 0x945070CD17F86558u, 0x3CC17BF0CF625BE2u, 0xCE977321EBF01D22u,
    0xD0FDADFE8D63B29Du, 0xAC2659522D305336u, 0x08A513CC3C456260u, 0xA35AA883154D754Du,
    0xE7D782D2A24A5471u, 0x1C489625DC422521u, 0xBD44D9796E4F4D5Au, 0xA2B703580D316B78u,
    0xF21D28752E754329u, 0xC8E52CF3C1F68706u, 0xB7604ABF974F5D51u, 0xE162603022F0ACD0u,
    0xD936AC8A58DBE99Au, 0xDE411AB49317E15Fu, 0x34C4502138B98990u, 0x49A04C44A365E3ABu,
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
