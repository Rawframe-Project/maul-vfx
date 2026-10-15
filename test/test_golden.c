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
    0x5A9E390A50EE224Du, 0x60640D5B01EB424Cu, 0x066B5A221E26C67Cu, 0x35EB9EFCD756FA13u,
    0xCB9A7109A24FAACAu, 0x92F174DCA742AE18u, 0x29C783D437E2D3B9u, 0xC9DD2732F30ED53Bu,
    0x108FB7216FE8F58Bu, 0xC8D357A66F51509Fu, 0x62FC84D337B6CCF6u, 0x23B67FB58B0EB776u,
    0x3B59D6E8D07DFD3Du, 0x8E554D07C0BEDB34u, 0x202710862E664BE6u, 0x6CA0ABE549B99D44u,
    0xC979BA7A912FE8C7u, 0x3875A198A9D9BCD3u, 0x924315EE92CAE4CFu, 0x135A0D7BD945BBBEu};

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
