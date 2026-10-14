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
    0x2104D199E1B9E17Au, 0x4D39A453562CD154u, 0x7EED00E1B0B2F5E0u, 0x6C35D5E9E125CAB8u,
    0xE00054420F2BCAB7u, 0x84744CB279D722D5u, 0x2A40EBA37143BD12u, 0x4ED5B35CFEB8A5F7u,
    0x92E728C2FE8DD7F4u, 0x9EEA59DBC4939FFBu, 0x35E79D660C5C9479u, 0xDE88E7A5D3CF7238u,
    0x497EB41715392364u, 0xEF5E2056B15C066Cu, 0x9A0E28F29EB38C75u, 0x04BC2488F5E03D6Au,
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
