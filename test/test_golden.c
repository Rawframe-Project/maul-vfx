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
    0xE0158A826BD9AB26u, 0x32AFF0E899801BE4u, 0x7CC25F2416B51DE7u, 0xE57587A3EB8A1442u,
    0x9F56ED76CE4B08A0u, 0x34863682EDAF5F99u, 0xE9603B3C0959EAB7u, 0xE81A642D25296610u,
    0x1AD3C0E61340A7A8u, 0x80C26C71381E7261u, 0xBF7EB78F5DA5FA98u, 0x63A898422821D948u,
    0x577E771521EF2463u, 0x9939393B6D02B582u, 0x0AE2682C49C82422u, 0xDCF4312B475BF8B0u,
    0xE6D114AE3C1926AAu, 0x5CDDB0E46A28C508u, 0x5D0BA511AFB5C22Cu, 0xA2FA2AF718CA7E2Cu,
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
