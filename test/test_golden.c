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
    0xF18966AEC9738FF3u, 0xF2B0ACAC91DD48E1u, 0x14ADFA6EEFBAD2D6u, 0x2DF9F92B4A1A34EDu,
    0x618B1CAFCD0382EEu, 0x5EEC0362136F4343u, 0x9C625FAEB780C058u, 0xCD831B3E3F2F80E1u,
    0x7E841F942CF7DC8Du, 0xFD48AA4250B177DFu, 0xE90865DE57AA6EFCu, 0x40A6467CDF2EEACDu,
    0x316FEB6E17DE5553u, 0xB7176A5B8E75E20Cu, 0xDB9401A7ACAF1AF6u, 0x595C655D28CB1ACCu,
    0xA4E50F743F0A49E5u, 0x40D7CBF7781C52E8u, 0x934C048F98E2EAB4u, 0x56CD6A133C569B17u,
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
