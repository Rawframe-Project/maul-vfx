// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The scene's state hashed after every step against values committed here
// (record mvfx-0001: the same bits on every platform). CI runs it on every
// platform of its matrix; one hash differing anywhere fails. A change meant
// to change results updates the values and says so in the changelog. Set
// MVFX_PRINT_GOLDEN to print the values this build computes.

#include "vfx_scene.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static const uint64_t s_golden[SCENE_STEP_COUNT] = {
    0xACE09DC97D5DDADEu, 0x5F6447E54A0D44B5u, 0xF31D84CAF7F8433Du, 0xF23601A13C2BE24Cu,
    0xB6F5660C8AAF1E3Au, 0xD54E813687E5AC7Fu, 0x2C053B90522197E2u, 0xEFAB75BF3F219CB9u,
    0x5185D7943335160Cu, 0x67FF5AF5A7E18814u, 0xF7820EB7EDEF924Cu, 0xCE30A65A9542DB06u,
    0x3559F74F451A2854u, 0x4A5BF7C84110E76Fu, 0xF5495520709C3CB6u, 0x86209DCA292377D2u,
    0x044500AE1C66A2F0u, 0x39728C1D9F725D0Eu, 0x819D11F142F7A83Bu, 0x27228C4A975F70DFu,
};

int main(void)
{
    mvfxContextDef def = mvfxDefaultContextDef();
    mvfxContext* context = NULL;
    CHECK(mvfxCreateContext(&def, &context) == mvfx_success, "context");
    BuildScene(context);
    bool print = getenv("MVFX_PRINT_GOLDEN") != NULL;
    for (int32_t step = 0; step < SCENE_STEP_COUNT; ++step)
    {
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
