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
    0x55FC55AC670498FDu, 0xA2DA7F32A030B78Au, 0xD51724E65C27EC2Eu, 0xA9DB84FC8EEA766Au,
    0xE3E66FD587014F33u, 0xDA7F82A5BE4E9377u, 0xD8EDEAA5568C6BAFu, 0x2027A4027042FD6Fu,
    0xDC105781AE5343C1u, 0x41E598227F8623AFu, 0xD86675060AD4BFBDu, 0xA9228D2180FB9B37u,
    0x329BA2C2D34A2E3Fu, 0xBB389ED43663E7CDu, 0x02F25708758C7C7Eu, 0xEC5EAC8B33E4AE19u,
    0x0384529CC3C4CA18u, 0xDF969AFD923B00CFu, 0xCD0F31B9656A9D27u, 0xFBF2D58A33106528u};

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
