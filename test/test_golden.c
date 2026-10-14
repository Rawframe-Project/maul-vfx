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
    0xE0BE3F6474BDF77Eu, 0x8C49B2C2280E1AFFu, 0x5B13646B20C8FE09u, 0x498BFBDDB9FA4C0Du,
    0x0C53D5BF6ED88287u, 0x423BF564B382A3B8u, 0xDC38FEF0749270CDu, 0xD5A7163C5A35DAB0u,
    0x8EE616A6C6A8C7D0u, 0x6EF3C32384DA8492u, 0xD929F7181FF606C3u, 0x30AAD70E9DF72D8Cu,
    0x7F4EDE19AF3AC385u, 0x2E9B20C1F98326C3u, 0x6791664C1DD82035u, 0x815BF37F27B2D6F2u,
    0xB51DED16EAF0C3D1u, 0x3754DE00A4318957u, 0xF627C74883EC9793u, 0x2B44563188E1332Bu,
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
