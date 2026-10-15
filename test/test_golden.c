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
    0x83606ECBF36759F3u, 0x87E5E21B9C7DDBABu, 0x9E23EC62BB50DE55u, 0x95D7781F98057360u,
    0xA702D9D7A64A92E0u, 0x505799FBD2FB9679u, 0xC5993AB3E2737200u, 0x3AB69B44EF0659B2u,
    0x24DEB1790A9EB657u, 0xB47FA97BA7E2A672u, 0xBC75C815A13DA0ACu, 0x5226114DCA246BF5u,
    0x6627C70CBD7B62DEu, 0x54FEEA2FF23C1FA7u, 0x6EAF581F40A01361u, 0x3E373901C423E0B6u,
    0x1C53A9F65FB4FC1Au, 0xF9954273855888EFu, 0x1666258E2E64A620u, 0x81BE05C7BD325DAAu,
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
