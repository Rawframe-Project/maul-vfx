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
    0x61EC29C873C4AB3Eu, 0x8E56B6BAAEDC6A7Fu, 0x4B490144D1CA4C27u, 0x1966FE7D9AA47DDEu,
    0x3BE2B8840DB32786u, 0xB9619DE3722930EDu, 0xD96EB46684C3ECDBu, 0xEBC7E468D8051AD5u,
    0x0DF7669E9D362168u, 0x92135C1C61543198u, 0x9678E53A30268C49u, 0x3F369F044C2DA2CBu,
    0xA96DBF8CF08972EBu, 0xBF3097A61A2301A9u, 0x3581C7F7FC9757CBu, 0x4B943A8E7731C5C1u,
    0xF5A5B48D34B7BDD0u, 0x506B4DBB9652A3A6u, 0x167C413C6A35FD5Du, 0x98D43D8E4ED99C12u,
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
