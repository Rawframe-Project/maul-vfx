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
    0x63017F9525B6DF52u, 0x661FD7E05A692212u, 0xE58F1289AFD41DCEu, 0x222882B388182798u,
    0x038BF9CA57ABB3F1u, 0xEE1B9C20EDA9484Au, 0x2702A97C4BF2246Cu, 0x8EA788D3F521EF9Au,
    0x8703753AACA3808Fu, 0x31D2D571AE134458u, 0x9426E900F3F3635Eu, 0xCBB771B6CBD05C0Eu,
    0xC2CEF1F9728EA15Du, 0x38F2B253F140E8FFu, 0x0D8A5CD2AE11B44Cu, 0x1C5F76586F96CE5Bu,
    0x422980D8097C7CA2u, 0xD022AB3BF7E14031u, 0x85820434948C544Au, 0x235964A56279EAF8u,
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
