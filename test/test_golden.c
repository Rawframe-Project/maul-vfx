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
    0xF74AD9CC11B7FAABu, 0x112ED9244967F19Du, 0xABE4A99498D9EE50u, 0x4476872460CA19F7u,
    0xB639D2195232EBF6u, 0x9F5A59272A4886B8u, 0xE8F03BFCA3E258E7u, 0x7C2DDC4FF5ADB969u,
    0x98B28DF1FD7CD609u, 0xFD8A60C97254B92Cu, 0x9E23A18A99F1300Fu, 0xD5312111D7F76991u,
    0xB549A74F9B0D62B1u, 0xCD6F1613223262C4u, 0xC1B89C7753C4E179u, 0xA91B1BE44FEA2FB1u,
    0x2CD9D0A470AA5D55u, 0x813162012EE82BDFu, 0x5841473195A3CBD0u, 0x71C6F13C2936913Bu};

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
