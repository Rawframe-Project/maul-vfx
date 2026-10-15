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
    0xA7506FD47407AC9Du, 0x9F2CDFC7F9D7BE68u, 0xE4692CF9C8FFF67Fu, 0x881D0011FE12B8D6u,
    0x9A1D38CA8C1C423Eu, 0xBEB93DDA89014B82u, 0x53DAF69F796538A6u, 0x3DD364AE72E11A4Bu,
    0x5E8C33E3A65E90A5u, 0x06C85C03B59FC416u, 0x794DBC3FC8F3B04Fu, 0xEE4FA86AFB2423BFu,
    0xDE49FD9964C66916u, 0x1C3599BC89B1287Eu, 0x9EBE1A0EA7C42A3Fu, 0x9E0994607FB38EA4u,
    0x7D8B36D0451CBC3Eu, 0xF9A7DE463175D37Bu, 0x66A7455D978EA7A9u, 0xCAF6E24B78C4D0EEu,
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
