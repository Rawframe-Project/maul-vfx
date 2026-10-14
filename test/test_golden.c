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
    0xD175887A16CCEE23u, 0xC7CE16BA822FB6C8u, 0xEADA8EEA52F35EC2u, 0x90B82A12F17D05CFu,
    0x2E96CE27C7726105u, 0x16B79F2DFF1CC6CEu, 0x30680F9E65CFAF7Fu, 0xAF557A5E972AE913u,
    0x384137EDA27CB89Au, 0x607D57106B9A91CBu, 0x246FC08261F67BD7u, 0x53BF57FF1E67DA4Au,
    0x0003162417F80758u, 0x4551BEC045D3FBFFu, 0xA9DEB606CA70ADB6u, 0xA61DF7D18DCF5B5Eu,
    0x47DE61A760B69174u, 0x001084F762C0D5DDu, 0xBF1AACD7C4288A45u, 0x825A13E22F109FDEu,
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
