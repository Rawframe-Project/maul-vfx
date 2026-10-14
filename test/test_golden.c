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
    0x121737D10184828Bu, 0x0F6570941A74C8C2u, 0xBF816AA5E7138EDDu, 0xA2FD7613E8FF37D8u,
    0x3E1F1DB588254564u, 0x6078622FF70EA2DEu, 0xCBDD79DA0D42A257u, 0xC18E1DA303498192u,
    0x0A1CCB2C26DE6019u, 0x33A5FDF81629E040u, 0x8B3A0D3CC258E441u, 0x0166F97D0EE52B29u,
    0x09E4BA5F3BFCCE86u, 0x797F5C93042A5ED8u, 0x977074ABA510F7B5u, 0x4475C4EA427FE7FBu,
    0x2679E8F6764394ECu, 0x542234803BF0EF2Bu, 0xBCFE0FC72CD4ACDBu, 0xE6901A26A6988EADu,
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
