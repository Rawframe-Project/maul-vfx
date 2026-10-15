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
    0xFDEEE6BF303E17BDu, 0x3F484DD7D3BD436Bu, 0xEF8AA5CF7000E217u, 0xC918C93865F19502u,
    0x37635443DFEFC2D1u, 0x9DCAC892CFD521FFu, 0xEE0A9B43D87FFE79u, 0xE6499C22244E1E15u,
    0xAB001FA08E2FC22Cu, 0xA391B1D315587CFAu, 0x9E293ACAD33DDDFDu, 0x4501A130ED4452A0u,
    0xE28D390A087CBDD5u, 0xD64675835C4BA59Au, 0xF604E1614B444D58u, 0x968C9C9742257E8Du,
    0xFA7885A3B3E5E9C0u, 0xF74D81101069AFBEu, 0xE8CA4A22580421C6u, 0x22D9AB794AEDB29Au,
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
