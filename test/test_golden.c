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
    0x44993AC9E0C36A3Du, 0x0DE42EB3B209CF92u, 0x8E6081F7A2118E22u, 0x0A5AE19A7A7BE956u,
    0xD793AE5F7C54D8D0u, 0xBC3DA3260443EC6Fu, 0x20727F3A7C95CFEAu, 0xAE633446BBA6F3DBu,
    0x44BF2BC183914CC7u, 0x74FB34B82554A5F9u, 0xF8E1620DDA3B5659u, 0xD493F29B33BF4345u,
    0x83D2C6F9A323D773u, 0x83DAF4BFA349F529u, 0x09B3B2E669512E7Cu, 0x8F38711A5FEE78A7u,
    0x40C48DD66B374BD8u, 0xFA20BF6107C34F7Cu, 0xF2C4E272C36AE6D9u, 0x5ABA093A1D260F42u};

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
