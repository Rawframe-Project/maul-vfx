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
    0xCEB8DA7818C70361u, 0xE7498E5330CE3018u, 0x9D77B6B51C2A47C5u, 0xE47359E2174A0519u,
    0x9E123CC257CB0F27u, 0x3F409A587CA4CC6Fu, 0xCB6F644728A7D267u, 0x2E5F520C3260CFD4u,
    0x3B1192FCE4C21D92u, 0xFC3A0B34F1AC3BD9u, 0xA7A151EEAFB83A8Cu, 0xA6A8477DB6E67ACDu,
    0xED5099A96B590C4Fu, 0x75B6C5A21BA78703u, 0x55EA75B7D3282FCEu, 0x78789463343B9F9Au,
    0x20257FFEB3ECCA80u, 0xE730E7917B8E387Eu, 0x2A1B70B0F1270984u, 0xE63AE433461D3A21u,
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
