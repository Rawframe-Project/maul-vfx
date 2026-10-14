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
    0x3FF4DAF82CEBF6CEu, 0x9A4B8052D9952394u, 0xE377FE5E41C473AFu, 0x6C6C3A2E23F3B078u,
    0x6F6AE02E4604F3BEu, 0xF3EDD5166F4FC2D3u, 0x95125F071A25147Au, 0xC09E0DCA3B0434E9u,
    0x5F53A4AEC9205C46u, 0xB374E6C44F01E171u, 0xE061138210ABE363u, 0xD9F5C38909A29418u,
    0xD256B3E1692EF491u, 0x639276D83138284Bu, 0x8C732F5429074AF1u, 0xE5E751B5937DCF5Bu,
    0x22ECDBBC9FF38D8Bu, 0x94046F6BD2C31775u, 0x59BBAC35BBB6111Du, 0x5DA9C6A13E24497Eu,
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
