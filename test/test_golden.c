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
    0x915F7FEF8C0C7B97u, 0x13F0EAD02FC5276Fu, 0xADA92837AB1358F1u, 0x39F92A6C9DD6AF9Cu,
    0xFE63D760E3EA4B56u, 0x03F64B762A11BF40u, 0x649DE0BAB908170Bu, 0x960209774CD33761u,
    0xB36B27A12F6DDCE4u, 0x1DF3744DD86E3598u, 0x4185E152ECC1A7DDu, 0x6B45A682ECB84888u,
    0x27083BA80B0CDB68u, 0x258884260C5BB6F1u, 0x078B90EC20F3E8CCu, 0x3A6BB36353CCE297u,
    0xD4F76D2656B4BA2Eu, 0x550DDE5ACE01920Au, 0x30F30E24779E167Cu, 0xDF8D54D5D4BB8302u,
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
