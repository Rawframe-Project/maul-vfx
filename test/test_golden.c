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
    0x44993AC9E0C36A3Du, 0xA9100100E9B88F46u, 0xE0CC87DFF3EEB465u, 0x5681F4AFD5D63D4Eu,
    0x689BA70E91A8AC74u, 0xF4A262DE84DF46D3u, 0x689F20BA4EF9C457u, 0x387B6EBBD288C176u,
    0x2DABB9CE01338502u, 0xA9C4E05A54FACE38u, 0x6B3D9BD8F0D66C17u, 0x0E0511F7FA833EBCu,
    0x4FE414C557AECD4Cu, 0xA283AAEC41A3D345u, 0xCE349C4D2FBCEB18u, 0x899C15A4BDA4FF12u,
    0xCD212FFECA221163u, 0xB741B9ECF3D5FD31u, 0xD5C98E38D4041744u, 0x9462E22D6DE6FB02u};

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
