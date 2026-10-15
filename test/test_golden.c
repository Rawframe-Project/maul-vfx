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
    0x1B56F804CEAD30E1u, 0x5F930A0F45C10920u, 0x162E47A6810335C5u, 0x23D549E570EADAE8u,
    0x5BA9F20AB84B94CCu, 0x232F91913A0B9618u, 0xC57D2E317CA8170Fu, 0x3A6F2B8DBB9CC8E3u,
    0x814E3912C483C61Fu, 0x07504D1BFC111C95u, 0x25003B0B278EA1D5u, 0x0ACFF6F6EE6D9065u,
    0xCD576AFD655A99E8u, 0x5ABE72286C380748u, 0x2CDDA5479AECAD5Au, 0xFB01B43E33D502EEu,
    0x0676C9D9A75415B1u, 0xE42FF1E334738C39u, 0x790670D6233C9CCDu, 0xE931BB21446F7481u,
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
