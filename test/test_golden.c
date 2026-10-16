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
    0x97DB051C257805B2u, 0xF93C799C8AD2C63Fu, 0x493C4D230ABDCB8Cu, 0xDEC68897E9DA16BBu,
    0x2AB1BB9B54C87517u, 0x5937264A3C97C1D4u, 0xC48AD3392ED2608Eu, 0x2E4C38F77B140559u,
    0xAA11AEB29BE8AD78u, 0x2847EA349BCD6D98u, 0x9F0BA551673637E8u, 0x717DB4883B4C75D2u,
    0x6A5940DACDC912B5u, 0xD3049A307D403156u, 0x76C0CC58E3C4C7CFu, 0x55199F4F6D9F680Au,
    0xE6C87AD1A543DF45u, 0xC7ACB29C5DF6DD5Au, 0xC4CA80E8D4CAE558u, 0xBC6A5A4B0A49E9A2u};

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
