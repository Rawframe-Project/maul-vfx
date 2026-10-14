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
    0x2E96CE27C7726105u, 0x16B79F2DFF1CC6CEu, 0x30680F9E65CFAF7Fu, 0x406C4BF9B327179Cu,
    0xA18823FAAF260477u, 0x70440F276AD69134u, 0xF6625E90DDEB30C1u, 0x513632EDDEBC587Fu,
    0xBDEDAAF4EDCFE8EEu, 0x6AC6F4FFA1A06B4Du, 0x6FB523837DA1D4DBu, 0xC3EB9CDAA59F0146u,
    0x5C521FB53A2711EBu, 0x70677586DA5393F9u, 0x0B5C890898CD26B6u, 0x8563BB97F8D1C425u,
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
