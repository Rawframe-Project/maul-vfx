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
    0x946F8410706FCCBFu, 0x7644A2EC2172959Fu, 0x18FB25EC00E0639Fu, 0xAE69BDADC81E561Eu,
    0xF1163AED92911C29u, 0x68895E9E448F6DF0u, 0xCC732176A9AF1576u, 0xD99A69D268CF095Du,
    0x3A42B5065982E176u, 0xFAD07A95DB38F7F1u, 0xE7158457AC9B274Au, 0x2308D5E709F3CCEAu,
    0x76E3F6A8FCB203C5u, 0x779E164A51ADEBCDu, 0x0AB8840DDA5D870Cu, 0xE063D3C56FC8BE13u,
    0x7163027832031E30u, 0xC316B0E7EEEB0649u, 0x927EA1DDAF3F8711u, 0xB6773D6A8C1E7AD0u,
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
